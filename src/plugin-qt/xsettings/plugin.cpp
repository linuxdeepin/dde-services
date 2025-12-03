// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "impl/xsettings1.h"

#include <QDBusConnection>

static XSettings1 *xSettings = nullptr;

extern "C" int DSMRegister(const char *name, void *data)
{
    xSettings = new XSettings1();

    QDBusConnection::RegisterOptions opts = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties;
    QString path = name;
    path = QString("/%1").arg(path.replace(".", "/"));
    auto connection = reinterpret_cast<QDBusConnection *>(data);
    connection->registerObject(path, xSettings, opts);
    return 0;
}

// 该函数用于资源释放
// 非常驻插件必须实现该函数，以防内存泄漏
extern "C" int DSMUnRegister(const char *name, void *data)
{
    (void)name;
    (void)data;
    xSettings->deleteLater();
    xSettings = nullptr;
    return 0;
}
