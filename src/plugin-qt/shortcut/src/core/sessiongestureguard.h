// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantMap>
#include <cstdint>

class QDBusPendingCallWatcher;
class QTimer;
class SessionActiveMonitor;
typedef struct xcb_connection_t xcb_connection_t;
typedef uint32_t xcb_window_t;

class SessionGestureGuard : public QObject
{
    Q_OBJECT
public:
    explicit SessionGestureGuard(SessionActiveMonitor *sessionMonitor,
                                 QObject *parent = nullptr);
    ~SessionGestureGuard() override;

    bool canHandleTouchpadEvent() const;
    bool canHandleTouchpadGesture(const QString &gestureName) const;
    bool canBeginWindowMove() const;

private slots:
    void onTouchpadPropertiesChanged(const QString &interface,
                                     const QVariantMap &changed,
                                     const QStringList &invalidated);
    void refreshMultitaskState();
    void onMultitaskStateFinished(QDBusPendingCallWatcher *watcher);

private:
    void refreshTouchpadState();
    void refreshWmOwner();
    void setWmOwner(const QString &owner);
    bool isKeyboardGrabbed() const;

    SessionActiveMonitor *m_sessionMonitor = nullptr;
    bool m_touchpadEnabled = false;
    bool m_multitaskVisible = false;
    QString m_wmOwner;
    quint64 m_wmGeneration = 0;
    quint64 m_multitaskPendingGeneration = 0;
    QTimer *m_multitaskRefreshTimer = nullptr;
    xcb_connection_t *m_xConnection = nullptr;
    xcb_window_t m_rootWindow = 0;
};
