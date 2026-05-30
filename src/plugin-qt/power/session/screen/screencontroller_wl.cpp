// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencontroller_wl.h"

#include <QGuiApplication>
#include <QTimer>
#include <QDebug>
#include <QLoggingCategory>

#include <wayland-client.h>

Q_DECLARE_LOGGING_CATEGORY(logPowerSession)

OutputPower::OutputPower(::zwlr_output_power_v1 *obj)
    : QtWayland::zwlr_output_power_v1(obj)
{

}

OutputPower::~OutputPower() 
{
    destroy();
}

void OutputPower::zwlr_output_power_v1_mode(uint32_t mode)
{
    Q_EMIT modeChanged(mode);
}

void OutputPower::zwlr_output_power_v1_failed()
{

}

OutputPowerManager::OutputPowerManager()
    : QWaylandClientExtensionTemplate<OutputPowerManager>(1)
{

}

OutputPowerManager::~OutputPowerManager()
{
    if (isInitialized()) 
        destroy();
}

void OutputPowerManager::instantiate()
{
    initialize(); 
}

std::unique_ptr<OutputPower> OutputPowerManager::getOutputPower(wl_output *output)
{
    if (!isInitialized() || !output) 
        return nullptr;

    auto *raw = get_output_power(output);
    if (!raw) 
        return nullptr;

    return std::make_unique<OutputPower>(raw);
}


OutputColorControl::OutputColorControl(::treeland_output_color_control_v1 *obj)
    : QtWayland::treeland_output_color_control_v1(obj) 
{

}

OutputColorControl::~OutputColorControl()
{
    destroy();
}

void OutputColorControl::treeland_output_color_control_v1_brightness(wl_fixed_t brightness)
{
    m_brightness = wl_fixed_to_double(brightness);
    Q_EMIT brightnessChanged(m_brightness);
}

void OutputColorControl::treeland_output_color_control_v1_color_temperature(uint32_t)
{

}

void OutputColorControl::treeland_output_color_control_v1_result(uint32_t)
{

}

TreeLandOutputMgr::TreeLandOutputMgr()
    : QWaylandClientExtensionTemplate<TreeLandOutputMgr>(2)
{

}

TreeLandOutputMgr::~TreeLandOutputMgr()
{
    if (isInitialized())
        destroy();
}

void TreeLandOutputMgr::instantiate()
{
    initialize();
}

OutputColorControl *TreeLandOutputMgr::getColorControl(wl_output *output)
{
    if (!isInitialized() || !output) 
        return nullptr;

    auto *raw = get_color_control(output);
    if (!raw) 
        return nullptr;

    return new OutputColorControl(raw);
}


WaylandScreenController::WaylandScreenController(QObject *parent)
    : ScreenController(parent)
{
    auto *wlApp = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wlApp) {
        qWarning(logPowerSession) << "[Power::WL] Scrn: no QWaylandApplication"; 
        return;
    }

    m_display = wlApp->display();
    if (!m_display) {
        qWarning(logPowerSession) << "[Power::WL] Scrn: no wl_display";
        return;
    }

    m_manager.reset(new OutputPowerManager);
    m_manager->instantiate();
    if (!m_manager->isInitialized()) { 
        qWarning(logPowerSession) << "[Power::WL] Scrn: manager init failed";
        return;
    }

    m_treeLandMgr.reset(new TreeLandOutputMgr);
    m_treeLandMgr->instantiate();
    if (m_treeLandMgr->isInitialized()) {
        wl_display_roundtrip(m_display);
        m_brightnessAvailable = true;
    } else {
        qWarning(logPowerSession) << "[Power::WL] Scrn: treeland brightness NOT available";
    }

    discoverOutputs();
    m_valid = true;
}

WaylandScreenController::~WaylandScreenController()
{
    for (auto *anim : m_brightnessAnims) {
        anim->stop();
        anim->deleteLater();
    }
    m_brightnessAnims.clear();
    for (auto &out : m_outputs) {
        if (out.wlOutput)
            wl_output_destroy(out.wlOutput);
    }
    m_outputs.clear();
    m_treeLandMgr.reset();
    m_manager.reset();
    if (m_registry)
        wl_registry_destroy(m_registry);
}

