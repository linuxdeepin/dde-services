// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointer>
#include <QMetaObject>

#include <functional>

class QWindow;
class TreelandShortcutCaptureManager;
class TreelandShortcutCaptureSession;
class X11ShortcutCapture;

class ShortcutCaptureController : public QObject
{
    Q_OBJECT

public:
    explicit ShortcutCaptureController(QObject *parent = nullptr);
    ~ShortcutCaptureController() override;

    bool capture(QObject *owner,
                 QWindow *window,
                 std::function<void(const QString &)> completed,
                 std::function<void(const QString &)> failed);
    void cancel(QObject *owner = nullptr);
    bool isActive() const { return m_active; }

private:
    static QString normalizeCapturedShortcut(const QString &shortcut);
    void finishSuccess(const QString &shortcut);
    void finishFailure(const QString &reason);
    void clearActiveCapture();

    X11ShortcutCapture *m_x11Capture = nullptr;
    TreelandShortcutCaptureManager *m_waylandCapture = nullptr;
    QPointer<TreelandShortcutCaptureSession> m_waylandSession;
    QPointer<QObject> m_owner;
    QMetaObject::Connection m_ownerDestroyedConnection;
    std::function<void(const QString &)> m_completed;
    std::function<void(const QString &)> m_failed;
    bool m_active = false;
};
