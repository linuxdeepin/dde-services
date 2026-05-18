// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>

class PowerManager;
class QTimer;

class LidSwitchHandler : public QObject {
    Q_OBJECT
public:
    explicit LidSwitchHandler(PowerManager *manager, QObject *parent = nullptr);

private Q_SLOTS:
    void onLidClosed();
    void onLidOpened();

private:
    void doLidStateChanged(bool opened);
    QTimer *m_debounce = nullptr;
    bool m_pendingOpen = true;
    PowerManager *m_manager = nullptr;
};
