// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "systemdbusproxy.h"

#include <QDBusConnection>
#include <QDBusInterface>

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
