// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVector>

/**
 * @brief Abstract interface for output power management (DPMS equivalent).
 *
 * X11 implementation: uses DPMS Extension.
 * Wayland implementation: uses wlr-output-power-management-unstable-v1.
 */
class ScreenController : public QObject
{
    Q_OBJECT

public:
    enum Mode { Off = 0, On = 1 };

    explicit ScreenController(QObject *parent = nullptr) : QObject(parent) {}
    ~ScreenController() override = default;

    /// Whether the underlying backend initialized successfully.
    virtual bool isValid() const = 0;

    /// Number of managed outputs.
    virtual int outputCount() const = 0;

    /// Current power mode of output @p index.
    virtual Mode mode(int index) const = 0;

    /// Set power mode for output @p index.
    virtual void setMode(int index, Mode m) = 0;

    /// Convenience: set the same mode on every output.
    void setAllModes(Mode m);

    /// True when all outputs are Off.
    bool isAllOff() const;

    // ── Brightness (Treeland protocol, 0.0–100.0) ───────────────
    /// Whether brightness control is supported by this backend.
    virtual bool supportsBrightness() const { return false; }

    /// Current brightness of output @p index (0.0–100.0), or -1.0 if unsupported.
    virtual double brightness(int index) const { Q_UNUSED(index); return -1.0; }

    /// Set brightness for output @p index (0.0–100.0).
    virtual void setBrightness(int index, double value) { Q_UNUSED(index); Q_UNUSED(value); }

Q_SIGNALS:
    /// Emitted when the power mode of an output changes.
    void modeChanged(int index, ScreenController::Mode mode);

    /// Emitted when the brightness of an output changes.
    void brightnessChanged(int index, double value);
};
