// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "actionexecutor.h"
#include <QProcess>
#include <QDebug>

ActionExecutor::ActionExecutor(QObject *parent)
    : QObject(parent)
{

}

void ActionExecutor::execute(const BaseConfig &config)
{
    qDebug() << "Executing action for" << config.appId << "Type:" << config.triggerType;
    
    if (config.triggerType == (int)TriggerType::Command) { // Command
        runCommand(config.triggerValue);
    } else if (config.triggerType == (int)TriggerType::App) { // App
        if (!config.triggerValue.isEmpty()) {
            runApp(config.triggerValue.first());
        }
    } else { // action
        qWarning() << "Running action by compositor:" << config.triggerType;
    }
}

void ActionExecutor::runCommand(const QStringList &cmd)
{
    if (cmd.isEmpty()) return;
    
    QString program = cmd.first();
    QStringList args = cmd.mid(1);
    
    QStringList argsList;
    argsList << "-c" << program;
    if (!args.isEmpty()) {
        argsList << "--" << args;
    }
    
    qDebug() << "Running command via dde-am:" << argsList;
    QProcess::startDetached("/usr/bin/dde-am", argsList);
}

void ActionExecutor::runApp(const QString &appId)
{
    qDebug() << "Launching app via dde-am:" << appId;
    QProcess::startDetached("/usr/bin/dde-am", {appId});
}
