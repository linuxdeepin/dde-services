// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "shortcutconfig.h"

#include <QObject>

class ActionExecutor : public QObject
{
    Q_OBJECT
public:
    explicit ActionExecutor(QObject *parent = nullptr);

    void execute(const BaseConfig &config);
    bool executeCommand(const QStringList &command);

private:
    void runApp(const QString &appId);
};
