#!/bin/bash

# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later
#

# =============================================================================
# dde-wallpaper-cache functional test script (D-Bus CLI version)
# Usage: sudo bash test_dbus.sh [wallpaper_image_path]
# =============================================================================

set -e

# ---- Config ----
WALLPAPER="${1:-/usr/share/backgrounds/default_background.jpg}"
SERVICE_WC="org.deepin.dde.WallpaperCache"
PATH_WC="/org/deepin/dde/WallpaperCache"
SERVICE_IE="org.deepin.dde.ImageEffect1"
PATH_IE="/org/deepin/dde/ImageEffect1"
SERVICE_IB="org.deepin.dde.ImageBlur1"
PATH_IB="/org/deepin/dde/ImageBlur1"
BLUR_CACHE_DIR="/var/cache/dde-wallpaper-cache/blur"
SIZE_CACHE_DIR="/var/cache/dde-wallpaper-cache"

# Color output
GREEN="\033[0;32m"; RED="\033[0;31m"; YELLOW="\033[1;33m"; NC="\033[0m"
FAILURES=0
pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC} $1"; FAILURES=$((FAILURES + 1)); }
info() { echo -e "${YELLOW}[INFO]${NC} $1"; }
section() { echo -e "\n${YELLOW}======== $1 ========${NC}"; }

# Extract the first string value from gdbus call output, e.g. ('value',) or (['v1', 'v2'],)
gdbus_extract_string() {
    echo "$1" | grep -oP "(?<=')[^']+(?=')" | head -1
}
gdbus_extract_strings() {
    echo "$1" | grep -oP "(?<=')[^']+(?=')"
}

# Check if a file is a valid image using the file command
check_valid_image() {
    local filepath="$1"
    file "$filepath" | grep -qiE "image|JPEG|PNG|BMP|GIF|TIFF|WebP"
}

# Get image dimensions using identify (ImageMagick) if available
get_image_dimensions() {
    local filepath="$1"
    if command -v identify &>/dev/null; then
        identify -format "%wx%h" "$filepath" 2>/dev/null
    else
        echo ""
    fi
}

# ---- Pre-checks ----
section "Pre-checks"
if [ ! -f "$WALLPAPER" ]; then
    fail "Test wallpaper file not found: $WALLPAPER"
    exit 1
fi
info "Using wallpaper: $WALLPAPER"

if ! dbus-send --system --print-reply \
    --dest="$SERVICE_WC" "$PATH_WC" \
    org.freedesktop.DBus.Introspectable.Introspect &>/dev/null; then
    fail "WallpaperCache service unreachable, please start dde-wallpaper-cache first"
    exit 1
fi
pass "WallpaperCache service reachable"

if ! dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    org.freedesktop.DBus.Introspectable.Introspect &>/dev/null; then
    fail "ImageEffect1 compatibility service unreachable"
    exit 1
fi
pass "ImageEffect1 compatibility service reachable"

