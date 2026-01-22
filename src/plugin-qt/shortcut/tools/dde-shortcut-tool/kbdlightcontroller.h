// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef KBDLIGHTCONTROLLER_H
#define KBDLIGHTCONTROLLER_H

#include "basecontroller.h"

class QDBusInterface;

class KbdLightController : public BaseController 
{
    Q_OBJECT
public:
    explicit KbdLightController(QObject *parent = nullptr);
    ~KbdLightController() override;
    
    // BaseController interface
    QString name() const override { return "kbdlight"; }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;

    bool toggle();
    bool changeBrightness(bool raised);

private:
    bool getKbdController(QString &controllerName, int &maxBrightness);
    
    QDBusInterface *m_backlightInterface;
};

#endif
