// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/shortcutconfig.h"

#include <QObject>

class AbstractKeyHandler : public QObject
{
    Q_OBJECT
public:
    explicit AbstractKeyHandler(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~AbstractKeyHandler() = default;

    virtual bool registerKey(const KeyConfig &config) = 0;
    virtual bool unregisterKey(const QString &shortcutId) = 0;

    // Commit pending changes (Wayland only, no-op for X11)
    // Returns true if commit succeeded or no-op, false if commit failed
    virtual bool commit() { return true; }

    // Lock key state operations (X11 only, default no-op for other backends)
    virtual bool getCapsLockState() const { return false; }
    virtual bool getNumLockState() const { return false; }
    virtual void setCapsLockState(bool on) { Q_UNUSED(on); }
    virtual void setNumLockState(bool on) { Q_UNUSED(on); }

signals:
    void keyActivated(const QString &shortcutId);
};
