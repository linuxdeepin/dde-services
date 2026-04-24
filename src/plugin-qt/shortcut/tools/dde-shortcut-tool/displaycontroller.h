// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef DISPLAYCONTROLLER_H
#define DISPLAYCONTROLLER_H

#include "basecontroller.h"

class QDBusInterface;

class DisplayController : public BaseController 
{
    Q_OBJECT
public:
    explicit DisplayController(QObject *parent = nullptr);
    ~DisplayController() override;
    
    // BaseController interface
    QString name() const override { return "display"; }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;

    // Display control methods
    bool changeBrightness(bool raised);
    bool switchDisplayMode();
    bool turnOffScreen();

private:
    void showOSD(const QString &signal);

private:
    QDBusInterface *m_displayInterface;
};

#endif
