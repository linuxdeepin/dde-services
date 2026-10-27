// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "backend/abstractgesturehandler.h"

#include <QMap>

class TreelandShortcutWrapper;

class WaylandGestureHandler : public AbstractGestureHandler
{
    Q_OBJECT
public:
    explicit WaylandGestureHandler(TreelandShortcutWrapper *wrapper, QObject *parent = nullptr);
    ~WaylandGestureHandler() override;

    bool registerGesture(const GestureConfig &config) override;
    bool unregisterGesture(const QString &id) override;
    bool commit() override;
    bool commitSync() override;

private slots:
    void onActivated(const QString &name, uint32_t flags);
    void onBindFailed(const QString &name, uint32_t error);

private:
    TreelandShortcutWrapper *m_wrapper = nullptr;

    QMap<QString, QString> m_bindings; // Config id -> unique wire name
    quint64 m_bindingSerial = 0;
};
