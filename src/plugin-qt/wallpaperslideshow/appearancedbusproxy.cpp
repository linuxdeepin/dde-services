// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "appearancedbusproxy.h"

#include <QDBusPendingReply>

const QString DaemonService = QStringLiteral("org.deepin.dde.Daemon1");
const QString DaemonPath = QStringLiteral("/org/deepin/dde/Daemon1");
const QString DaemonInterface = QStringLiteral("org.deepin.dde.Daemon1");

const QString AppearanceService = QStringLiteral("org.deepin.dde.Appearance1");
const QString AppearancePath = QStringLiteral("/org/deepin/dde/Appearance1");
const QString AppearanceInterface = QStringLiteral("org.deepin.dde.Appearance1");


AppearanceDBusProxy::AppearanceDBusProxy(QObject *parent)
    : QObject(parent)
{
    const QString sessionType = qEnvironmentVariable("XDG_SESSION_TYPE");
    if (sessionType == "wayland") {
        m_wmInterface = nullptr;
    } else {
        m_wmInterface = new DDBusInterface("com.deepin.wm", "/com/deepin/wm", "com.deepin.wm", QDBusConnection::sessionBus(), this);
    }
    m_appearanceInterface = new DDBusInterface(AppearanceService, AppearancePath, AppearanceInterface, QDBusConnection::sessionBus(), this);
    QDBusConnection::systemBus().connect(DaemonService, DaemonPath, DaemonInterface, "HandleForSleep", this, SIGNAL(HandleForSleep(bool)));
    QDBusConnection::sessionBus().connect(QStringLiteral("org.deepin.dde.Timedate1"), QStringLiteral("/org/deepin/dde/Timedate1"), QStringLiteral("org.deepin.dde.Timedate1"), "TimeUpdate", this, SIGNAL(TimeUpdate()));
}

int AppearanceDBusProxy::GetCurrentWorkspace()
{
    if (m_wmInterface == nullptr) {
        return 0;
    }
    return QDBusPendingReply<int>(m_wmInterface->asyncCall(QStringLiteral("GetCurrentWorkspace")));
}

QStringList AppearanceDBusProxy::GetCustomWallPapers(const QString &username)
{
    QDBusMessage daemonMessage = QDBusMessage::createMethodCall(DaemonService, DaemonPath, DaemonInterface, "GetCustomWallPapers");
    daemonMessage << username;
    return QDBusPendingReply<QStringList>(QDBusConnection::systemBus().asyncCall(daemonMessage));
}

void AppearanceDBusProxy::SetCurrentWorkspaceBackgroundForMonitor(const QString &url, const QString &screenName)
{
    m_appearanceInterface->asyncCall(QStringLiteral("SetCurrentWorkspaceBackgroundForMonitor"), url, screenName);
}

QString AppearanceDBusProxy::getCurrentWorkspaceBackground()
{
    return QDBusPendingReply<QString>(m_appearanceInterface->asyncCall(QStringLiteral("GetCurrentWorkspaceBackground")));
}

QString AppearanceDBusProxy::getCurrentWorkspaceBackgroundForMonitor(const QString &monitor)
{
    return QDBusPendingReply<QString>(m_appearanceInterface->asyncCall(QStringLiteral("GetCurrentWorkspaceBackgroundForMonitor"), QVariant::fromValue(monitor)));
}

void AppearanceDBusProxy::SetGreeterBackground(const QString &url)
{
    m_appearanceInterface->asyncCall(QStringLiteral("Set"), QStringLiteral("greeterbackground"), QVariant::fromValue(url));
}
