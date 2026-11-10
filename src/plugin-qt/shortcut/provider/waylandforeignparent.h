// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QWaylandClientExtension>

#include "qwayland-xdg-foreign-unstable-v2.h"

class QWindow;

class ImportedWaylandToplevel
    : public QObject
    , public QtWayland::zxdg_imported_v2
{
    Q_OBJECT

public:
    explicit ImportedWaylandToplevel(struct ::zxdg_imported_v2 *object,
                                    QObject *parent = nullptr);
    ~ImportedWaylandToplevel() override;

protected:
    void zxdg_imported_v2_destroyed() override;

};

class WaylandForeignParent
    : public QWaylandClientExtensionTemplate<WaylandForeignParent>
    , public QtWayland::zxdg_importer_v2
{
    Q_OBJECT

public:
    explicit WaylandForeignParent(QObject *parent = nullptr);
    ~WaylandForeignParent() override;

    ImportedWaylandToplevel *apply(QWindow *window, const QString &handle);
    static void applyActivationToken(QWindow *window, const QString &token);
};
