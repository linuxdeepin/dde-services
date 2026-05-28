// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef BASECONTROLLER_H
#define BASECONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>

/**
 * @brief Abstract base class for controllers
 * 
 * All shortcut tool controllers must inherit from this class and implement the interface.
 * This allows command dispatching through a unified interface.
 */
class BaseController : public QObject
{
    Q_OBJECT

public:
    explicit BaseController(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~BaseController() = default;

    /**
     * @brief Get controller name (subcommand name)
     * @return e.g. "audio", "display", "power", etc.
     */
    virtual QString name() const = 0;

    /**
     * @brief Get list of supported actions
     * @return Action name list, e.g. ["mute-toggle", "volume-up", "volume-down"]
     */
    virtual QStringList supportedActions() const = 0;

    /**
     * @brief Execute specified action
     * @param action Action name
     * @param args Additional arguments for the action
     * @return true on success, false otherwise
     */
    virtual bool execute(const QString &action, const QStringList &args = QStringList()) = 0;

    /**
     * @brief Get help text for an action
     * @param action Action name
     * @return Help text
     */
    virtual QString actionHelp(const QString &action) const { 
        Q_UNUSED(action);
        return QString(); 
    }
};

#endif // BASECONTROLLER_H
