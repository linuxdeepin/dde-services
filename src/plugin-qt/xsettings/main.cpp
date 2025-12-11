// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "impl/xsettings1.h"

#include <DLog>

#include <QCoreApplication>
#include <QDBusConnection>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    a.setApplicationName("org.deepin.dde.XSettings");
    Dtk::Core::DLogManager::registerJournalAppender();

    QDBusConnection connection =QDBusConnection::sessionBus();
    XSettings1 *xSettings = new XSettings1(&a);

    QDBusConnection::RegisterOptions opts = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals | QDBusConnection::ExportAllProperties;
    connection.registerObject("/org/deepin/dde/XSettings1", xSettings, opts);
    if (!connection.registerService("org.deepin.dde.XSettings1")) {
        qWarning() << "Failed to register D-Bus service";
        return 0;
    }
    return a.exec();
}
