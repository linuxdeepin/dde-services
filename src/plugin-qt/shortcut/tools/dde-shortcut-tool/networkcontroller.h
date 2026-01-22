// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef NETWORKCONTROLLER_H
#define NETWORKCONTROLLER_H

#include "basecontroller.h"

class QDBusInterface;

class NetworkController : public BaseController
{
    Q_OBJECT

public:
    explicit NetworkController(QObject *parent = nullptr);
    ~NetworkController() override;

    // BaseController interface
    QString name() const override { return "network"; }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;

private:
    void toggleWifi();
    void toggleAirplaneMode();

    QDBusInterface *m_airplaneInterface;
};

#endif // NETWORKCONTROLLER_H
