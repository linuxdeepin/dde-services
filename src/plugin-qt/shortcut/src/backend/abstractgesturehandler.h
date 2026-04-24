// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/shortcutconfig.h"

#include <QObject>

class AbstractGestureHandler : public QObject
{
    Q_OBJECT
public:
    explicit AbstractGestureHandler(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~AbstractGestureHandler() = default;

    virtual bool registerGesture(const GestureConfig &config) = 0;
    virtual bool unregisterGesture(const QString &appId) = 0;

    // Commit pending changes (Wayland only)
    // Returns true if commit succeeded or no-op, false if commit failed
    virtual bool commit() { return true; }

signals:
    void activated(const QString &name);
};
