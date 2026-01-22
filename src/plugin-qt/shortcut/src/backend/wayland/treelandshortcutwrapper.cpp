// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "treelandshortcutwrapper.h"

#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <qlogging.h>

TreelandShortcutWrapper::TreelandShortcutWrapper(QObject *parent)
    : QWaylandClientExtensionTemplate<TreelandShortcutWrapper>(2) // version 2
    , QtWayland::treeland_shortcut_manager_v2()
{
    setParent(parent);

    connect(this, &TreelandShortcutWrapper::activeChanged, this, [this]() {
        if (isActive()) {
            qDebug() << "TreelandShortcutManager protocol is now active, acquiring...";
            acquire();
            m_boundObject = object();  // Save bound object for session recovery check
            emit ready();  // Emit signal directly since signal is connected before init() is called
        } else {
            qWarning() << "TreelandShortcutManager protocol is now inactive!";
            m_boundObject = nullptr;
            emit protocolInactive();
        }
    });

    // Don't call initialize() in constructor; let external code call init() to trigger it
}

void TreelandShortcutWrapper::initProtocol()
{
    initialize();
}

TreelandShortcutWrapper::~TreelandShortcutWrapper()
{
    if (isActive()) {
        destroy();
    }
}

bool TreelandShortcutWrapper::bindKey(const QString &name, const QString &key, uint32_t flags, int action)
{
    if (!isActive()) return false;
    bind_key(name, key, flags, action);
    return true;
}

bool TreelandShortcutWrapper::bindSwipeGesture(const QString &name, int finger, int direction, int action)
{
    if (!isActive()) return false;
    bind_swipe_gesture(name, finger, direction, action);
    return true;
}

bool TreelandShortcutWrapper::bindHoldGesture(const QString &name, int finger, int action)
{
    if (!isActive()) return false;
    bind_hold_gesture(name, finger, action);
    return true;
}

bool TreelandShortcutWrapper::unbind(const QString &name)
{
    if (!isActive()) return false;

    // Check if object changed (session recovery scenario)
    if (object() != m_boundObject) {
        qWarning() << "TreelandShortcutWrapper::unbind: object changed, skip unbind for" << name;
        return false;
    }

    QtWayland::treeland_shortcut_manager_v2::unbind(name);
    return true;
}

void TreelandShortcutWrapper::commit()
{
    if (!isActive()) return;
    QtWayland::treeland_shortcut_manager_v2::commit();
}

bool TreelandShortcutWrapper::commitAndWait(int timeoutMs)
{
    if (!isActive()) return false;

    QEventLoop loop;
    bool success = false;
    bool responded = false;

    auto conn = connect(this, &TreelandShortcutWrapper::commitStatus, &loop, [&](bool status) {
        loop.quit();
        success = status;
        responded = true;
        qDebug() << "TreelandShortcutWrapper::commitAndWait response:" << status;
    }, Qt::SingleShotConnection);

    QTimer::singleShot(timeoutMs, &loop, [&]() {
        if (!responded) {
            qWarning() << "TreelandShortcutWrapper::commitAndWait timeout after" << timeoutMs << "ms";
            loop.quit();
        }
    });

    QtWayland::treeland_shortcut_manager_v2::commit();
    loop.exec();

    //disconnect(conn);
    return success;
}

void TreelandShortcutWrapper::treeland_shortcut_manager_v2_activated(const QString &name, uint32_t flags)
{
    qDebug() << "Treeland Shortcut Manager Activated:" << name << "flags:" << flags;
    emit activated(name, flags);
}

void TreelandShortcutWrapper::treeland_shortcut_manager_v2_commit_success()
{
    emit commitStatus(true);
}

void TreelandShortcutWrapper::treeland_shortcut_manager_v2_commit_failure(const QString &name, uint32_t error)
{
    qWarning() << "Treeland Shortcut Manager Commit Failed:" << name << "Error:" << error;
    emit commitStatus(false);
}
