// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "globalshortcutsprovider.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("dde-global-shortcuts-provider"));
    application.setOrganizationName(QStringLiteral("deepin"));
    application.setQuitOnLastWindowClosed(false);

    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale::system(),
                          QStringLiteral("qtbase"),
                          QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        application.installTranslator(&qtTranslator);
    }

    QTranslator providerTranslator;
    const QString translationPath = QStandardPaths::locate(
            QStandardPaths::GenericDataLocation,
            QStringLiteral("dde-global-shortcuts-provider/translations"),
            QStandardPaths::LocateDirectory);
    if (!translationPath.isEmpty()
        && providerTranslator.load(QLocale::system(),
                                   QStringLiteral("dde-global-shortcuts-provider"),
                                   QStringLiteral("_"),
                                   translationPath)) {
        application.installTranslator(&providerTranslator);
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    GlobalShortcutsProvider provider;
    if (!bus.registerObject(QStringLiteral("/org/deepin/dde/GlobalShortcutsProvider1"),
                            &provider,
                            QDBusConnection::ExportScriptableContents)) {
        qCritical() << "Could not register GlobalShortcuts provider object:"
                    << bus.lastError().message();
        return 1;
    }
    if (!bus.registerService(QStringLiteral("org.deepin.dde.GlobalShortcutsProvider1"))) {
        qWarning() << "GlobalShortcuts provider service is already owned:"
                   << bus.lastError().message();
        bus.unregisterObject(QStringLiteral("/org/deepin/dde/GlobalShortcutsProvider1"));
        return 1;
    }
    return application.exec();
}