# ---- Test 1: ImageEffect1.Get() blur effect ----
section "Test 1: ImageEffect1.Get() blur effect"
info "Calling ImageEffect1.Get(\"\", \"$WALLPAPER\")..."
BLUR_PATH=$(dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Get" string:"" string:"$WALLPAPER" \
    2>&1 | grep -oP '(?<=string ").*(?=")')

if [ -z "$BLUR_PATH" ]; then
    fail "Get() returned empty string"
else
    info "Blur image path: $BLUR_PATH"
    if [ -f "$BLUR_PATH" ] && [ -s "$BLUR_PATH" ]; then
        SIZE=$(du -h "$BLUR_PATH" | cut -f1)
        pass "Blur image file exists, size: $SIZE"
    else
        fail "Blur image file missing or empty: $BLUR_PATH"
    fi

    # Validate file is a real image
    if check_valid_image "$BLUR_PATH"; then
        pass "Blur image is a valid image file"
    else
        fail "Blur image is not a valid image file"
    fi

    # Validate dimensions match original
    ORIG_DIM=$(get_image_dimensions "$WALLPAPER")
    BLUR_DIM=$(get_image_dimensions "$BLUR_PATH")
    if [ -n "$ORIG_DIM" ] && [ -n "$BLUR_DIM" ]; then
        if [ "$ORIG_DIM" = "$BLUR_DIM" ]; then
            pass "Blur image dimensions match original: $BLUR_DIM"
        else
            fail "Blur image dimensions mismatch: original=$ORIG_DIM blur=$BLUR_DIM"
        fi
    else
        info "ImageMagick not available, skipping dimension check"
    fi

    # Validate cached in correct directory
    if echo "$BLUR_PATH" | grep -q "^$BLUR_CACHE_DIR"; then
        pass "Blur image stored in correct cache directory"
    else
        fail "Blur image not in expected cache directory: $BLUR_CACHE_DIR"
    fi
fi

# ---- Test 2: Cache hit (second call should be faster) ----
section "Test 2: Cache hit verification"
info "Second call to Get(), expecting cached response..."
T1=$(date +%s%3N)
BLUR_PATH2=$(dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Get" string:"" string:"$WALLPAPER" \
    2>&1 | grep -oP '(?<=string ").*(?=")')
T2=$(date +%s%3N)
ELAPSED=$((T2 - T1))

if [ "$BLUR_PATH" = "$BLUR_PATH2" ]; then
    pass "Two calls returned the same path: $BLUR_PATH2 (${ELAPSED}ms)"
    if [ "$ELAPSED" -lt 200 ]; then
        pass "Cache hit, response time ${ELAPSED}ms < 200ms"
    else
        info "Response time ${ELAPSED}ms, may not have hit memory cache (first disk read)"
    fi
else
    fail "Two calls returned different paths"
fi

# ---- Test 3: GetBlurImagePath() dedicated interface ----
section "Test 3: WallpaperCache.GetBlurImagePath()"
BLUR_PATH3=$(dbus-send --system --print-reply \
    --dest="$SERVICE_WC" "$PATH_WC" \
    "${SERVICE_WC}.GetBlurImagePath" string:"$WALLPAPER" \
    2>&1 | grep -oP '(?<=string ").*(?=")')

if [ "$BLUR_PATH3" = "$BLUR_PATH" ]; then
    pass "GetBlurImagePath matches ImageEffect1.Get result"
else
    fail "Path mismatch: GetBlurImagePath=$BLUR_PATH3  Get=$BLUR_PATH"
fi

# ---- Test 4: Get() with explicit "pixmix" effect name ----
section "Test 4: ImageEffect1.Get(\"pixmix\") explicit effect"
BLUR_PIXMIX=$(dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Get" string:"pixmix" string:"$WALLPAPER" \
    2>&1 | grep -oP '(?<=string ").*(?=")')

if [ -n "$BLUR_PIXMIX" ] && [ "$BLUR_PIXMIX" = "$BLUR_PATH" ]; then
    pass "Get(\"pixmix\") returns same path as Get(\"\")"
else
    fail "Get(\"pixmix\") result mismatch or empty: $BLUR_PIXMIX"
fi

# ---- Test 5: Get() with unsupported effect ----
section "Test 5: ImageEffect1.Get() unsupported effect"
BLUR_BAD=$(dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Get" string:"unsupported_effect" string:"$WALLPAPER" \
    2>&1 | grep -oP '(?<=string ").*(?=")')

if [ -z "$BLUR_BAD" ]; then
    pass "Get(\"unsupported_effect\") returned empty (expected)"
else
    fail "Get(\"unsupported_effect\") should return empty, got: $BLUR_BAD"
fi

# ---- Test 6: GetProcessedImagePaths() size scaling ----
section "Test 6: WallpaperCache.GetProcessedImagePaths() size scaling"
info "Requesting 1920x1080 scaling..."
RAW_OUTPUT=$(gdbus call --system \
    --dest "$SERVICE_WC" \
    --object-path "$PATH_WC" \
    --method "${SERVICE_WC}.GetProcessedImagePaths" \
    "$WALLPAPER" \
    "[<(1920, 1080)>]" 2>&1)
SCALED_PATHS=$(gdbus_extract_string "$RAW_OUTPUT")

if [ -n "$SCALED_PATHS" ]; then
    info "First call returned: $SCALED_PATHS"
    # Wait for async processing (first call may return original path)
    sleep 3
    # Call again to check if cache was generated
    RAW_OUTPUT2=$(gdbus call --system \
        --dest "$SERVICE_WC" \
        --object-path "$PATH_WC" \
        --method "${SERVICE_WC}.GetProcessedImagePaths" \
        "$WALLPAPER" \
        "[<(1920, 1080)>]" 2>&1)
    SCALED_PATHS2=$(gdbus_extract_string "$RAW_OUTPUT2")
    info "Second call returned: $SCALED_PATHS2"
    if echo "$SCALED_PATHS2" | grep -q "_"; then
        pass "Size cache generated (path contains _ size suffix)"
        # Validate scaled image dimensions
        if [ -f "$SCALED_PATHS2" ]; then
            SCALED_DIM=$(get_image_dimensions "$SCALED_PATHS2")
            if [ -n "$SCALED_DIM" ]; then
                info "Scaled image actual dimensions: $SCALED_DIM"
                if [ "$SCALED_DIM" = "1920x1080" ]; then
                    pass "Scaled image dimensions correct: 1920x1080"
                else
                    fail "Scaled image dimensions wrong: expected 1920x1080, got $SCALED_DIM"
                fi
            fi
            # Validate it's a real image
            if check_valid_image "$SCALED_PATHS2"; then
                pass "Scaled image is a valid image file"
            else
                fail "Scaled image is not a valid image file"
            fi
        fi
    else
        info "Size image still being generated or same as original"
    fi
else
    fail "GetProcessedImagePaths returned empty"
fi

# ---- Test 7: GetProcessedImageWithBlur() blur+size in one step ----
section "Test 7: WallpaperCache.GetProcessedImageWithBlur() blur+size"
RAW_OUTPUT=$(gdbus call --system \
    --dest "$SERVICE_WC" \
    --object-path "$PATH_WC" \
    --method "${SERVICE_WC}.GetProcessedImageWithBlur" \
    "$WALLPAPER" \
    "[<(1920, 1080)>]" \
    true 2>&1)
BLUR_SCALED_LIST=$(gdbus_extract_strings "$RAW_OUTPUT")

if [ -n "$BLUR_SCALED_LIST" ]; then
    BLUR_SCALED=$(echo "$BLUR_SCALED_LIST" | head -1)
    info "Returned: $BLUR_SCALED"
    if [ -f "$BLUR_SCALED" ]; then
        pass "GetProcessedImageWithBlur returned file exists"
        if check_valid_image "$BLUR_SCALED"; then
            pass "Blur+scaled image is a valid image file"
        else
            fail "Blur+scaled image is not a valid image file"
        fi
    else
        info "File may still be generating asynchronously: $BLUR_SCALED"
    fi
else
    fail "GetProcessedImageWithBlur returned empty"
fi

# needBlur=false should return non-blur path
RAW_OUTPUT_NB=$(gdbus call --system \
    --dest "$SERVICE_WC" \
    --object-path "$PATH_WC" \
    --method "${SERVICE_WC}.GetProcessedImageWithBlur" \
    "$WALLPAPER" \
    "[<(1920, 1080)>]" \
    false 2>&1)
NO_BLUR_RESULTS=$(gdbus_extract_strings "$RAW_OUTPUT_NB")

if [ -n "$NO_BLUR_RESULTS" ]; then
    NO_BLUR_FIRST=$(echo "$NO_BLUR_RESULTS" | head -1)
    if echo "$NO_BLUR_FIRST" | grep -qv "/blur/"; then
        pass "needBlur=false returned path not in blur directory"
    else
        fail "needBlur=false returned path from blur directory: $NO_BLUR_FIRST"
    fi
else
    fail "GetProcessedImageWithBlur(needBlur=false) returned empty"
fi

# ---- Test 8: ImageEffect1.Delete() delete cache ----
section "Test 8: ImageEffect1.Delete() delete blur cache"
info "Deleting blur cache for $WALLPAPER..."
dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Delete" string:"pixmix" string:"$WALLPAPER" \
    2>&1 | tail -1

if [ -n "$BLUR_PATH" ] && [ ! -f "$BLUR_PATH" ]; then
    pass "Blur cache file deleted"
elif [ -n "$BLUR_PATH" ] && [ -f "$BLUR_PATH" ]; then
    fail "Blur cache file still exists (Delete did not take effect)"
else
    info "Cannot verify deletion (original path empty)"
fi

# ---- Test 9: Regenerate after delete ----
section "Test 9: Regenerate after delete"
info "Calling Get() again to verify regeneration..."
BLUR_PATH_NEW=$(dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Get" string:"" string:"$WALLPAPER" \
    2>&1 | grep -oP '(?<=string ").*(?=")')

if [ -n "$BLUR_PATH_NEW" ] && [ -f "$BLUR_PATH_NEW" ] && [ -s "$BLUR_PATH_NEW" ]; then
    pass "Regeneration successful: $BLUR_PATH_NEW"
else
    fail "Regeneration failed"
fi

# ---- Test 10: Delete("all") deletes all effect caches ----
section "Test 10: ImageEffect1.Delete(\"all\") delete all"
# Ensure blur exists first
dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Get" string:"" string:"$WALLPAPER" &>/dev/null

BLUR_BEFORE=$(dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Get" string:"" string:"$WALLPAPER" \
    2>&1 | grep -oP '(?<=string ").*(?=")')

dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Delete" string:"all" string:"$WALLPAPER" \
    2>&1 | tail -1

sleep 0.2
if [ -n "$BLUR_BEFORE" ] && [ ! -f "$BLUR_BEFORE" ]; then
    pass "Delete(\"all\") removed blur cache file"
else
    fail "Delete(\"all\") did not remove blur cache file: $BLUR_BEFORE"
fi

# Restore for subsequent tests
dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Get" string:"" string:"$WALLPAPER" &>/dev/null

# ---- Test 11: ImageBlur1.Get() compatibility interface ----
section "Test 11: ImageBlur1.Get() compatibility interface"
if dbus-send --system --print-reply \
    --dest="$SERVICE_IB" "$PATH_IB" \
    org.freedesktop.DBus.Introspectable.Introspect &>/dev/null; then
    pass "ImageBlur1 compatibility service reachable"

    BLUR1_PATH=$(dbus-send --system --print-reply \
        --dest="$SERVICE_IB" "$PATH_IB" \
        "${SERVICE_IB}.Get" string:"$WALLPAPER" \
        2>&1 | grep -oP '(?<=string ").*(?=")')

    if [ -n "$BLUR1_PATH" ] && [ -f "$BLUR1_PATH" ]; then
        pass "ImageBlur1.Get() returned valid path: $BLUR1_PATH"
        # Consistency check
        BLUR_WC=$(dbus-send --system --print-reply \
            --dest="$SERVICE_WC" "$PATH_WC" \
            "${SERVICE_WC}.GetBlurImagePath" string:"$WALLPAPER" \
            2>&1 | grep -oP '(?<=string ").*(?=")')
        if [ "$BLUR1_PATH" = "$BLUR_WC" ]; then
            pass "ImageBlur1.Get matches GetBlurImagePath"
        else
            fail "ImageBlur1.Get path mismatch: $BLUR1_PATH vs $BLUR_WC"
        fi
    else
        fail "ImageBlur1.Get() returned empty or file not found"
    fi
else
    fail "ImageBlur1 compatibility service unreachable"
fi

# ---- Test 12: ImageBlur1.Delete() compatibility interface ----
section "Test 12: ImageBlur1.Delete()"
if [ -n "$BLUR1_PATH" ] && [ -f "$BLUR1_PATH" ]; then
    dbus-send --system --print-reply \
        --dest="$SERVICE_IB" "$PATH_IB" \
        "${SERVICE_IB}.Delete" string:"$WALLPAPER" \
        2>&1 | tail -1

    sleep 0.2
    if [ ! -f "$BLUR1_PATH" ]; then
        pass "ImageBlur1.Delete() file removed"
    else
        fail "ImageBlur1.Delete() file still exists"
    fi

    # Restore
    dbus-send --system --print-reply \
        --dest="$SERVICE_IB" "$PATH_IB" \
        "${SERVICE_IB}.Get" string:"$WALLPAPER" &>/dev/null
else
    info "Skipping ImageBlur1.Delete() test (no blur image)"
fi

# ---- Test 13: GetWallpaperListForScreen() with multiple sizes ----
section "Test 13: WallpaperCache.GetWallpaperListForScreen() multi-size"
RAW_OUTPUT=$(gdbus call --system \
    --dest "$SERVICE_WC" \
    --object-path "$PATH_WC" \
    --method "${SERVICE_WC}.GetWallpaperListForScreen" \
    "$WALLPAPER" \
    "[<(1920, 1080)>, <(2560, 1440)>]" \
    true 2>&1)
WLFS_RESULTS=$(gdbus_extract_strings "$RAW_OUTPUT")

if [ -n "$WLFS_RESULTS" ]; then
    COUNT=$(echo "$WLFS_RESULTS" | wc -l)
    pass "GetWallpaperListForScreen(needBlur=true) returned $COUNT path(s)"
    echo "$WLFS_RESULTS" | while read -r p; do
        info "  -> $p"
    done
else
    fail "GetWallpaperListForScreen returned empty"
fi

# needBlur=false
RAW_OUTPUT=$(gdbus call --system \
    --dest "$SERVICE_WC" \
    --object-path "$PATH_WC" \
    --method "${SERVICE_WC}.GetWallpaperListForScreen" \
    "$WALLPAPER" \
    "[<(1920, 1080)>, <(2560, 1440)>]" \
    false 2>&1)
WLFS_RESULTS2=$(gdbus_extract_strings "$RAW_OUTPUT")

if [ -n "$WLFS_RESULTS2" ]; then
    pass "GetWallpaperListForScreen(needBlur=false) returned results"
else
    fail "GetWallpaperListForScreen(needBlur=false) returned empty"
fi

# ---- Test 14: Non-existent file error handling ----
section "Test 14: Non-existent file error handling"
FAKE_PATH="/tmp/nonexistent_wallpaper_12345.jpg"
RESULT_FAKE=$(dbus-send --system --print-reply \
    --dest="$SERVICE_IE" "$PATH_IE" \
    "${SERVICE_IE}.Get" string:"" string:"$FAKE_PATH" \
    2>&1 | grep -oP '(?<=string ").*(?=")')

if [ -z "$RESULT_FAKE" ]; then
    pass "Get() with non-existent file returned empty (service not crashed)"
else
    info "Get() with non-existent file returned: $RESULT_FAKE"
fi

# Verify service is still alive after error
if dbus-send --system --print-reply \
    --dest="$SERVICE_WC" "$PATH_WC" \
    org.freedesktop.DBus.Introspectable.Introspect &>/dev/null; then
    pass "Service still alive after error handling"
else
    fail "Service crashed after error handling"
fi

# ---- Cache directory status ----
section "Cache directory status"
info "Blur cache dir: $BLUR_CACHE_DIR"
ls -lh "$BLUR_CACHE_DIR" 2>/dev/null || echo "  (directory not found or no permission)"
info "Size cache dir: $SIZE_CACHE_DIR"
ls -lh "$SIZE_CACHE_DIR" 2>/dev/null | head -10 || echo "  (directory not found or no permission)"

# ---- Summary ----
section "Test complete"
echo "Tip: observe service log with:"
echo "  sudo journalctl -u dde-wallpaper-cache -f"

if [ "$FAILURES" -gt 0 ]; then
    echo -e "${RED}$FAILURES test(s) failed${NC}"
    exit 1
else
    echo -e "${GREEN}All tests passed${NC}"
    exit 0
fi
