// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "systemdbusproxy.h"
#include "../powerconstants.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingReply>
#include <QDebug>

using namespace PowerDBus;

SystemDBusProxy::SystemDBusProxy(QObject *parent)
    : QObject(parent)
{
}

QString SystemDBusProxy::chassis() const
{
    QDBusInterface iface("org.freedesktop.hostname1",
                          "/org/freedesktop/hostname1",
                          "org.freedesktop.hostname1",
                          QDBusConnection::systemBus());
    return iface.property("Chassis").toString();
}

bool SystemDBusProxy::lidIsPresent() const
{
    QDBusInterface iface(kUPowerService, kUPowerPath, kUPowerService,
                          QDBusConnection::systemBus());
    return iface.property("LidIsPresent").toBool();
}

bool SystemDBusProxy::lidIsClosed() const
{
    QDBusInterface iface(kUPowerService, kUPowerPath, kUPowerService,
                          QDBusConnection::systemBus());
    return iface.property("LidIsClosed").toBool();
}
