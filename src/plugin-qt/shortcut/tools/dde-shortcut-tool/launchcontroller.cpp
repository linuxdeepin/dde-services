// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "launchcontroller.h"

#include <QDebug>
#include <QProcess>

LaunchController::LaunchController(QObject *parent)
    : BaseController(parent)
{
}

LaunchController::~LaunchController()
{
}

QStringList LaunchController::supportedActions() const
{
    return {"mime"};
}

bool LaunchController::execute(const QString &action, const QStringList &args)
{
    if (action == "mime" && !args.isEmpty()) {
        launchMime(args.first());
        return true;
    }

    qWarning() << "Unknown launch action:" << action;
    return false;
}

QString LaunchController::actionHelp(const QString &action) const
{
    if (action == "mime")
        return "Launch default application for MIME type";
    return QString();
}

/**
 * @brief Launch default application for MIME type
 * @param mimeType MIME type string, supports:
 *   - Standard MIME types: audio/mpeg, video/mp4, image/png, etc.
 *   - Scheme handlers: x-scheme-handler/http, x-scheme-handler/mailto, etc.
 */
void LaunchController::launchMime(const QString &mimeType)
{
    qDebug() << "Launching default app for MIME:" << mimeType;
    
    QProcess query;
    query.start("xdg-mime", {"query", "default", mimeType});
    if (!query.waitForFinished() || query.exitCode() != 0) {
        qWarning() << "Failed to query default app for" << mimeType;
        return;
    }

    QString desktopFile = QString::fromUtf8(query.readAllStandardOutput()).trimmed();
    if (desktopFile.isEmpty()) {
        qWarning() << "No default app found for" << mimeType;
        return;
    }

    // dde-am supports appId directly (without .desktop suffix)
    QString appId = desktopFile.endsWith(".desktop")
                    ? desktopFile.chopped(8)
                    : desktopFile;
    
    qDebug() << "Launching app via dde-am:" << appId;
    QProcess::startDetached("/usr/bin/dde-am", {appId});
}
