// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <cstdint>

/**
 * @brief Abstract interface for user idle detection.
 *
 * X11 implementation: uses org.freedesktop.ScreenSaver D-Bus signals.
 * Wayland implementation: uses ext-idle-notify-v1 protocol.
 */
class IdleWatcher : public QObject
{
    Q_OBJECT

public:
    explicit IdleWatcher(QObject *parent = nullptr) : QObject(parent) {}
    ~IdleWatcher() override = default;

    /// Whether the underlying backend initialized successfully.
    virtual bool isValid() const = 0;

    /// Set the idle timeout in seconds.  Re-creates the notification object
    /// with the new timeout when the backend supports it.
    virtual void setTimeout(uint32_t timeoutSec) = 0;

    /// Simulate user activity — resets the idle timer.
    virtual void simulateActivity() = 0;

    /// Elapsed idle time in milliseconds (0 if not idle).
    virtual uint32_t idleTimeMs() const = 0;

    /// Whether the user is currently idle.
    virtual bool isIdle() const = 0;

Q_SIGNALS:
    /// Emitted when the user becomes idle.
    void idled();

    /// Emitted when the user resumes activity.
    void resumed();
};
