// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "actionexecutor.h"
#include "commandlineparser.h"
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
        const QStringList command = config.category == QLatin1String(CategoryKey::Custom)
                ? CommandLineParser::expandFieldCodesWithoutFiles(config.triggerValue)
                : config.triggerValue;
        executeCommand(command);
    } else if (config.triggerType == (int)TriggerType::App) { // App
        if (!config.triggerValue.isEmpty()) {
            runApp(config.triggerValue.first());
        }
    } else { // action
        qWarning() << "Running action by compositor:" << config.triggerType;
    }
}

bool ActionExecutor::executeCommand(const QStringList &command)
{
    if (command.isEmpty())
        return false;

    QStringList argsList;
    argsList << QStringLiteral("-c") << command.first();
    if (command.size() > 1)
        argsList << QStringLiteral("--") << command.sliced(1);

    qDebug() << "Running command via dde-am:" << argsList;
    return QProcess::startDetached(QStringLiteral("/usr/bin/dde-am"), argsList);
}

void ActionExecutor::runApp(const QString &appId)
{
    qDebug() << "Launching app via dde-am:" << appId;
    QProcess::startDetached("/usr/bin/dde-am", {appId});
}
