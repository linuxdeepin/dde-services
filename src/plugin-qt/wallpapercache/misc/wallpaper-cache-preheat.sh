#!/bin/sh
# Pre-generate blur cache for the default wallpaper to avoid D-Bus
# on-demand activation cold-start delay on first boot.
#
# The wallpapercache plugin (org.deepin.dde.ImageEffect1) is loaded
# on-demand by deepin-service-manager. On a freshly installed system
# the blur cache directory is empty, so the first Get() call must
# cold-start the service AND generate the blur image from scratch,
# which can exceed the greeter client's timeout.
#
# This script triggers the service early in boot (before the display
# manager) so that the blur file is cached on disk by the time the
# greeter requests it. Failures are non-fatal — the greeter will
# still fall back to the original wallpaper.

DEFAULT_WALLPAPER="/usr/share/backgrounds/default_background.jpg"

[ -f "$DEFAULT_WALLPAPER" ] || exit 0

# Trigger D-Bus activation of the wallpapercache plugin and request
# blur generation for the default wallpaper. Use a generous timeout
# (30 s) to accommodate cold-start + image processing on slow hardware.
dbus-send --system --print-reply --reply-timeout=30000 \
    --dest=org.deepin.dde.ImageEffect1 \
    /org/deepin/dde/ImageEffect1 \
    org.deepin.dde.ImageEffect1.Get \
    string:"" string:"$DEFAULT_WALLPAPER" >/dev/null 2>&1 || true

exit 0
