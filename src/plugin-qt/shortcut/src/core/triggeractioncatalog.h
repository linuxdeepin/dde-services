// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QString>

#include <optional>

// Stable IDs used by shortcut/gesture configurations and D-Bus. These values
// must not follow protocol enum renumbering; translate them at the backend.
enum class TriggerActionId {
    Invalid = -1,
    Disable = 0,
    Notify = 1,
    Workspace1 = 2,
    Workspace2 = 3,
    Workspace3 = 4,
    Workspace4 = 5,
    Workspace5 = 6,
    Workspace6 = 7,
    PreviousWorkspace = 8,
    NextWorkspace = 9,
    ShowDesktop = 10,
    MaximizeWindow = 11,
    RestoreWindow = 12,
    MoveWindow = 13,
    CloseWindow = 14,
    ShowWindowMenu = 15,
    ShowMultitask = 16,
    HideMultitask = 17,
    ToggleMultitask = 18,
    ToggleFpsDisplay = 19,
    LockScreen = 20,
    ShutdownMenu = 21,
    Quit = 22,
    TaskSwitchEnter = 23,
    TaskSwitchNext = 24,
    TaskSwitchPrevious = 25,
    TaskSwitchSameAppNext = 26,
    TaskSwitchSameAppPrevious = 27,

    SplitWindowLeft = 100,
    SplitWindowRight = 101,
    HideDesktop = 102,
    ToggleGrandSearch = 103,
    ToggleLauncher = 104,
    ToggleClipboard = 105,
    ToggleNotifications = 106,
    MinimizeWindow = 110,
    ResizeWindow = 111,
    ZoomIn = 112,
    ZoomOut = 113,
    ZoomReset = 114,
    MoveToLeftWorkspace = 120,
    MoveToRightWorkspace = 121,
};

// Gesture APIs use the same action IDs as action-triggered shortcuts.
using GestureActionId = TriggerActionId;

class TriggerActionCatalog
{
public:
    static TriggerActionId resolve(const QString &value);
    static QString idString(TriggerActionId actionId);

    // Returns the KWin global-accel ID for actions that must be delegated on
    // X11.  An empty result means that the action is not a KWin shortcut.
    static QString x11WmShortcutId(TriggerActionId actionId);

    // Returns the v3 wire value for actions implemented by Treeland. Disabled,
    // removed and unimplemented actions return no value. In particular, the
    // protocol's notify = 0 must not be confused with the configuration's Disable.
    static std::optional<int> treelandActionId(TriggerActionId actionId);
};
