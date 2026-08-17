// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "idlewatcher.h"
#include "../../powerconstants.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QLoggingCategory>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

Q_DECLARE_LOGGING_CATEGORY(logPowerSession)

using namespace PowerDBus;

class X11IdleWatcher : public IdleWatcher
{
    Q_OBJECT
public:
    explicit X11IdleWatcher(QObject *parent = nullptr)
        : IdleWatcher(parent)
        , m_screenSaver(kX11IdleService, kX11IdlePath, kX11IdleInterface,
                        QDBusConnection::sessionBus())
        , m_display(XOpenDisplay(nullptr))
    {
        auto bus = QDBusConnection::sessionBus();
        const bool idleOn = bus.connect(kX11IdleService, kX11IdlePath, kX11IdleInterface, "IdleOn",
                                        this, SLOT(onIdleOn()));
        const bool idleOff = bus.connect(kX11IdleService, kX11IdlePath, kX11IdleInterface, "IdleOff",
                                         this, SLOT(onIdleOff()));
        m_valid = idleOn && idleOff;
        if (!m_valid)
            qWarning(logPowerSession) << "Failed to connect X11 idle signals";
    }

    ~X11IdleWatcher() override
    {
        if (m_display)
            XCloseDisplay(m_display);
    }

    bool isValid() const override { return m_valid; }

    void setTimeout(uint32_t timeoutSec) override
    {
        m_timeoutSec = timeoutSec;
        m_screenSaver.asyncCall("SetTimeout", timeoutSec, 0u, false);
    }

    void simulateActivity() override
    {
        m_screenSaver.asyncCall("SimulateUserActivity");
    }

    uint32_t idleTimeMs() const override
    {
        if (!m_display)
            return 0;
        XScreenSaverInfo *info = XScreenSaverAllocInfo();
        const bool ok = info && XScreenSaverQueryInfo(
            m_display, DefaultRootWindow(m_display), info);
        const uint32_t idle = ok ? info->idle : 0;
        if (info)
            XFree(info);
        return idle;
    }

    bool isIdle() const override { return m_isIdle; }

private Q_SLOTS:
    void onIdleOn()
    {
        if (m_isIdle)
            return;
        m_isIdle = true;
        Q_EMIT idled();
    }

    void onIdleOff()
    {
        if (!m_isIdle)
            return;
        m_isIdle = false;
        Q_EMIT resumed();
    }

private:
    QDBusInterface m_screenSaver;
    Display *m_display = nullptr;
    uint32_t m_timeoutSec = 300;
    bool m_valid = false;
    bool m_isIdle = false;
};

IdleWatcher *createX11IdleWatcher(QObject *parent)
{
    return new X11IdleWatcher(parent);
}

#include "idlewatcher_x11.moc"
