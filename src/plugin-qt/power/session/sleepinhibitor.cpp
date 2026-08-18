// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "sleepinhibitor.h"
#include "../powerconstants.h"

#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusConnection>
#include <unistd.h>
#include <cerrno>
#include <cstring>

using namespace PowerDBus;

SleepInhibitor::SleepInhibitor(QObject *parent)
    : QObject(parent)
{
    inhibit();
    QDBusConnection::systemBus().connect(
        kDaemonService, kDaemonPath, kDaemonService, "HandleForSleep",
        this, SLOT(handleSleep(bool)));
    QDBusConnection::systemBus().connect(
        kFreedesktopDBus, kFreedesktopPath, kFreedesktopDBus, "NameOwnerChanged",
        this, SLOT(onNameOwnerChanged(QString, QString, QString)));
}

SleepInhibitor::~SleepInhibitor()
{
    unblock();
}

void SleepInhibitor::handleSleep(bool beforeSleep)
{
    if (beforeSleep) {
        unblock();
        Q_EMIT aboutToSleep();
    } else {
        Q_EMIT wokeUp();
        block();
    }
}

void SleepInhibitor::onNameOwnerChanged(const QString &name,
                                         const QString &,
                                         const QString &newOwner)
{
    if (name == QLatin1String(kLogin1Service) && !newOwner.isEmpty())
        inhibit();
}

void SleepInhibitor::inhibit()
{
    if (m_fd >= 0)
        return;
    QDBusInterface iface(kLogin1Service, kLogin1Path, kLogin1Manager,
                          QDBusConnection::systemBus());
    QDBusReply<QDBusUnixFileDescriptor> reply =
        iface.call("Inhibit", "sleep", kService, "run screen lock", "delay");
    if (reply.isValid()) {
        m_fd = dup(reply.value().fileDescriptor());
        if (m_fd < 0)
            qWarning("[SleepInhibitor] dup failed: %s", strerror(errno));
    }
}

void SleepInhibitor::block()
{
    inhibit();
}

void SleepInhibitor::unblock()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}
