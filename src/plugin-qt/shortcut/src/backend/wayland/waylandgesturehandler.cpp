// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "shortcutlogging.h"

#include "waylandgesturehandler.h"
#include "core/shortcutconfig.h"
#include "core/triggeractioncatalog.h"
#include "treelandshortcutwrapper.h"

#include <QDebug>

WaylandGestureHandler::WaylandGestureHandler(TreelandShortcutWrapper *wrapper, QObject *parent)
    : AbstractGestureHandler(parent)
    , m_wrapper(wrapper)
{
    connect(m_wrapper, &TreelandShortcutWrapper::activated, this, &WaylandGestureHandler::onActivated);
    connect(m_wrapper, &TreelandShortcutWrapper::bindFailed, this, &WaylandGestureHandler::onBindFailed);
    connect(m_wrapper, &TreelandShortcutWrapper::protocolInactive, this, [this]() {
        qCWarning(logShortcut) << "WaylandGestureHandler: Protocol inactive, clearing all bindings";
        m_bindings.clear();
    });
}

WaylandGestureHandler::~WaylandGestureHandler()
{
}

bool WaylandGestureHandler::registerGesture(const GestureConfig &config)
{
    if (!m_wrapper) return false;

    if (m_bindings.contains(config.getId())) {
        unregisterGesture(config.getId());
    }

    const QString name = config.getId() + "#" + QString::number(++m_bindingSerial);
    int action = QtWayland::treeland_shortcut_manager_v3::action_notify;

    if (config.triggerType == int(TriggerType::Action)) {
        const auto actionId = TriggerActionCatalog::resolve(config.triggerValue.value(0));
        const auto treelandAction = TriggerActionCatalog::treelandActionId(actionId);
        if (!treelandAction)
            return false;
        action = *treelandAction;
    }

    bool success = false;
    if (config.gestureType == (int)GestureType::Swipe) { // Swipe
        success = m_wrapper->bindSwipeGesture(name, config.fingerCount, config.direction, action);
    } else if (config.gestureType == (int)GestureType::Hold) { // Hold
        success = m_wrapper->bindHoldGesture(name, config.fingerCount, action);
    }

    if (success) {
        m_bindings.insert(config.getId(), name);
    } else {
        qCWarning(logShortcut) << "Failed to bind gesture for" << config.getId();
    }

    return success;
}

bool WaylandGestureHandler::unregisterGesture(const QString &id)
{
    if (!m_bindings.contains(id)) return false;

    m_wrapper->unbind(m_bindings.take(id));

    return true;
}

bool WaylandGestureHandler::commit()
{
    m_wrapper->flushDeferred();
    return true;
}

bool WaylandGestureHandler::commitSync()
{
    bool success = m_wrapper->syncAndWait();
    if (!success) {
        qCWarning(logShortcut) << "WaylandGestureHandler::commitSync failed";
    }

    return success;
}

void WaylandGestureHandler::onActivated(const QString &name, uint32_t flags)
{
    Q_UNUSED(flags);
    // Forward the activation signal to GestureHandler
    const QString id = m_bindings.key(name);
    if (id.isEmpty()) return;

    qCDebug(logShortcut) << "WaylandGestureHandler::onActivated name:" << name << "flags:" << flags;
    emit activated(id);
}

void WaylandGestureHandler::onBindFailed(const QString &name, uint32_t error)
{
    Q_UNUSED(error);
    const QString id = m_bindings.key(name);
    if (id.isEmpty())
        return;

    m_bindings.remove(id);
    emit registrationFailed(id);
}
