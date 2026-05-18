// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "screencontroller.h"

#include <QObject>
#include <QWaylandClientExtension>
#include <QVector>
#include <QVariantAnimation>
#include <QHash>

#include <vector>

#include "qwayland-treeland-output-manager-v1.h"
#include "qwayland-wlr-output-power-management-unstable-v1.h"

struct wl_display;
struct wl_output;

class OutputPower : public QObject, public QtWayland::zwlr_output_power_v1
{
    Q_OBJECT
public:
    explicit OutputPower(::zwlr_output_power_v1 *obj);
    ~OutputPower() override;
Q_SIGNALS:
    void modeChanged(uint32_t mode);
protected:
    void zwlr_output_power_v1_mode(uint32_t mode) override;
    void zwlr_output_power_v1_failed() override;
};

class OutputPowerManager : public QWaylandClientExtensionTemplate<OutputPowerManager>
                         , public QtWayland::zwlr_output_power_manager_v1
{
    Q_OBJECT
public:
    OutputPowerManager();
    ~OutputPowerManager() override;
    void instantiate();
    std::unique_ptr<OutputPower> getOutputPower(wl_output *output);
};

class OutputColorControl : public QObject, public QtWayland::treeland_output_color_control_v1
{
    Q_OBJECT
public:
    explicit OutputColorControl(::treeland_output_color_control_v1 *obj);
    ~OutputColorControl() override;

    double currentBrightness() const { return m_brightness; }

Q_SIGNALS:
    void brightnessChanged(double value);

protected:
    void treeland_output_color_control_v1_brightness(wl_fixed_t brightness) override;
    void treeland_output_color_control_v1_color_temperature(uint32_t temperature) override;
    void treeland_output_color_control_v1_result(uint32_t success) override;

private:
    double m_brightness = 100.0;
};

class TreeLandOutputMgr : public QWaylandClientExtensionTemplate<TreeLandOutputMgr>
                        , public QtWayland::treeland_output_manager_v1
{
    Q_OBJECT
public:
    TreeLandOutputMgr();
    ~TreeLandOutputMgr() override;
    void instantiate();
    OutputColorControl *getColorControl(wl_output *output);
};

class WaylandScreenController : public ScreenController
{
    Q_OBJECT
public:
    struct Output {
        uint32_t registryName = 0;
        wl_output *wlOutput = nullptr;
        std::unique_ptr<OutputPower> power;
        std::unique_ptr<OutputColorControl> colorControl;
        uint32_t currentMode = 1;
    };

    explicit WaylandScreenController(QObject *parent = nullptr);
    ~WaylandScreenController() override;

    bool isValid() const override { return m_valid; }
    int outputCount() const override { return int(m_outputs.size()); }
    Mode mode(int index) const override;
    void setMode(int index, Mode m) override;

    bool supportsBrightness() const override { return m_brightnessAvailable; }
    double brightness(int index) const override;
    void setBrightness(int index, double value) override;

    wl_display *m_display = nullptr;
    std::unique_ptr<OutputPowerManager> m_manager;
    std::unique_ptr<TreeLandOutputMgr> m_treeLandMgr;
    std::vector<Output> m_outputs;
    QHash<int, QVariantAnimation*> m_brightnessAnims;
    bool m_brightnessAvailable = false;

private:
    void discoverOutputs();

    bool m_valid = false;
};
