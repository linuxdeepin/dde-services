// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "waylandforeignparent.h"

#include <QGuiApplication>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>
#include <private/qwaylandwindow_p.h>

extern "C" {
#include <wayland-client-core.h>
}

ImportedWaylandToplevel::ImportedWaylandToplevel(struct ::zxdg_imported_v2 *object,
                                                 QObject *parent)
    : QObject(parent)
    , QtWayland::zxdg_imported_v2(object)
{
}

ImportedWaylandToplevel::~ImportedWaylandToplevel()
{
    if (isInitialized())
        QtWayland::zxdg_imported_v2::destroy();
}

void ImportedWaylandToplevel::zxdg_imported_v2_destroyed()
{
    deleteLater();
}

WaylandForeignParent::WaylandForeignParent(QObject *parent)
    : QWaylandClientExtensionTemplate<WaylandForeignParent>(1)
{
    setParent(parent);
    initialize();
}

WaylandForeignParent::~WaylandForeignParent()
{
    if (isActive())
        QtWayland::zxdg_importer_v2::destroy();
}

ImportedWaylandToplevel *WaylandForeignParent::apply(QWindow *window,
                                                     const QString &handle)
{
    if (!isActive() || !window || handle.isEmpty())
        return nullptr;

    QPlatformNativeInterface *nativeInterface = QGuiApplication::platformNativeInterface();
    if (!nativeInterface)
        return nullptr;
    auto *surface = static_cast<struct ::wl_surface *>(
            nativeInterface->nativeResourceForWindow(QByteArrayLiteral("surface"), window));
    if (!surface)
        return nullptr;

    struct ::zxdg_imported_v2 *object = import_toplevel(handle);
    if (!object)
        return nullptr;
    auto *imported = new ImportedWaylandToplevel(object, window);
    imported->set_parent_of(surface);
    return imported;
}

void WaylandForeignParent::applyActivationToken(QWindow *window, const QString &token)
{
    if (!window || token.isEmpty() || QGuiApplication::platformName() != QLatin1String("wayland"))
        return;
    auto *platformWindow = dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
    if (!platformWindow)
        return;
    platformWindow->setXdgActivationToken(token);
    window->requestActivate();
}
