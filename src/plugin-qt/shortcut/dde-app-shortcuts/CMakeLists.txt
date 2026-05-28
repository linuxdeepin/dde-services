# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(Dtk6 REQUIRED COMPONENTS DConfig)

file(GLOB CONFIG_SUBDIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/org.deepin.dde.keybinding.shortcut.app.*"
)

foreach(SUBDIR_PATH ${CONFIG_SUBDIRS})
    dtk_add_config_meta_files(
        APPID "org.deepin.dde.keybinding"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/configs"
        FILES "${SUBDIR_PATH}/org.deepin.shortcut.json"
    )
endforeach()

install(FILES configs/org.deepin.dde.shortcut.dde-app.ini
        DESTINATION share/deepin/org.deepin.dde.keybinding)

# ============ i18n Translation Support ============
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/DdeShortcutI18n.cmake")

set(I18N_LANGUAGES
    "zh_CN" "zh_HK" "zh_TW" "en" "ast" "az" "bg" "bo" "ca" "cs" "da"
    "de" "el" "es" "et" "eu" "fa" "fi" "fr" "gl" "he" "hi" "hr" "hu"
    "hy" "id" "it" "ja" "ka" "kk" "ko" "ky" "lt" "lv" "ms" "nb" "ne"
    "nl" "pa" "pl" "pt_BR" "pt" "ro" "ru" "sk" "sl" "sq" "sr" "sv" "th"
    "tr" "ug" "uk" "vi"
)

dde_shortcut_add_translations(
    APP_ID "org.deepin.dde.shortcut.dde-app"
    CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/configs"
    TS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/translations"
    LANGUAGES ${I18N_LANGUAGES}
)
