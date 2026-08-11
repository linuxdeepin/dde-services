// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantMap>

class SessionActiveMonitor : public QObject
{
    Q_OBJECT
public:
    explicit SessionActiveMonitor(QObject *parent = nullptr);
    ~SessionActiveMonitor() override;

    bool isActive() const;
    bool isLocked() const;

signals:
    void activeChanged(bool active);

private slots:
    void onSessionManagerPropertiesChanged(const QString &interface,
                                           const QVariantMap &changed,
                                           const QStringList &invalidated);

private:
    void refreshSessionManagerState();
    void refreshLoginSessionState();
    void updateCurrentSession(const QString &path);
    void handleLoginSessionPropertiesChanged(const QString &path, quint64 generation,
                                             const QString &interface,
                                             const QVariantMap &changed,
                                             const QStringList &invalidated);
    void setActive(bool active);
    void setLocked(bool locked);
    void disconnectLoginSessionSignal();

    QString m_currentSessionPath;
    quint64 m_sessionGeneration = 0;
    bool m_active = false;
    bool m_locked = true;
    QObject *m_loginSessionSignalRelay = nullptr;
};
