// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include "basecontroller.h"

#include <QMap>

class QDBusInterface;

/**
 * @brief Camera hotkey controller
 *
 * Toggles camera privacy through the system daemon
 * (org.deepin.dde.Daemon1.SetCameraPrivacy).  The hotkey only drives the
 * hardware switch; it shows the matching OSD and never launches or closes a
 * camera application.
 */
class CameraController : public BaseController
{
    Q_OBJECT

public:
    explicit CameraController(QObject *parent = nullptr);
    ~CameraController() override;

    static QString commandName() { return "camera"; }
    static QStringList commandActions();
    static QMap<QString, QString> commandActionHelp();

    // BaseController interface
    QString name() const override { return commandName(); }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;

private:
    bool toggle();
    bool setPrivacy(bool privacy);
    // Returns the daemon's privacy state; known is false when no camera is
    // available to report on.
    bool currentPrivacy(bool *known) const;
    // Asks the daemon to switch the camera; applied reports whether hardware
    // switching happened.
    bool requestPrivacy(bool privacy, bool *applied);
    void showOSD(const QString &signal);

    QDBusInterface *m_daemonInterface;
};

#endif // CAMERACONTROLLER_H
