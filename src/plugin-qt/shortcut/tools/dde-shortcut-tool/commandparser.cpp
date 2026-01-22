// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "commandparser.h"
#include "basecontroller.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>

CommandParser::CommandParser(QObject *parent)
    : QObject(parent)
{
}

CommandParser::~CommandParser()
{
    // Controllers are children, will be deleted automatically
}

void CommandParser::registerController(BaseController *controller)
{
    if (!controller) {
        return;
    }
    
    controller->setParent(this);
    m_controllers.insert(controller->name(), controller);
}

int CommandParser::run(int argc, char *argv[])
{
    QStringList args;
    for (int i = 1; i < argc; ++i) {
        args << QString::fromLocal8Bit(argv[i]);
    }

    // No arguments, print help
    if (args.isEmpty()) {
        printHelp();
        return 1;
    }

    QString firstArg = args.first();

    // Global help
    if (firstArg == "--help" || firstArg == "-h") {
        printHelp();
        return 0;
    }

    // Version info
    if (firstArg == "--version" || firstArg == "-v") {
        QTextStream out(stdout);
        out << "dde-shortcut-tool version 1.0.0\n";
        return 0;
    }

    // Find controller
    QString controllerName = firstArg.toLower();
    if (!m_controllers.contains(controllerName)) {
        QTextStream err(stderr);
        err << "Error: Unknown command '" << controllerName << "'\n";
        err << "Run 'dde-shortcut-tool --help' for usage.\n";
        return 1;
    }

    BaseController *controller = m_controllers.value(controllerName);

    // Controller help
    if (args.size() >= 2 && (args[1] == "--help" || args[1] == "-h")) {
        printControllerHelp(controllerName);
        return 0;
    }

    // No action argument
    if (args.size() < 2) {
        QTextStream err(stderr);
        err << "Error: No action specified for '" << controllerName << "'\n";
        printControllerHelp(controllerName);
        return 1;
    }

    QString action = args[1].toLower();
    
    // Check if action is supported
    QStringList supportedActions = controller->supportedActions();
    if (!supportedActions.contains(action)) {
        QTextStream err(stderr);
        err << "Error: Unknown action '" << action << "' for '" << controllerName << "'\n";
        printControllerHelp(controllerName);
        return 1;
    }

    // Collect additional arguments (args[2], args[3], ...)
    QStringList extraArgs;
    for (int i = 2; i < args.size(); ++i) {
        extraArgs << args[i];
    }

    // Execute action
    bool success = controller->execute(action, extraArgs);
    if (!success) {
        QTextStream err(stderr);
        err << "Error: Failed to execute '" << controllerName << " " << action << "'\n";
        return 1;
    }

    return 0;
}

void CommandParser::printHelp()
{
    QTextStream out(stdout);
    out << "Usage: dde-shortcut-tool <command> <action> [options]\n\n";
    out << "A command-line tool for executing shortcut actions.\n\n";
    out << "Commands:\n";
    
    for (auto it = m_controllers.constBegin(); it != m_controllers.constEnd(); ++it) {
        QString name = it.key();
        QStringList actions = it.value()->supportedActions();
        out << "  " << name.leftJustified(12) << " " << actions.join(", ") << "\n";
    }
    
    out << "\nOptions:\n";
    out << "  -h, --help      Show this help message\n";
    out << "  -v, --version   Show version information\n";
    out << "\nExamples:\n";
    out << "  dde-shortcut-tool audio mute-toggle\n";
    out << "  dde-shortcut-tool display brightness-up\n";
    out << "  dde-shortcut-tool power suspend\n";
    out << "\nUse 'dde-shortcut-tool <command> --help' for more information about a command.\n";
}

void CommandParser::printControllerHelp(const QString &controllerName)
{
    if (!m_controllers.contains(controllerName)) {
        QTextStream err(stderr);
        err << "Unknown command: " << controllerName << "\n";
        return;
    }

    BaseController *controller = m_controllers.value(controllerName);
    QStringList actions = controller->supportedActions();

    QTextStream out(stdout);
    out << "Usage: dde-shortcut-tool " << controllerName << " <action>\n\n";
    out << "Available actions:\n";
    
    for (const QString &action : actions) {
        QString help = controller->actionHelp(action);
        if (help.isEmpty()) {
            out << "  " << action << "\n";
        } else {
            out << "  " << action.leftJustified(20) << " " << help << "\n";
        }
    }
}
