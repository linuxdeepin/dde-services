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

enum Category {
    System = 1,
    App,
    Custom
};

// Base configuration information
struct BaseConfig
{
    QString appId;              // Application ID
    QString subPath;            // DConfig subPath (e.g. [appId].shortcut.xxx)
    QString displayName;        // Localized name
    int category;               // 1: System, 2: App, 3: Custom
    bool enabled;
    bool modifiable;
    int triggerType;            // 1: Command, 2: App, 3: Action
    QStringList triggerValue;   // Command args, AppID, or Action Enum Value (as string list)

    QString getId() const { return subPath; }
};

// Keyboard shortcut configuration
struct KeyConfig : public BaseConfig
{
    QStringList hotkeys;    // List of key combinations
    int keyEventFlags;      // KeyEventFlag bitfield: Press=0x1, Release=0x2, Repeat=0x4

    KeyConfig() : keyEventFlags(KeyEventFlag::Release) {} // Default to release

    bool isValid() const { return enabled && !appId.isEmpty() && !displayName.isEmpty(); }

    bool operator==(const KeyConfig &other) const {
        return appId == other.appId &&
               subPath == other.subPath &&
               displayName == other.displayName &&
               category == other.category &&
               enabled == other.enabled &&
               modifiable == other.modifiable &&
               triggerType == other.triggerType &&
               triggerValue == other.triggerValue &&
               hotkeys == other.hotkeys &&
               keyEventFlags == other.keyEventFlags;
    }

    bool operator!=(const KeyConfig &other) const {
        return !(*this == other);
    }
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
};

Q_DECLARE_METATYPE(KeyConfig)
Q_DECLARE_METATYPE(GestureConfig)
