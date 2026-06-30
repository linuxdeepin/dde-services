// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QMetaType>

enum class TriggerType {
    Command = 1,
    App,
    Action
};

// Key event flags (bitfield, matches Wayland protocol keybind_flag)
namespace KeyEventFlag {
    constexpr int Press = 0x1;    // Trigger on key press
    constexpr int Release = 0x2; // Trigger on key release
    constexpr int Repeat = 0x4;  // Trigger on key repeat (auto-repeat)
}

// Framework-reserved shortcut categories.
namespace CategoryKey {
    inline constexpr const char *System = "System";
    inline constexpr const char *Window = "Window";
    inline constexpr const char *Workspace = "Workspace";
    inline constexpr const char *Custom = "Custom";
}

// Base configuration information
struct BaseConfig
{
    QString appId;              // Application ID
    QString subPath;            // DConfig subPath (e.g. [appId].shortcut.xxx)
    QString displayName;        // Localized name
    QString category;           // Logical category key (e.g. "System", "Window", app-defined)
    bool enabled;
    bool modifiable;
    int triggerType;            // 1: Command, 2: App, 3: Action
    QStringList triggerValue;   // Command args, AppID, or Action Enum Value (as string list)

    QString getId() const { return subPath; }
    bool operator==(const BaseConfig &other) const {
        return appId == other.appId
                && subPath == other.subPath
                && displayName == other.displayName
                && category == other.category
                && enabled == other.enabled
                && modifiable == other.modifiable
                && triggerType == other.triggerType
                && triggerValue == other.triggerValue;
    }
    bool operator!=(const BaseConfig &other) const { return !(*this == other); }
};

// Keyboard shortcut configuration
struct KeyConfig : public BaseConfig
{
    QStringList hotkeys;    // List of key combinations
    int keyEventFlags;      // KeyEventFlag bitfield: Press=0x1, Release=0x2, Repeat=0x4

    KeyConfig() : keyEventFlags(KeyEventFlag::Release) {} // Default to release

    bool isValid() const { return enabled && !appId.isEmpty() && !displayName.isEmpty(); }
    bool canRegister() const { return isValid() && !hotkeys.isEmpty(); }
    // Valid enough to display (has identity) but carries no key binding; shown
    // as "None" after another shortcut takes its binding.
    bool isDisplayOnly() const { return enabled && modifiable && !appId.isEmpty() && !displayName.isEmpty() && hotkeys.isEmpty(); }
    bool operator==(const KeyConfig &other) const {
        return static_cast<const BaseConfig &>(*this) == static_cast<const BaseConfig &>(other)
                && hotkeys == other.hotkeys
                && keyEventFlags == other.keyEventFlags;
    }
    bool operator!=(const KeyConfig &other) const { return !(*this == other); }
};

enum class GestureType {
    Swipe = 1,
    Hold
};

// Gesture shortcut configuration
struct GestureConfig : public BaseConfig
{
    int gestureType;        // 1: Swipe, 2: Hold
    int fingerCount;
    int direction;          // 0: None, 1: Down, 2: Left, 3: Up, 4: Right

    bool isValid() const { return enabled && !appId.isEmpty() && !displayName.isEmpty() && gestureType > 0 && fingerCount > 0; }
    bool operator==(const GestureConfig &other) const {
        return static_cast<const BaseConfig &>(*this) == static_cast<const BaseConfig &>(other)
                && gestureType == other.gestureType
                && fingerCount == other.fingerCount
                && direction == other.direction;
    }
    bool operator!=(const GestureConfig &other) const { return !(*this == other); }
};

Q_DECLARE_METATYPE(KeyConfig)
Q_DECLARE_METATYPE(GestureConfig)
