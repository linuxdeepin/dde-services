// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef APPEARANCEDBUSPROXY_H
#define APPEARANCEDBUSPROXY_H

#include <DDBusInterface>
#include <QSharedPointer>

using Dtk::Core::DDBusInterface;

class AppearanceDBusProxy : public QObject
{
    Q_OBJECT
public:
    explicit AppearanceDBusProxy(QObject *parent = nullptr);
    void SetCurrentWorkspaceBackgroundForMonitor(const QString &url, const QString &screenName);
    QString getCurrentWorkspaceBackground();
    QString getCurrentWorkspaceBackgroundForMonitor(const QString &monitor);
    void SetGreeterBackground(const QString &url);

public Q_SLOTS:
    int GetCurrentWorkspace();
    

Q_SIGNALS:
    void PrimaryChanged(const QString &Primary);
    void MonitorsChanged(QList<QDBusObjectPath> monitors);

Q_SIGNALS:
    void DesktopBackgroundsChanged(const QStringList &desktopBackgrounds);
    void GreeterBackgroundChanged(const QString &greeterBackground);

    // Daemon
public:
    static QStringList GetCustomWallPapers(const QString &username);

Q_SIGNALS:
    void HandleForSleep(bool sleep);
    void WallpaperURlsChanged(QString) const;

private:
    DDBusInterface *m_wmInterface;
    DDBusInterface *m_appearanceInterface;
};

#endif // APPEARANCEDBUSPROXY_H
