// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef TIMEDATEDBUSPROXY_H
#define TIMEDATEDBUSPROXY_H

#include <DDBusInterface>

using Dtk::Core::DDBusInterface;

class TimeDateDBusProxy : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString Timezone READ timezone NOTIFY TimezoneChanged)
    Q_PROPERTY(bool NTP READ nTP NOTIFY NTPChanged)

public:
    explicit TimeDateDBusProxy(QObject *parent = nullptr);

    QString timezone();
    bool nTP();

Q_SIGNALS:
    void TimezoneChanged(QString timezone);
    void NTPChanged(bool NTP);
    void TimeUpdate();

private:
    DDBusInterface *m_timeDateInterface;
};

#endif // TIMEDATEDBUSPROXY_H
