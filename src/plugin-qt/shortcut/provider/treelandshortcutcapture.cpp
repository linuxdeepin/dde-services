// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "treelandshortcutcapture.h"

#include <QDebug>
#include <QGuiApplication>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

extern "C" {
#include <wayland-client-core.h>
}

TreelandShortcutCaptureSession::TreelandShortcutCaptureSession(
        struct ::treeland_shortcut_capture_v3 *object,
        QObject *parent)
    : QObject(parent)
    , QtWayland::treeland_shortcut_capture_v3(object)
{
}

TreelandShortcutCaptureSession::~TreelandShortcutCaptureSession()
{
    if (!m_finished && isInitialized())
        QtWayland::treeland_shortcut_capture_v3::destroy();
}

void TreelandShortcutCaptureSession::treeland_shortcut_capture_v3_captured(
        const QString &key)
{
    m_finished = true;
    Q_EMIT captured(key);
    QtWayland::treeland_shortcut_capture_v3::destroy();
    deleteLater();
}

void TreelandShortcutCaptureSession::treeland_shortcut_capture_v3_failed(uint32_t reason)
{
    m_finished = true;
    Q_EMIT failed(reason);
    QtWayland::treeland_shortcut_capture_v3::destroy();
    deleteLater();
}

TreelandShortcutCaptureManager::TreelandShortcutCaptureManager(QObject *parent)
    : QWaylandClientExtensionTemplate<TreelandShortcutCaptureManager>(1)
{
    setParent(parent);
    initialize();
}

TreelandShortcutCaptureManager::~TreelandShortcutCaptureManager()
{
    if (isActive())
        QtWayland::treeland_shortcut_manager_v3::destroy();
}

TreelandShortcutCaptureSession *TreelandShortcutCaptureManager::captureNext(QWindow *window)
{
    if (!isActive() || !window)
        return nullptr;

    QPlatformNativeInterface *nativeInterface = QGuiApplication::platformNativeInterface();
    if (!nativeInterface)
        return nullptr;
    auto *surface = static_cast<struct ::wl_surface *>(
            nativeInterface->nativeResourceForWindow(QByteArrayLiteral("surface"), window));
    if (!surface)
        return nullptr;

    struct ::treeland_shortcut_capture_v3 *object = capture_next_shortcut(surface, nullptr);
    if (!object)
        return nullptr;
    return new TreelandShortcutCaptureSession(object, this);
}
