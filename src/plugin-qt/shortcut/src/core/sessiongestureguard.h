// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantMap>

#include <cstdint>

class QDBusPendingCallWatcher;
class QTimer;
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_window_t;

class SessionGestureGuard : public QObject
{
    Q_OBJECT
public:
    explicit SessionGestureGuard(QObject *parent = nullptr);
    ~SessionGestureGuard() override;

    bool canHandleTouchpadEvent() const;
    bool canHandleTouchpadGesture(const QString &gestureName) const;
    bool canBeginWindowMove() const;

private slots:
    void onSessionManagerPropertiesChanged(const QString &interface,
                                           const QVariantMap &changed,
                                           const QStringList &invalidated);
    void onTouchpadPropertiesChanged(const QString &interface,
                                     const QVariantMap &changed,
                                     const QStringList &invalidated);
    void refreshMultitaskState();
    void onMultitaskStateFinished(QDBusPendingCallWatcher *watcher);

private:
    void refreshSessionManagerState();
    void refreshTouchpadState();
    void refreshLoginSessionState();
    void updateCurrentSession(const QString &path);
    void handleLoginSessionPropertiesChanged(const QString &path, quint64 generation,
                                             const QString &interface,
                                             const QVariantMap &changed,
                                             const QStringList &invalidated);
    void refreshWmOwner();
    void setWmOwner(const QString &owner);
    bool isKeyboardGrabbed() const;

    QString m_currentSessionPath;
    quint64 m_sessionGeneration = 0;
    bool m_locked = true;
    bool m_sessionActive = false;
    bool m_touchpadEnabled = false;
    bool m_multitaskVisible = false;
    QString m_wmOwner;
    quint64 m_wmGeneration = 0;
    quint64 m_multitaskPendingGeneration = 0;
    QObject *m_loginSessionSignalRelay = nullptr;
    QTimer *m_multitaskRefreshTimer = nullptr;
    xcb_connection_t *m_xConnection = nullptr;
    xcb_window_t m_rootWindow = 0;
};