static void scOutputGlobal(void *data, wl_registry *r, uint32_t name,
                           const char *iface, uint32_t)
{
    auto *sc = static_cast<WaylandScreenController *>(data);
    if (std::strcmp(iface, wl_output_interface.name) == 0) {
        auto *wlo = static_cast<wl_output *>(
            wl_registry_bind(r, name, &wl_output_interface, 1));
        WaylandScreenController::Output out;
        out.registryName = name;
        out.wlOutput = wlo;
        out.power = sc->m_manager->getOutputPower(wlo);
        if (out.power) {
            auto *pwr = out.power.get();
            QObject::connect(pwr, &OutputPower::modeChanged,
                sc, [sc, pwr](uint32_t m) {
                auto &outs = sc->m_outputs;
                for (int i = 0; i < int(outs.size()); ++i) {
                    if (outs[i].power.get() == pwr) {
                        outs[i].currentMode = m;
                        Q_EMIT sc->modeChanged(i, m == 0 ? ScreenController::Off : ScreenController::On);
                        return;
                    }
                }
            });
        }
        if (sc->m_brightnessAvailable) {
            out.colorControl.reset(sc->m_treeLandMgr->getColorControl(wlo));
            if (out.colorControl) {
                auto *cc = out.colorControl.get();
                QObject::connect(cc, &OutputColorControl::brightnessChanged,
                    sc, [sc, cc](double v) {
                    auto &outs = sc->m_outputs;
                    for (int i = 0; i < int(outs.size()); ++i) {
                        if (outs[i].colorControl.get() == cc) {
                            Q_EMIT sc->brightnessChanged(i, v);
                            return;
                        }
                    }
                });
            }
        }
        sc->m_outputs.push_back(std::move(out));
    }
}

static void scOutputRemove(void *data, wl_registry *, uint32_t name)
{
    auto *sc = static_cast<WaylandScreenController *>(data);
    auto &outs = sc->m_outputs;
    for (auto it = outs.begin(); it != outs.end(); ++it) {
        if (it->registryName == name) {
            if (it->power) {
                QObject::disconnect(it->power.get(), nullptr, sc, nullptr);
            }
            if (it->colorControl) {
                QObject::disconnect(it->colorControl.get(), nullptr, sc, nullptr);
            }
            if (auto *anim = sc->m_brightnessAnims.take(it->registryName)) {
                anim->stop();
                anim->deleteLater();
            }
            if (it->wlOutput)
                wl_output_destroy(it->wlOutput);
            outs.erase(it);
            break;
        }
    }
}

static const wl_registry_listener kOutputListener = {scOutputGlobal, scOutputRemove};

void WaylandScreenController::discoverOutputs()
{
    if (m_registry)
        wl_registry_destroy(m_registry);
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &kOutputListener, this);
    wl_display_roundtrip(m_display);
    wl_display_roundtrip(m_display);
}

ScreenController::Mode WaylandScreenController::mode(int index) const
{
    if (index < 0 || size_t(index) >= m_outputs.size()) 
        return On;

    return m_outputs[size_t(index)].currentMode == 0 ? Off : On;
}

void WaylandScreenController::setMode(int index, Mode m)
{
    if (index < 0 || size_t(index) >= m_outputs.size()) return;
    auto &out = m_outputs[size_t(index)];
    uint32_t wm = m == Off ? 0 : 1;
    if (out.power) {
        out.power->set_mode(wm);
        wl_display_flush(m_display);
    }
    out.currentMode = wm;
    Q_EMIT modeChanged(index, m);
}

double WaylandScreenController::brightness(int index) const
{
    if (index < 0 || size_t(index) >= m_outputs.size()) {
        qWarning(logPowerSession) << "[Power::WL] brightness(" << index << "): out of range (total=" << int(m_outputs.size()) << ")";
        return -1.0;
    }
    auto &out = m_outputs[size_t(index)];
    if (out.colorControl) {
        return out.colorControl->currentBrightness();
    }
    return -1.0;
}

void WaylandScreenController::setBrightness(int index, double value)
{
    if (index < 0 || size_t(index) >= m_outputs.size()) {
        qWarning(logPowerSession) << "[Power::WL] setBrightness(" << index << ", " << value << "): out of range (total=" << int(m_outputs.size()) << ")";
        return;
    }

    auto &out = m_outputs[size_t(index)];
    if (!out.colorControl) {
        qWarning(logPowerSession) << "[Power::WL] setBrightness(" << index << ", " << value << "): no colorControl";
        return;
    }

    double current = out.colorControl->currentBrightness();
    if (current < 0.0 || std::abs(current - value) < 0.5) {
        out.colorControl->set_brightness(wl_fixed_from_double(value));
        out.colorControl->commit();
        wl_display_flush(m_display);
        return;
    }

    if (auto *old = m_brightnessAnims.take(out.registryName)) {
        old->disconnect(this);
        old->stop();
        old->deleteLater();
    }

    auto *anim = new QVariantAnimation(this);
    anim->setDuration(400);
    anim->setStartValue(current);
    anim->setEndValue(value);
    anim->setEasingCurve(QEasingCurve::InOutCubic);

    auto regName = out.registryName;
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this, regName](const QVariant &v) {
        auto &outs = m_outputs;
        for (int i = 0; i < int(outs.size()); ++i) {
            if (outs[i].registryName == regName) {
                if (outs[i].colorControl) {
                    outs[i].colorControl->set_brightness(wl_fixed_from_double(v.toDouble()));
                    outs[i].colorControl->commit();
                    wl_display_flush(m_display);
                }
                return;
            }
        }
    });

    m_brightnessAnims[regName] = anim;
    anim->start();
}
