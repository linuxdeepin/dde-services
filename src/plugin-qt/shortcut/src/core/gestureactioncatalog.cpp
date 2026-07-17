// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "gestureactioncatalog.h"

#include <QCoreApplication>

namespace {

constexpr auto UnsupportedGestureActionReason = QT_TRANSLATE_NOOP(
        "org.deepin.dde.keybinding",
        "This gesture action is not currently supported");

struct X11GestureActionRule {
    GestureActionId id;
    QList<int> gestureTypes;
    QList<int> fingerCounts;
};

const QList<X11GestureActionRule> &x11ActionRules()
{
    static const QList<X11GestureActionRule> rules = {
        {GestureActionId::MaximizeWindow, {int(GestureType::Swipe)}, {3}},
        {GestureActionId::RestoreWindow, {int(GestureType::Swipe)}, {3}},
        {GestureActionId::SplitWindowLeft, {int(GestureType::Swipe)}, {3}},
        {GestureActionId::SplitWindowRight, {int(GestureType::Swipe)}, {3}},
        {GestureActionId::ShowMultitask, {int(GestureType::Swipe)}, {4}},
        {GestureActionId::HideMultitask, {int(GestureType::Swipe)}, {4}},
        {GestureActionId::PreviousWorkspace, {int(GestureType::Swipe)}, {4}},
        {GestureActionId::NextWorkspace, {int(GestureType::Swipe)}, {4}},
        {GestureActionId::ShowDesktop, {int(GestureType::Swipe)}, {4}},
        {GestureActionId::HideDesktop, {int(GestureType::Swipe)}, {4}},
        {GestureActionId::ToggleGrandSearch, {int(GestureType::Hold)}, {3, 4}},
        {GestureActionId::ToggleLauncher, {int(GestureType::Hold)}, {3, 4}},
        {GestureActionId::ToggleClipboard, {int(GestureType::Hold)}, {3, 4}},
        {GestureActionId::ToggleNotifications, {int(GestureType::Hold)}, {3, 4}},
        {GestureActionId::Disable, {int(GestureType::Swipe), int(GestureType::Hold)}, {3, 4}},
    };
    return rules;
}

bool appliesTo(const X11GestureActionRule &rule, const GestureConfig &config)
{
    return rule.gestureTypes.contains(config.gestureType)
            && rule.fingerCounts.contains(config.fingerCount);
}

QList<GestureActionId> x11ActionIds(const GestureConfig &config)
{
    QList<GestureActionId> result;
    for (const X11GestureActionRule &rule : x11ActionRules()) {
        if (appliesTo(rule, config))
            result.append(rule.id);
    }
    return result;
}

QList<GestureActionId> treelandActionIds(const GestureConfig &config)
{
    static const QList<GestureActionId> ThreeFingerActions = {
        GestureActionId::MaximizeWindow,
        GestureActionId::RestoreWindow,
        GestureActionId::Disable,
    };
    static const QList<GestureActionId> FourFingerActions = {
        GestureActionId::ShowMultitask,
        GestureActionId::HideMultitask,
        GestureActionId::PreviousWorkspace,
        GestureActionId::NextWorkspace,
        GestureActionId::ShowDesktop,
        GestureActionId::HideDesktop,
        GestureActionId::Disable,
    };
    static const QList<GestureActionId> ServiceActions = {
        GestureActionId::ToggleGrandSearch,
        GestureActionId::ToggleLauncher,
        GestureActionId::ToggleClipboard,
        GestureActionId::ToggleNotifications,
    };

    if (config.gestureType == int(GestureType::Hold)) {
        if (config.fingerCount == 3 || config.fingerCount == 4) {
            QList<GestureActionId> result = ServiceActions;
            result.append(GestureActionId::Disable);
            return result;
        }
        return {};
    }

    QList<GestureActionId> result;
    if (config.fingerCount == 3)
        result = ThreeFingerActions;
    else if (config.fingerCount == 4)
        result = FourFingerActions;

    // TODO: expose SplitWindowLeft and SplitWindowRight after Treeland
    // provides native compositor actions for them. Until then they must
    // remain unregistered and absent from the control-center choices.
    return result;
}

QList<GestureActionId> actionIdsFor(const GestureConfig &config, GestureBackend backend)
{
    return backend == GestureBackend::Treeland ? treelandActionIds(config)
                                                : x11ActionIds(config);
}

const GestureActionMetadata *findMetadata(GestureActionId actionId)
{
    for (const GestureActionMetadata &action : GestureActionCatalog::metadata()) {
        if (action.id == actionId)
            return &action;
    }
    return nullptr;
}

}

