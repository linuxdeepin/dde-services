// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "waylandgesturehandler.h"
#include "core/shortcutconfig.h"
#include "treelandshortcutwrapper.h"

#include <QDebug>

WaylandGestureHandler::WaylandGestureHandler(TreelandShortcutWrapper *wrapper, QObject *parent)
    : AbstractGestureHandler(parent)
    , m_wrapper(wrapper)
{
    connect(m_wrapper, &TreelandShortcutWrapper::activated, this, &WaylandGestureHandler::onActivated);
    connect(m_wrapper, &TreelandShortcutWrapper::protocolInactive, this, [this]() {
        qWarning() << "WaylandGestureHandler: Protocol inactive, clearing all bindings";
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

    // use id as name
    QString name = config.getId();
    int action = (int)QtWayland::treeland_shortcut_manager_v2::action_notify; // notify
    
    if (config.triggerType == (int)TriggerType::Action && !config.triggerValue.isEmpty()) {
         bool ok;
         int val = config.triggerValue.first().toInt(&ok);
         if (ok) action = val;
    }

    bool success = false;
    if (config.gestureType == (int)GestureType::Swipe) { // Swipe
        success = m_wrapper->bindSwipeGesture(name, config.fingerCount, config.direction, action);
    } else if (config.gestureType == (int)GestureType::Hold) { // Hold
        success = m_wrapper->bindHoldGesture(name, config.fingerCount, action);
    }

    if (success) {
        m_bindings.append(name);
    } else {
        qWarning() << "Failed to bind gesture for" << config.getId();
    }

    return success;
}

bool WaylandGestureHandler::unregisterGesture(const QString &id)
{
    if (!m_bindings.contains(id)) return false;

    m_bindings.removeAll(id);
    m_wrapper->unbind(id);

    return true;
}

bool WaylandGestureHandler::commit()
{
    bool success = m_wrapper->commitAndWait();
    if (!success) {
        qWarning() << "WaylandGestureHandler::commit failed";
    }
    
    return success;
}

void WaylandGestureHandler::onActivated(const QString &name, uint32_t flags)
{
    Q_UNUSED(flags);
    // Forward the activation signal to GestureHandler
    if (!m_bindings.contains(name)) return;

    qDebug() << "WaylandGestureHandler::onActivated name:" << name << "flags:" << flags;
    emit activated(name);
}
