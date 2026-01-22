// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef LAUNCHCONTROLLER_H
#define LAUNCHCONTROLLER_H

#include "basecontroller.h"

class LaunchController : public BaseController
{
    Q_OBJECT
public:
    explicit LaunchController(QObject *parent = nullptr);
    ~LaunchController() override;

    // BaseController interface
    QString name() const override { return "launch"; }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;

private:
    void launchMime(const QString &mimeType);
};

#endif // LAUNCHCONTROLLER_H
