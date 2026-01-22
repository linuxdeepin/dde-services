// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QGuiApplication>
#include <QDebug>

#include "core/shortcutmanager.h"
#include "config/configloader.h"

int main(int argc, char *argv[])
{
    // Use QGuiApplication instead of QCoreApplication
    // because Wayland backend needs access to QNativeInterface::QWaylandApplication
    QGuiApplication a(argc, argv);
    a.setOrganizationName("deepin");
    a.setApplicationName("dde-shortcut-manager");

    // Check for --list parameter
    if (a.arguments().contains("--list")) {
        ConfigLoader config;
        config.scanForConfigs();
        config.dumpConfigs();
        return 0;
    }

    // Create and initialize ShortcutManager
    auto *shortcutManager = new ShortcutManager(&a);
    if (!shortcutManager->init()) {
        qCritical() << "Failed to initialize ShortcutManager";
        return 1;
    }

    return a.exec();
}
