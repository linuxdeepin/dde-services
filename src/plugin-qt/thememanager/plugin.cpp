// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "thememanager.h"

#include <QDBusConnection>
#include <DGuiApplicationHelper>

DGUI_USE_NAMESPACE

static ThemeManager *manager = nullptr;

extern "C" int DSMRegister(const char *name, void *data)
{
    (void)name;

    // // 该服务目前只在 wayland 平台下使用，x11 平台下不注册服务
    if (!DGuiApplicationHelper::testAttribute(DGuiApplicationHelper::IsWaylandPlatform))
        return 0;

    manager = new ThemeManager();
    auto connection = reinterpret_cast<QDBusConnection *>(data);
    connection->registerObject("/org/deepin/service/thememanager", manager, QDBusConnection::ExportScriptableContents);
    return 0;
}

// 该函数用于资源释放
// 非常驻插件必须实现该函数，以防内存泄漏
extern "C" int DSMUnRegister(const char *name, void *data)
{
    (void)name;
    (void)data;
    manager->deleteLater();
    manager = nullptr;
    return 0;
}
