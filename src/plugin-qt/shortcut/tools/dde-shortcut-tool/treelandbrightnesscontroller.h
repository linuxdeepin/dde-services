// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef TREELANDBRIGHTNESSCONTROLLER_H
#define TREELANDBRIGHTNESSCONTROLLER_H

#include "qwayland-treeland-output-manager-v1.h"

#include <functional>
#include <memory>
#include <vector>

struct wl_display;
struct wl_output;
struct wl_registry;

class TreelandBrightnessController;

namespace TreelandBrightnessPrivate {

class TreelandColorControl : public QtWayland::treeland_output_color_control_v1
{
public:
    explicit TreelandColorControl(struct ::treeland_output_color_control_v1 *object);
    ~TreelandColorControl() override;

    bool hasBrightness() const;
    double brightness() const;
    double changeBrightness(bool raised);
    void setResultHandler(std::function<void(bool)> handler);

protected:
    void treeland_output_color_control_v1_result(uint32_t success) override;
    void treeland_output_color_control_v1_color_temperature(uint32_t temperature) override;
    void treeland_output_color_control_v1_brightness(wl_fixed_t brightness) override;

private:
    double m_brightness = -1.0;
    std::function<void(bool)> m_resultHandler;
};

class TreelandOutputManager : public QtWayland::treeland_output_manager_v1
{
public:
    TreelandOutputManager(struct wl_registry *registry, uint32_t id, int version);
    ~TreelandOutputManager() override;

    std::unique_ptr<TreelandColorControl> createColorControl(struct wl_output *output);
};

class WaylandBrightnessSession
{
    friend class ::TreelandBrightnessController;

private:
    WaylandBrightnessSession() = default;
    ~WaylandBrightnessSession();

    bool changeBrightness(bool raised);

    struct Output {
        uint32_t registryName = 0;
        struct wl_output *wlOutput = nullptr;
        std::unique_ptr<TreelandColorControl> colorControl;
    };

    static void handleGlobal(void *data, struct wl_registry *registry, uint32_t name,
                             const char *interface, uint32_t version);
    static void handleGlobalRemove(void *data, struct wl_registry *registry, uint32_t name);

    struct wl_display *m_display = nullptr;
    struct wl_registry *m_registry = nullptr;
    std::unique_ptr<TreelandOutputManager> m_manager;
    std::vector<Output> m_outputs;
};

} // namespace TreelandBrightnessPrivate

class TreelandBrightnessController
{
public:
    bool changeBrightness(bool raised);
};

#endif // TREELANDBRIGHTNESSCONTROLLER_H
