// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ambientbrightnessservice.h"

#include <QDBusConnection>

using dde::ambient_brightness::AmbientBrightnessService;

static AmbientBrightnessService *g_service = nullptr;

extern "C" int DSMRegister(const char *, void *data)
{
    auto *connection = reinterpret_cast<QDBusConnection *>(data);
    if (!connection)
        return -1;
    // 防御重复注册:先释放可能存在的旧实例,避免覆盖全局指针时泄漏。
    delete g_service;
    g_service = nullptr;
    g_service = new AmbientBrightnessService(*connection);
    if (!g_service->initialize()) {
        delete g_service;
        g_service = nullptr;
        return -1;
    }
    return 0;
}

extern "C" int DSMUnRegister(const char *, void *)
{
    delete g_service;
    g_service = nullptr;
    return 0;
}
