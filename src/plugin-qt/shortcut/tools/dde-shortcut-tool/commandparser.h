// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef COMMANDPARSER_H
#define COMMANDPARSER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>

#include <functional>

class BaseController;

/**
 * @brief Command line parser
 * 
 * Parses command line arguments and dispatches to corresponding controllers.
 * 
 * Usage examples:
 *   dde-shortcut-tool audio mute-toggle
 *   dde-shortcut-tool display brightness-up
 *   dde-shortcut-tool --help
 *   dde-shortcut-tool audio --help
 */
class CommandParser : public QObject
{
    Q_OBJECT

public:
    using ControllerFactory = std::function<BaseController*()>;

    explicit CommandParser(QObject *parent = nullptr);
    ~CommandParser();

    /**
     * @brief Register a controller
     * @param controller Controller instance, ownership transfers to CommandParser
     */
    void registerController(BaseController *controller);

    /**
     * @brief Register a controller lazily.
     *
     * The controller object is only constructed when the matching command is
     * executed or when detailed help for that command requires it. This keeps
     * unrelated DBus services out of the hot path for a shortcut action.
     */
    void registerControllerFactory(const QString &name,
                                   const QStringList &actions,
                                   const QMap<QString, QString> &actionHelp,
                                   ControllerFactory factory);

    /**
     * @brief Parse and execute command
     * @param argc Argument count
     * @param argv Argument array
     * @return 0 on success, non-zero error code on failure
     */
    int run(int argc, char *argv[]);

    /**
     * @brief Print help information
     */
    void printHelp();

    /**
     * @brief Print help information for a specific controller
     * @param controllerName Controller name
     */
    void printControllerHelp(const QString &controllerName);

private:
    struct CommandInfo {
        QStringList actions;
        QMap<QString, QString> actionHelp;
        ControllerFactory factory;
        BaseController *controller = nullptr;
    };

    BaseController *controllerFor(const QString &controllerName);

    QMap<QString, CommandInfo> m_commands;
};

#endif // COMMANDPARSER_H
