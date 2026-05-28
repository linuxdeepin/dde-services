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

    // commit() async (may be debounced); commitSync() returns compositor ack.
    virtual bool commit() { return true; }
    virtual bool commitSync() { return commit(); }

signals:
    void activated(const QString &name);
};
