// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "basecontroller.h"

#include <QMap>

class WmController : public BaseController
{
    Q_OBJECT
public:
    explicit WmController(QObject *parent = nullptr);

    static QString commandName() { return QStringLiteral("wm"); }
    static QStringList commandActions();
    static QMap<QString, QString> commandActionHelp();

    QString name() const override { return commandName(); }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = {}) override;
    QString actionHelp(const QString &action) const override;

private:
    bool switchEffects();
};
