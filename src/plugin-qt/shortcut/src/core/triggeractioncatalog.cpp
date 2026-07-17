// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "triggeractioncatalog.h"

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
        {TriggerActionId::Disable, nullptr, 0},
        {TriggerActionId::Notify, nullptr, 1},
        {TriggerActionId::Workspace1, "switchToWorkspace1", 2},
        {TriggerActionId::Workspace2, "switchToWorkspace2", 3},
        {TriggerActionId::Workspace3, "switchToWorkspace3", 4},
        {TriggerActionId::Workspace4, "switchToWorkspace4", 5},
        {TriggerActionId::Workspace5, "switchToWorkspace5", 6},
        {TriggerActionId::Workspace6, "switchToWorkspace6", 7},
        {TriggerActionId::PreviousWorkspace, "switchToWorkspaceLeft", 8},
        {TriggerActionId::NextWorkspace, "switchToWorkspaceRight", 9},
        {TriggerActionId::ShowDesktop, "showDesktop", 10},
        {TriggerActionId::MaximizeWindow, "maximize", 11},
        {TriggerActionId::RestoreWindow, "unmaximize", 12},
        {TriggerActionId::MoveWindow, "beginMove", 13},
        {TriggerActionId::CloseWindow, "close", 14},
        {TriggerActionId::ShowWindowMenu, "activateWindowMenu", 15},
        {TriggerActionId::ShowMultitask, nullptr, 16},
        {TriggerActionId::HideMultitask, nullptr, 17},
        {TriggerActionId::ToggleMultitask, "previewWorkspace", 18},
        {TriggerActionId::ToggleFpsDisplay, nullptr, 19},
        {TriggerActionId::LockScreen, nullptr, 20},
        {TriggerActionId::ShutdownMenu, nullptr, 21},
        {TriggerActionId::Quit, nullptr, 22},
        {TriggerActionId::TaskSwitchEnter, nullptr, 23},
        {TriggerActionId::TaskSwitchNext, "switchApplications", 24},
        {TriggerActionId::TaskSwitchPrevious, "switchApplicationsBackward", 25},
        {TriggerActionId::TaskSwitchSameAppNext, "switchGroup", 26},
        {TriggerActionId::TaskSwitchSameAppPrevious, "switchGroupBackward", 27},
        {TriggerActionId::SplitWindowLeft, "toggleToLeft", -1},
        {TriggerActionId::SplitWindowRight, "toggleToRight", -1},
        {TriggerActionId::HideDesktop, nullptr, -1},
        {TriggerActionId::ToggleGrandSearch, nullptr, -1},
        {TriggerActionId::ToggleLauncher, nullptr, -1},
        {TriggerActionId::ToggleClipboard, nullptr, -1},
        {TriggerActionId::ToggleNotifications, nullptr, -1},
        {TriggerActionId::MinimizeWindow, "minimize", -1},
        {TriggerActionId::ResizeWindow, "beginResize", -1},
        {TriggerActionId::ZoomIn, "viewZoomIn", -1},
        {TriggerActionId::ZoomOut, "viewZoomOut", -1},
        {TriggerActionId::ZoomReset, "viewActualSize", -1},
        {TriggerActionId::MoveToLeftWorkspace, "moveToWorkspaceLeft", -1},
        {TriggerActionId::MoveToRightWorkspace, "moveToWorkspaceRight", -1},
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
