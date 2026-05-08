// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "impl/screenscale.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDebug>
#include <memory>

struct QObjectDeleteLater {
    void operator()(QObject *obj) const {
        if (obj) {
            obj->deleteLater();
        }
    }
};

static std::unique_ptr<ScreenScale, QObjectDeleteLater> screenScale;

extern "C" int DSMRegister(const char *name, void *data)
{
    auto connection = reinterpret_cast<QDBusConnection *>(data);
    if (connection->interface()->isServiceRegistered(name)) {
        qWarning() << "DBus service already exists:" << name;
        return 1;
    }

    screenScale.reset(new ScreenScale());

    QDBusConnection::RegisterOptions opts = QDBusConnection::ExportAllSlots
                                           | QDBusConnection::ExportAllSignals
                                           | QDBusConnection::ExportAllProperties;
    
    QString path = name;
    path = QString("/%1").arg(path.replace(".", "/"));
    
    if (!connection->registerObject(path, screenScale.get(), opts)) {
        qWarning() << "Failed to register object at" << path;
        screenScale.reset();
        return 2;
    }

    return 0;
}

extern "C" int DSMUnRegister(const char *name, void *data)
{
    Q_UNUSED(name);
    Q_UNUSED(data);
    screenScale.reset();
    return 0;
}
