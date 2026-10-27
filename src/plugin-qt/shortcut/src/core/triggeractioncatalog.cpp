// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "triggeractioncatalog.h"

#include "wayland-treeland-shortcut-manager-unstable-v3-client-protocol.h"

namespace {

struct ActionBackendMapping {
    TriggerActionId id;
    const char *x11WmId;
    int treelandId;
};

// Keep backend details in one table.  Shortcut and gesture code only carries
// the logical action ID and never needs to know a configuration object path.
const ActionBackendMapping *mappingFor(TriggerActionId actionId)
{
    static constexpr ActionBackendMapping mappings[] = {
        {TriggerActionId::Disable, nullptr, -1},
        {TriggerActionId::Notify, nullptr, TREELAND_SHORTCUT_MANAGER_V3_ACTION_NOTIFY},
        {TriggerActionId::Workspace1, "switchToWorkspace1",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_WORKSPACE_1},
        {TriggerActionId::Workspace2, "switchToWorkspace2",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_WORKSPACE_2},
        {TriggerActionId::Workspace3, "switchToWorkspace3",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_WORKSPACE_3},
        {TriggerActionId::Workspace4, "switchToWorkspace4",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_WORKSPACE_4},
        {TriggerActionId::Workspace5, "switchToWorkspace5",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_WORKSPACE_5},
        {TriggerActionId::Workspace6, "switchToWorkspace6",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_WORKSPACE_6},
        {TriggerActionId::PreviousWorkspace, "switchToWorkspaceLeft",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_PREV_WORKSPACE},
        {TriggerActionId::NextWorkspace, "switchToWorkspaceRight",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_NEXT_WORKSPACE},
        {TriggerActionId::ShowDesktop, "showDesktop",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_SHOW_DESKTOP},
        {TriggerActionId::MaximizeWindow, "maximize", TREELAND_SHORTCUT_MANAGER_V3_ACTION_MAXIMIZE},
        {TriggerActionId::RestoreWindow, "unmaximize",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_CANCEL_MAXIMIZE},
        {TriggerActionId::MoveWindow, "beginMove", TREELAND_SHORTCUT_MANAGER_V3_ACTION_MOVE_WINDOW},
        {TriggerActionId::CloseWindow, "close", TREELAND_SHORTCUT_MANAGER_V3_ACTION_CLOSE_WINDOW},
        {TriggerActionId::ShowWindowMenu, "activateWindowMenu",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_SHOW_WINDOW_MENU},
        {TriggerActionId::ShowMultitask, nullptr,
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_OPEN_MULTITASK_VIEW},
        {TriggerActionId::HideMultitask, nullptr,
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_CLOSE_MULTITASK_VIEW},
        {TriggerActionId::ToggleMultitask, "previewWorkspace",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_TOGGLE_MULTITASK_VIEW},
        {TriggerActionId::ToggleFpsDisplay, nullptr,
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_TOGGLE_FPS_DISPLAY},
        {TriggerActionId::LockScreen, nullptr, TREELAND_SHORTCUT_MANAGER_V3_ACTION_LOCKSCREEN},
        {TriggerActionId::ShutdownMenu, nullptr, TREELAND_SHORTCUT_MANAGER_V3_ACTION_SHUTDOWN_MENU},
        {TriggerActionId::Quit, nullptr, -1},
        {TriggerActionId::TaskSwitchEnter, nullptr, -1},
        {TriggerActionId::TaskSwitchNext, "switchApplications",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_TASKSWITCH_NEXT},
        {TriggerActionId::TaskSwitchPrevious, "switchApplicationsBackward",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_TASKSWITCH_PREV},
        {TriggerActionId::TaskSwitchSameAppNext, "switchGroup",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_TASKSWITCH_SAMEAPP_NEXT},
        {TriggerActionId::TaskSwitchSameAppPrevious, "switchGroupBackward",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_TASKSWITCH_SAMEAPP_PREV},
        {TriggerActionId::SplitWindowLeft, "toggleToLeft",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_TILE_LEFT},
        {TriggerActionId::SplitWindowRight, "toggleToRight",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_TILE_RIGHT},
        {TriggerActionId::HideDesktop, nullptr, -1},
        {TriggerActionId::ToggleGrandSearch, nullptr, -1},
        {TriggerActionId::ToggleLauncher, nullptr, -1},
        {TriggerActionId::ToggleClipboard, nullptr, -1},
        {TriggerActionId::ToggleNotifications, nullptr, -1},
        {TriggerActionId::MinimizeWindow, "minimize", TREELAND_SHORTCUT_MANAGER_V3_ACTION_MINIMIZE},
        // Defined by v3, but not implemented by Treeland's action executor yet.
        {TriggerActionId::ResizeWindow, "beginResize", -1},
        {TriggerActionId::ZoomIn, "viewZoomIn", -1},
        {TriggerActionId::ZoomOut, "viewZoomOut", -1},
        {TriggerActionId::ZoomReset, "viewActualSize", -1},
        {TriggerActionId::MoveToLeftWorkspace, "moveToWorkspaceLeft",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_MOVE_WINDOW_TO_PREV_WORKSPACE},
        {TriggerActionId::MoveToRightWorkspace, "moveToWorkspaceRight",
         TREELAND_SHORTCUT_MANAGER_V3_ACTION_MOVE_WINDOW_TO_NEXT_WORKSPACE},
    };

    for (const ActionBackendMapping &mapping : mappings) {
        if (mapping.id == actionId)
            return &mapping;
    }
    return nullptr;
}

} // namespace

TriggerActionId TriggerActionCatalog::resolve(const QString &value)
{
    bool ok = false;
    const int numericValue = value.trimmed().toInt(&ok);
    if (!ok)
        return TriggerActionId::Invalid;

    const auto actionId = static_cast<TriggerActionId>(numericValue);
    return mappingFor(actionId) ? actionId : TriggerActionId::Invalid;
}

QString TriggerActionCatalog::idString(TriggerActionId actionId)
{
    return QString::number(static_cast<int>(actionId));
}

QString TriggerActionCatalog::x11WmShortcutId(TriggerActionId actionId)
{
    const ActionBackendMapping *mapping = mappingFor(actionId);
    return mapping && mapping->x11WmId ? QString::fromLatin1(mapping->x11WmId) : QString();
}

std::optional<int> TriggerActionCatalog::treelandActionId(TriggerActionId actionId)
{
    const ActionBackendMapping *mapping = mappingFor(actionId);
    if (!mapping || mapping->treelandId < 0)
        return std::nullopt;
    return mapping->treelandId;
}
