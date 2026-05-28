// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

enum ActionCmd {
    // Audio control
    AudioSinkMuteToggle = 1,
    AudioSinkVolumeUp,
    AudioSinkVolumeDown,
    AudioSourceMuteToggle,
    
    // Media player control
    MediaPlayerPlay,
    MediaPlayerPause,
    MediaPlayerStop,
    MediaPlayerPrevious,
    MediaPlayerNext,
    MediaPlayerRewind,
    MediaPlayerForward,
    MediaPlayerRepeat,
    
    // Display control
    MonitorBrightnessUp,
    MonitorBrightnessDown,
    DisplayModeSwitch,
    AdjustBrightnessSwitch,
    
    // Keyboard backlight control
    KbdLightToggle,
    KbdLightBrightnessUp,
    KbdLightBrightnessDown,
    
    // Touchpad control
    TouchpadToggle,
    TouchpadOn,
    TouchpadOff
};

// Power Actions
enum PowerAction {
    PowerActionShutdown = 0,
    PowerActionSuspend = 1,
    PowerActionHibernate = 2,
    PowerActionTurnOffScreen = 3,
    PowerActionShowUI = 4
};

namespace Config {

// DConfig App ID
constexpr const char *DSETTINGS_APP_ID = "org.deepin.dde.daemon";

// DConfig Names
constexpr const char *DSETTINGS_KEYBINDING_NAME = "org.deepin.dde.daemon.keybinding";
constexpr const char *DSETTINGS_KEYBINDING_SYSTEM = "org.deepin.dde.daemon.keybinding.system";
constexpr const char *DSETTINGS_KEYBINDING_MEDIAKEY = "org.deepin.dde.daemon.keybinding.mediakey";
constexpr const char *DSETTINGS_KEYBINDING_WRAP_GNOME_WM = "org.deepin.dde.daemon.keybinding.wrap.gnome.wm";
constexpr const char *DSETTINGS_KEYBINDING_ENABLE = "org.deepin.dde.daemon.keybinding.enable";
constexpr const char *DSETTINGS_POWER = "org.deepin.dde.daemon.power";
constexpr const char *DSETTINGS_KEYBOARD = "org.deepin.dde.daemon.keyboard";

// Config Keys
constexpr const char *KEY_WIRELESS_CONTROL_ENABLE = "wirelessControlEnable";
constexpr const char *KEY_NEED_XRANDR_Q_DEVICES = "needXrandrQDevices";
constexpr const char *KEY_DEVICE_MANAGER_CONTROL_ENABLE = "deviceManagerControlEnable";
constexpr const char *KEY_NUM_LOCK_STATE = "numlockState";
constexpr const char *KEY_SAVE_NUM_LOCK_STATE = "saveNumlockState";
constexpr const char *KEY_SHORTCUT_SWITCH_LAYOUT = "shortcutSwitchLayout";

// OSD related keys
constexpr const char *KEY_SHOW_CAPS_LOCK_OSD = "capslockToggle";
constexpr const char *KEY_OSD_ADJUST_VOLUME_STATE = "osdAdjustVolumeEnabled";
constexpr const char *KEY_OSD_ADJUST_BRIGHTNESS_STATE = "osdAdjustBrightnessEnabled";

// Power related keys
constexpr const char *KEY_BATTERY_PRESS_POWER_BTN_ACTION = "batteryPressPowerButton";
constexpr const char *KEY_LINE_POWER_PRESS_POWER_BTN_ACTION = "linePowerPressPowerButton";
constexpr const char *KEY_SCREEN_BLACK_LOCK = "screenBlackLock";
constexpr const char *KEY_HIGH_PERFORMANCE_ENABLED = "highPerformanceEnabled";
constexpr const char *KEY_SLEEP_LOCK = "sleepLock";
constexpr const char *KEY_AMBIENT_LIGHT_ADJUST_BRIGHTNESS = "ambientLightAdjustBrightness";

// Other keys
constexpr const char *KEY_UPPER_LAYER_WLAN = "upperLayerWlan";

} // end namespace Config