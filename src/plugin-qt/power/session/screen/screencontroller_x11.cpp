// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencontroller.h"

#include <QLoggingCategory>

#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>

Q_DECLARE_LOGGING_CATEGORY(logPowerSession)

class X11ScreenController : public ScreenController
{
    Q_OBJECT
public:
    explicit X11ScreenController(QObject *parent = nullptr)
        : ScreenController(parent)
        , m_display(XOpenDisplay(nullptr))
    {
        int eventBase = 0;
        int errorBase = 0;
        m_valid = m_display && DPMSQueryExtension(m_display, &eventBase, &errorBase)
                  && DPMSCapable(m_display);
        if (!m_valid)
            qWarning(logPowerSession) << "X11 DPMS is unavailable";
    }

    ~X11ScreenController() override
    {
        if (m_display)
            XCloseDisplay(m_display);
    }

    bool isValid() const override { return m_valid; }
    int outputCount() const override { return m_valid ? 1 : 0; }

    Mode mode(int index) const override
    {
        if (!m_valid || index != 0)
            return On;
        CARD16 level = DPMSModeOn;
        BOOL enabled = False;
        return DPMSInfo(m_display, &level, &enabled) && enabled && level != DPMSModeOn
            ? Off : On;
    }

    void setMode(int index, Mode mode) override
    {
        if (!m_valid || index != 0)
            return;
        if (!DPMSForceLevel(m_display, mode == On ? DPMSModeOn : DPMSModeOff)) {
            qWarning(logPowerSession) << "Failed to set X11 DPMS mode" << mode;
            return;
        }
        XFlush(m_display);
        Q_EMIT modeChanged(index, mode);
    }

private:
    Display *m_display = nullptr;
    bool m_valid = false;
};

ScreenController *createX11ScreenController(QObject *parent)
{
    return new X11ScreenController(parent);
}

#include "screencontroller_x11.moc"
