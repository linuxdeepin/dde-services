// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef COMMANDPARSER_H
#define COMMANDPARSER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>

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
    explicit CommandParser(QObject *parent = nullptr);
    ~CommandParser();

    /**
     * @brief Register a controller
     * @param controller Controller instance, ownership transfers to CommandParser
     */
    void registerController(BaseController *controller);

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
    QMap<QString, BaseController*> m_controllers;
};

#endif // COMMANDPARSER_H
