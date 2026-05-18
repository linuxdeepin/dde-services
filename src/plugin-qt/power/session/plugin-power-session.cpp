// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "powermanager.h"

#include <QDBusConnection>
#include <QCoreApplication>
#include <QLocale>
#include <QStandardPaths>
#include <QTranslator>

static PowerManager *g_mgr = nullptr;
static QTranslator *g_trans = nullptr;

extern "C" int DSMRegister(const char *name, void *data)
{
    if (qEnvironmentVariable("XDG_SESSION_TYPE") != QLatin1String("wayland"))
        return 0;

    g_trans = new QTranslator;
    const QString &path = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                  QStringLiteral("plugin-power-session/translations"),
                                                  QStandardPaths::LocateDirectory);
    if (g_trans->load(QLocale::system(), "plugin-power-session", "_", path)) {
        QCoreApplication::installTranslator(g_trans);
    }

    auto conn = reinterpret_cast<QDBusConnection *>(data);
    g_mgr = new PowerManager(conn, QString::fromLatin1(name));
    if (!g_mgr->initialize()) { delete g_mgr; g_mgr = nullptr; return -1; }
    return 0;
}

extern "C" int DSMUnRegister(const char *, void *)
{
    if (g_trans) {
        QCoreApplication::removeTranslator(g_trans);
        delete g_trans;
        g_trans = nullptr;
    }
    delete g_mgr; g_mgr = nullptr;
    return 0;
}
