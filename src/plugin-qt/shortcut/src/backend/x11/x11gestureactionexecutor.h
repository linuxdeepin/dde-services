// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/triggeractioncatalog.h"
#include "core/shortcutconfig.h"

#include <QObject>

#include <xcb/xcb.h>

class X11GestureActionExecutor : public QObject
{
    Q_OBJECT
public:
    explicit X11GestureActionExecutor(QObject *parent = nullptr);
    ~X11GestureActionExecutor() override;

    bool execute(const GestureConfig &config);
    bool execute(const KeyConfig &config);
    bool beginWindowMove();
    bool updateWindowMove(double accelX, double accelY);
    bool endWindowMove();

private:
    enum class WindowMoveState {
        Idle,
        Starting,
        Active,
        Ending,
    };

    bool callWindowManager(const QString &method, const QVariantList &arguments = {},
                           const QString &context = QString());
    bool setMultitaskVisible(bool visible, const QString &actionId);
    void updateMultitaskVisible();
    bool setDesktopVisible(bool visible);
    bool call(const QString &service, const QString &path, const QString &interface,
              const QString &method, const QVariantList &arguments = {},
              const QString &context = QString());
    bool sendWindowMoveUpdate();
    bool clearWindowMove();
    bool executeAction(GestureActionId actionId, const QString &context);

    xcb_connection_t *m_connection = nullptr;
    xcb_window_t m_rootWindow = XCB_WINDOW_NONE;
    xcb_atom_t m_showingDesktopAtom = XCB_ATOM_NONE;
    int m_windowMoveX = 0;
    int m_windowMoveY = 0;
    quint64 m_windowMoveGeneration = 0;
    WindowMoveState m_windowMoveState = WindowMoveState::Idle;
    bool m_windowMoveUpdatePending = false;
    bool m_windowMoveUpdateQueued = false;
    bool m_multitaskUpdatePending = false;
    bool m_multitaskTargetVisible = false;
    quint64 m_multitaskTargetGeneration = 0;
    QString m_multitaskActionId;
};
