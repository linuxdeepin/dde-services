// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "treelandbrightnesscontroller.h"

#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QLockFile>
#include <QStandardPaths>
#include <QTimer>
#include <QGuiApplication>

#include <algorithm>
#include <utility>

namespace {

constexpr double BrightnessStep = 5.0;
constexpr int BrightnessCommitTimeoutMs = 1000;
constexpr int BrightnessLockTimeoutMs = 1500;

} // namespace

namespace TreelandBrightnessPrivate {

TreelandColorControl::TreelandColorControl(struct ::treeland_output_color_control_v1 *object)
    : QtWayland::treeland_output_color_control_v1(object)
{
}

TreelandColorControl::~TreelandColorControl()
{
    if (isInitialized()) {
        destroy();
    }
}

bool TreelandColorControl::hasBrightness() const
{
    return m_brightness >= 0.0;
}

double TreelandColorControl::brightness() const
{
    return m_brightness;
}

double TreelandColorControl::changeBrightness(bool raised)
{
    const double delta = raised ? BrightnessStep : -BrightnessStep;
    const double target = std::clamp(m_brightness + delta, 0.0, 100.0);

    set_brightness(wl_fixed_from_double(target));
    commit();
    return target;
}

void TreelandColorControl::setResultHandler(std::function<void(bool)> handler)
{
    m_resultHandler = std::move(handler);
}

void TreelandColorControl::treeland_output_color_control_v1_result(uint32_t success)
{
    if (m_resultHandler) {
        m_resultHandler(success == 1);
    }
}

void TreelandColorControl::treeland_output_color_control_v1_color_temperature(uint32_t)
{
}

void TreelandColorControl::treeland_output_color_control_v1_brightness(wl_fixed_t brightness)
{
    m_brightness = wl_fixed_to_double(brightness);
}

TreelandOutputManager::TreelandOutputManager(struct wl_registry *registry, uint32_t id, int version)
    : QtWayland::treeland_output_manager_v1(registry, id, version)
{
}

TreelandOutputManager::~TreelandOutputManager()
{
    if (isInitialized()) {
        destroy();
    }
}

std::unique_ptr<TreelandColorControl> TreelandOutputManager::createColorControl(struct wl_output *output)
{
    if (!isInitialized() || !output) {
        return nullptr;
    }

    auto *control = get_color_control(output);
    if (!control) {
        return nullptr;
    }

    return std::make_unique<TreelandColorControl>(control);
}

WaylandBrightnessSession::~WaylandBrightnessSession()
{
    for (auto &output : m_outputs) {
        output.colorControl.reset();
    }
    m_manager.reset();

    for (auto &output : m_outputs) {
        if (output.wlOutput) {
            wl_output_destroy(output.wlOutput);
        }
    }
    m_outputs.clear();

    if (m_registry) {
        wl_registry_destroy(m_registry);
    }
}

bool WaylandBrightnessSession::changeBrightness(bool raised)
{
    auto *waylandApp = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    if (!waylandApp) {
        qWarning() << "TreelandBrightnessController: no QWaylandApplication";
        return false;
    }

    m_display = waylandApp->display();
    if (!m_display) {
        qWarning() << "TreelandBrightnessController: no wl_display";
        return false;
    }

    m_registry = wl_display_get_registry(m_display);
    if (!m_registry) {
        qWarning() << "TreelandBrightnessController: failed to get wl_registry";
        return false;
    }

    static const struct wl_registry_listener registryListener = {
        handleGlobal,
        handleGlobalRemove,
    };
    wl_registry_add_listener(m_registry, &registryListener, this);

    if (wl_display_roundtrip(m_display) < 0) {
        qWarning() << "TreelandBrightnessController: failed to discover Wayland globals";
        return false;
    }

    if (!m_manager) {
        qWarning() << "TreelandBrightnessController: output manager v2 is unavailable";
        return false;
    }

    for (auto &output : m_outputs) {
        output.colorControl = m_manager->createColorControl(output.wlOutput);
    }

    // The protocol sends the current brightness once after each color
    // control is created. A roundtrip makes those values available before
    // calculating the shortcut's relative adjustment.
    if (wl_display_roundtrip(m_display) < 0) {
        qWarning() << "TreelandBrightnessController: failed to query current brightness";
        return false;
    }

    int requestedCount = 0;
    for (auto &output : m_outputs) {
        if (!output.colorControl || !output.colorControl->hasBrightness()) {
            qWarning() << "TreelandBrightnessController: brightness unavailable for output"
                       << output.registryName;
            continue;
        }

        const double current = output.colorControl->brightness();
        const double target = output.colorControl->changeBrightness(raised);
        qDebug() << "TreelandBrightnessController: output" << output.registryName
                 << "brightness" << current << "->" << target;
        ++requestedCount;
    }

    if (requestedCount == 0) {
        qWarning() << "TreelandBrightnessController: no output supports brightness control";
        return false;
    }

    QEventLoop eventLoop;
    QTimer timeout;
    timeout.setSingleShot(true);

    int resultCount = 0;
    int successCount = 0;
    for (auto &output : m_outputs) {
        if (!output.colorControl || !output.colorControl->hasBrightness()) {
            continue;
        }

        output.colorControl->setResultHandler([&eventLoop, &resultCount, &successCount, requestedCount](bool success) {
            ++resultCount;
            if (success) {
                ++successCount;
            }
            if (resultCount >= requestedCount) {
                eventLoop.quit();
            }
        });
    }

    const auto clearResultHandlers = [this] {
        for (auto &output : m_outputs) {
            if (output.colorControl) {
                output.colorControl->setResultHandler({});
            }
        }
    };

    QObject::connect(&timeout, &QTimer::timeout, &eventLoop, &QEventLoop::quit);

    // The compositor may finish applying output color asynchronously after a
    // wl_display sync callback. Flush explicitly, then keep processing events
    // until all result callbacks arrive or the timeout expires.
    // EAGAIN leaves requests buffered; Qt Wayland retries the flush while the
    // event loop below processes display events.
    if (wl_display_flush(m_display) < 0 && errno != EAGAIN) {
        qWarning() << "TreelandBrightnessController: failed to flush brightness requests";
        clearResultHandlers();
        return false;
    }
    timeout.start(BrightnessCommitTimeoutMs);
    eventLoop.exec();

    clearResultHandlers();

    if (resultCount < requestedCount) {
        qWarning() << "TreelandBrightnessController: timed out waiting for brightness results:"
                   << resultCount << "/" << requestedCount;
    }

    if (successCount == 0) {
        qWarning() << "TreelandBrightnessController: compositor rejected brightness changes";
        return false;
    }

    return true;
}

void WaylandBrightnessSession::handleGlobal(void *data, struct wl_registry *registry,
                                            uint32_t name, const char *interface,
                                            uint32_t version)
{
    auto *session = static_cast<WaylandBrightnessSession *>(data);

    if (std::strcmp(interface, wl_output_interface.name) == 0) {
        auto *output = static_cast<struct wl_output *>(
                wl_registry_bind(registry, name, &wl_output_interface, 1));
        if (output) {
            session->m_outputs.push_back({name, output, nullptr});
        }
        return;
    }

    if (std::strcmp(interface, treeland_output_manager_v1_interface.name) == 0
            && version >= 2 && !session->m_manager) {
        session->m_manager = std::make_unique<TreelandOutputManager>(
                registry, name, static_cast<int>(std::min(version, 2U)));
    }
}

void WaylandBrightnessSession::handleGlobalRemove(void *data, struct wl_registry *, uint32_t name)
{
    auto *session = static_cast<WaylandBrightnessSession *>(data);
    auto it = std::find_if(session->m_outputs.begin(), session->m_outputs.end(),
                           [name](const Output &output) {
                               return output.registryName == name;
                           });
    if (it == session->m_outputs.end()) {
        return;
    }

    it->colorControl.reset();
    if (it->wlOutput) {
        wl_output_destroy(it->wlOutput);
    }
    session->m_outputs.erase(it);
}

} // namespace TreelandBrightnessPrivate

bool TreelandBrightnessController::changeBrightness(bool raised)
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtimeDir.isEmpty()) {
        qWarning() << "TreelandBrightnessController: user runtime directory is unavailable";
        return false;
    }

    // Serialize cross-process brightness read-modify-write operations.
    QLockFile lock(QDir(runtimeDir).filePath(QStringLiteral("dde-shortcut-tool-display-brightness.lock")));
    if (!lock.tryLock(BrightnessLockTimeoutMs)) {
        qWarning() << "TreelandBrightnessController: timed out waiting for another brightness operation";
        return false;
    }

    TreelandBrightnessPrivate::WaylandBrightnessSession session;
    return session.changeBrightness(raised);
}