const QList<GestureActionMetadata> &GestureActionCatalog::metadata()
{
    static const QList<GestureActionMetadata> catalog = {
        {GestureActionId::Notify, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Notify")},
        {GestureActionId::Workspace1, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Switch to workspace 1")},
        {GestureActionId::Workspace2, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Switch to workspace 2")},
        {GestureActionId::Workspace3, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Switch to workspace 3")},
        {GestureActionId::Workspace4, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Switch to workspace 4")},
        {GestureActionId::Workspace5, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Switch to workspace 5")},
        {GestureActionId::Workspace6, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Switch to workspace 6")},
        {GestureActionId::PreviousWorkspace, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Switch to previous workspace")},
        {GestureActionId::NextWorkspace, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Switch to next workspace")},
        {GestureActionId::ShowDesktop, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Show desktop")},
        {GestureActionId::MaximizeWindow, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Maximize window")},
        {GestureActionId::RestoreWindow, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Restore window")},
        {GestureActionId::MoveWindow, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Move window")},
        {GestureActionId::CloseWindow, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Close window")},
        {GestureActionId::ShowWindowMenu, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Show window menu")},
        {GestureActionId::ShowMultitask, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Show multitasking view")},
        {GestureActionId::HideMultitask, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Hide multitasking view")},
        {GestureActionId::ToggleMultitask, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Toggle multitasking view")},
        {GestureActionId::ToggleFpsDisplay, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Toggle FPS display")},
        {GestureActionId::LockScreen, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Lock screen")},
        {GestureActionId::ShutdownMenu, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Show shutdown menu")},
        {GestureActionId::Quit, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Quit")},
        {GestureActionId::TaskSwitchEnter, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Enter task switcher")},
        {GestureActionId::TaskSwitchNext, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Select next task")},
        {GestureActionId::TaskSwitchPrevious, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Select previous task")},
        {GestureActionId::TaskSwitchSameAppNext, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Select next window of current application")},
        {GestureActionId::TaskSwitchSameAppPrevious, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Select previous window of current application")},
        {GestureActionId::SplitWindowLeft, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Current window left split")},
        {GestureActionId::SplitWindowRight, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Current window right split")},
        {GestureActionId::HideDesktop, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Hide desktop")},
        {GestureActionId::ToggleGrandSearch, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Show/hide grand search")},
        {GestureActionId::ToggleLauncher, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Show/hide launcher")},
        {GestureActionId::ToggleClipboard, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Show/hide clipboard")},
        {GestureActionId::ToggleNotifications, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Show/hide notification center")},
        {GestureActionId::Disable, QT_TRANSLATE_NOOP("org.deepin.dde.keybinding", "Disable")},
    };
    return catalog;
}

QList<GestureActionMetadata> GestureActionCatalog::actionsFor(const GestureConfig &config)
{
    return actionsFor(config, GestureBackend::X11);
}

QList<GestureActionMetadata> GestureActionCatalog::actionsFor(const GestureConfig &config, GestureBackend backend)
{
    QList<GestureActionMetadata> result;
    for (GestureActionId actionId : actionIdsFor(config, backend)) {
        const GestureActionMetadata *action = findMetadata(actionId);
        Q_ASSERT_X(action, "GestureActionCatalog::actionsFor",
                   "backend action is missing metadata");
        if (action)
            result.append(*action);
    }
    return result;
}

GestureActionId GestureActionCatalog::resolveActionId(const GestureConfig &config, const QString &value)
{
    return resolveActionId(config, value, GestureBackend::X11);
}

GestureActionId GestureActionCatalog::resolveActionId(const GestureConfig &config,
                                                      const QString &value,
                                                      GestureBackend backend)
{
    const GestureActionId actionId = resolveKnownActionId(value);
    return find(config, actionId, backend) ? actionId : GestureActionId::Invalid;
}

GestureActionId GestureActionCatalog::resolveKnownActionId(const QString &value)
{
    const GestureActionId actionId = TriggerActionCatalog::resolve(value);
    return findMetadata(actionId) ? actionId : GestureActionId::Invalid;
}

const GestureActionMetadata *GestureActionCatalog::find(const GestureConfig &config, GestureActionId actionId)
{
    return find(config, actionId, GestureBackend::X11);
}

const GestureActionMetadata *GestureActionCatalog::find(const GestureConfig &config, GestureActionId actionId, GestureBackend backend)
{
    if (!actionIdsFor(config, backend).contains(actionId))
        return nullptr;
    return findMetadata(actionId);
}

GestureActionTarget GestureActionCatalog::targetFor(GestureActionId actionId, GestureBackend backend)
{
    switch (actionId) {
    case GestureActionId::ToggleGrandSearch:
    case GestureActionId::ToggleLauncher:
    case GestureActionId::ToggleClipboard:
    case GestureActionId::ToggleNotifications:
        return GestureActionTarget::Service;
    case GestureActionId::LockScreen:
        return backend == GestureBackend::X11 ? GestureActionTarget::Service
                                              : GestureActionTarget::Backend;
    default:
        return GestureActionTarget::Backend;
    }
}

GestureActionId GestureActionCatalog::registrationActionId(GestureActionId actionId, GestureBackend backend)
{
    if (backend == GestureBackend::Treeland) {
        if (targetFor(actionId, backend) == GestureActionTarget::Service)
            return GestureActionId::Notify;
        // Treeland's show_desktop action toggles the desktop state, matching
        // the legacy ShowDesktop/HideDesktop gesture pair.
        if (actionId == GestureActionId::HideDesktop)
            return GestureActionId::ShowDesktop;
    }
    return actionId;
}

QString GestureActionCatalog::unsupportedReasonSource()
{
    return QLatin1String(UnsupportedGestureActionReason);
}

QString GestureActionCatalog::displayNameSource(const GestureActionMetadata &action)
{
    return QString::fromUtf8(action.displayName);
}

QString GestureActionCatalog::displayNameSource(GestureActionId actionId)
{
    const GestureActionMetadata *action = findMetadata(actionId);
    return action ? displayNameSource(*action) : QString();
}

QString GestureActionCatalog::idString(GestureActionId actionId)
{
    return TriggerActionCatalog::idString(actionId);
}
