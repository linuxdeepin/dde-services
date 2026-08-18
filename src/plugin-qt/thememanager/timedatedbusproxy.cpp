// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "timedatedbusproxy.h"

#include <QDBusPendingReply>

TimeDateDBusProxy::TimeDateDBusProxy(QObject *parent)
    : QObject(parent)
    , m_timeDateInterface(new DDBusInterface("org.freedesktop.timedate1", 
                                             "/org/freedesktop/timedate1", 
                                             "org.freedesktop.timedate1", 
                                             QDBusConnection::systemBus(), 
                                             this))
{
    QDBusConnection::sessionBus().connect(QStringLiteral("org.deepin.dde.Timedate1"), 
                                          QStringLiteral("/org/deepin/dde/Timedate1"), 
                                          QStringLiteral("org.deepin.dde.Timedate1"), 
                                          "TimeUpdate", 
                                          this, 
                                          SIGNAL(TimeUpdate()));
}

QString TimeDateDBusProxy::timezone()
{
    return qvariant_cast<QString>(m_timeDateInterface->property("Timezone"));
}

bool TimeDateDBusProxy::nTP()
{
    return qvariant_cast<bool>(m_timeDateInterface->property("NTP"));
}
