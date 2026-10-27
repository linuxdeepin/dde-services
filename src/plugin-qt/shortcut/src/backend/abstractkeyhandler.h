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
    enum CaptureResult : uint {
        CaptureSuccess = 0,
        CaptureInvalid = 1,
        CaptureCanceled = 2,
        CaptureTimedOut = 3,
    };
    Q_ENUM(CaptureResult)

    explicit AbstractKeyHandler(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~AbstractKeyHandler() = default;

    virtual bool registerKey(const KeyConfig &config) = 0;
    virtual bool unregisterKey(const QString &shortcutId) = 0;
    virtual bool isAvailable() const { return true; }

    // commit() flushes queued requests; commitSync() waits for their processing
    // and reports binding failures. Neither provides an atomic transaction.
    // Treeland may defer inactive-session requests past the sync, reporting
    // registrationFailed later. X11 is immediate and these are no-ops.
    virtual bool commit() { return true; }
    virtual bool commitSync() { return commit(); }

    virtual bool beginCapture(quint64 captureId, uint timeoutMs, const QString &owner)
    {
        Q_UNUSED(captureId);
        Q_UNUSED(timeoutMs);
        Q_UNUSED(owner);
        return false;
    }
    virtual bool endCapture(const QString &owner)
    {
        Q_UNUSED(owner);
        return false;
    }

    // Lock key state operations (X11 only, default no-op for other backends)
    virtual bool getCapsLockState() const { return false; }
    virtual bool getNumLockState() const { return false; }
    virtual void setCapsLockState(bool on) { Q_UNUSED(on); }
    virtual void setNumLockState(bool on) { Q_UNUSED(on); }

signals:
    void keyActivated(const QString &shortcutId);
    void registrationFailed(const QString &shortcutId);
    void numLockStateChanged(bool on);
    void capsLockStateChanged(bool on);
    void captureStarted();
    void captureKeyEvent(bool pressed, const QString &keystroke);
    void captureResult(quint64 captureId, uint result, const QString &keystroke);
    void captureFinished();
    void keymapAboutToChange();
    void keymapChanged();
};
