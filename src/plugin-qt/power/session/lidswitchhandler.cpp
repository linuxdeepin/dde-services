// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "lidswitchhandler.h"
#include "idle/idlewatcher.h"
#include "powermanager.h"
#include "../powerconstants.h"

#include <QDBusConnection>
#include <QDebug>
#include <QLoggingCategory>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(logPowerSession)

using namespace PowerDBus;

LidSwitchHandler::LidSwitchHandler(PowerManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager)
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(1500);

    connect(m_debounce, &QTimer::timeout, this, [this]() {
        doLidStateChanged(m_pendingOpen);
    });

    auto bus = QDBusConnection::systemBus();
    bus.connect(kService, kPath, kInterface, "LidClosed",
                this, SLOT(onLidClosed()));
    bus.connect(kService, kPath, kInterface, "LidOpened",
                this, SLOT(onLidOpened()));
}

void LidSwitchHandler::onLidClosed()
{
    qDebug(logPowerSession) << "Lid closed";
    m_pendingOpen = false;
    m_debounce->start();
}

void LidSwitchHandler::onLidOpened()
{
    qDebug(logPowerSession) << "Lid opened";
    m_pendingOpen = true;
    m_debounce->start();
}

void LidSwitchHandler::doLidStateChanged(bool opened)
{
    if (!m_manager) return;

    if (!opened) {
        // 合盖
        m_manager->SetPrepareSuspend(PS_LidClose);
        bool onBattery = m_manager->onBattery();
        int32_t action = onBattery ? m_manager->batteryLidClosedAction()
                                   : m_manager->linePowerLidClosedAction();
        qDebug(logPowerSession) << "Lid closed, onBattery=" << onBattery << " action=" << action;

        switch (action) {
        case PA_Shutdown:
            m_manager->doShutdown();
            break;
        case PA_Suspend:
            m_manager->doSuspend();
            break;
        case PA_Hibernate:
            m_manager->doHibernate();
            break;
        case PA_TurnOffScreen:
            m_manager->doTurnOffScreen();
            break;
        case PA_Lock:
            m_manager->doLock();
            break;
        case PA_DoNothing:
            return;
        default:
            break;
        }

        if (action != PA_TurnOffScreen)
            m_manager->setBlackScreenActive(true);
    } else {
        if (m_manager->useWayland())
            m_manager->SetPrepareSuspend(PS_Resume);

        if (auto *iw = m_manager->idleWatcher())
            iw->simulateActivity();

        m_manager->setBlackScreenActive(false);
        m_manager->setDPMSModeOn();
    }
}
