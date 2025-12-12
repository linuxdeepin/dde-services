// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "impl/xsettings1.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>

static XSettings1 *xSettings = nullptr;

extern "C" int DSMRegister(const char *name, void *data)
{

    auto connection = reinterpret_cast<QDBusConnection *>(data);
    if (connection->interface()->isServiceRegistered(name)) {
        qWarning() << "DBus service already exists:" << name;
        return 1;
    }
    xSettings = new XSettings1();

    QDBusConnection::RegisterOptions opts = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties;
    QString path = name;
    path = QString("/%1").arg(path.replace(".", "/"));
    connection->registerObject(path, xSettings, opts);
    return 0;
}

// 该函数用于资源释放
// 非常驻插件必须实现该函数，以防内存泄漏
extern "C" int DSMUnRegister(const char *name, void *data)
{
    Q_UNUSED(name);
    Q_UNUSED(data);
    if (xSettings) {
        xSettings->deleteLater();
        xSettings = nullptr;
    }
    return 0;
}
