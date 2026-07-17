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
    CommandInfo info;
    info.actions = controller->supportedActions();
    for (const QString &action : info.actions) {
        info.actionHelp.insert(action, controller->actionHelp(action));
    }
    info.controller = controller;
    m_commands.insert(controller->name().toLower(), info);
}

void CommandParser::registerControllerFactory(const QString &name,
                                              const QStringList &actions,
                                              const QMap<QString, QString> &actionHelp,
                                              ControllerFactory factory)
{
    if (name.isEmpty() || !factory) {
        return;
    }

    CommandInfo info;
    info.actions = actions;
    info.actionHelp = actionHelp;
    info.factory = std::move(factory);
    m_commands.insert(name.toLower(), info);
}

BaseController *CommandParser::controllerFor(const QString &controllerName)
{
    auto it = m_commands.find(controllerName);
    if (it == m_commands.end()) {
        return nullptr;
    }

    if (!it->controller && it->factory) {
        it->controller = it->factory();
        if (it->controller) {
            it->controller->setParent(this);
        }
    }

    return it->controller;
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
    if (!m_commands.contains(controllerName)) {
        QTextStream err(stderr);
        err << "Error: Unknown command '" << controllerName << "'\n";
        err << "Run 'dde-shortcut-tool --help' for usage.\n";
        return 1;
    }

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
    const QStringList supportedActions = m_commands.value(controllerName).actions;
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

    BaseController *controller = controllerFor(controllerName);
    if (!controller) {
        QTextStream err(stderr);
        err << "Error: Failed to initialize command '" << controllerName << "'\n";
        return 1;
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
    
    for (auto it = m_commands.constBegin(); it != m_commands.constEnd(); ++it) {
        QString name = it.key();
        out << "  " << name.leftJustified(12) << " " << it.value().actions.join(", ") << "\n";
    }
    
    out << "\nOptions:\n";
    out << "  -h, --help      Show this help message\n";
    out << "  -v, --version   Show version information\n";
    out << "\nExamples:\n";
    out << "  dde-shortcut-tool audio mute-toggle\n";
    out << "  dde-shortcut-tool display brightness-up\n";
    out << "  dde-shortcut-tool power system-away\n";
    out << "  dde-shortcut-tool wm switch-effects\n";
    out << "\nUse 'dde-shortcut-tool <command> --help' for more information about a command.\n";
}

void CommandParser::printControllerHelp(const QString &controllerName)
{
    if (!m_commands.contains(controllerName)) {
        QTextStream err(stderr);
        err << "Unknown command: " << controllerName << "\n";
        return;
    }

    const CommandInfo info = m_commands.value(controllerName);

    QTextStream out(stdout);
    out << "Usage: dde-shortcut-tool " << controllerName << " <action>\n\n";
    out << "Available actions:\n";
    
    for (const QString &action : info.actions) {
        QString help = info.actionHelp.value(action);
        if (help.isEmpty()) {
            out << "  " << action << "\n";
        } else {
            out << "  " << action.leftJustified(20) << " " << help << "\n";
        }
    }
}
