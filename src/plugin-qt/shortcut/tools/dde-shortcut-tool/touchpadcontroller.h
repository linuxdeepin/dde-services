// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef TOUCHPADCONTROLLER_H
#define TOUCHPADCONTROLLER_H

#include "basecontroller.h"

class QDBusInterface;

class TouchPadController : public BaseController 
{
    Q_OBJECT
public:
    explicit TouchPadController(QObject *parent = nullptr);
    ~TouchPadController() override;
    
    // BaseController interface
    QString name() const override { return "touchpad"; }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;
    
private:
    bool toggle();
    bool setEnabled(bool enabled);
    void showOSD(const QString &signal);
    
    QDBusInterface *m_touchpadInterface;
};

#endif
