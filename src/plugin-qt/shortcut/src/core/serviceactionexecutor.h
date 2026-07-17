// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "gestureactioncatalog.h"

#include <QObject>

class ActionExecutor;

class ServiceActionExecutor : public QObject
{
    Q_OBJECT
public:
    explicit ServiceActionExecutor(ActionExecutor *actionExecutor, QObject *parent = nullptr);

    bool execute(TriggerActionId actionId, const QString &context = QString());

private:
    ActionExecutor *m_actionExecutor;

    bool call(
            const QString &service, const QString &path, const QString &interface,
            const QString &method, TriggerActionId actionId, const QString &context = QString());
};
