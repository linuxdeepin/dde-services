// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "powermanager.h"
#include <QDBusConnection>

static SystemPowerManager *g_mgr = nullptr;

extern "C" int DSMRegister(const char *name, void *data)
{
    auto conn = reinterpret_cast<QDBusConnection *>(data);
    g_mgr = new SystemPowerManager(conn, QString::fromLatin1(name));
    return g_mgr->initialize() ? 0 : -1;
}

extern "C" int DSMUnRegister(const char *, void *)
{
    delete g_mgr;
    g_mgr = nullptr;
    return 0;
}
