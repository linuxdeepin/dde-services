// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "cameracontroller.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>


namespace {

// dde-system-daemon owns the camera privacy switch: it applies the V4L2
// privacy control when the hardware exposes one and otherwise unbinds the
// uvcvideo driver on the video interface (which keeps a built-in mic on the
// same device working).
constexpr const char *kDaemonService = "org.deepin.dde.Daemon1";
constexpr const char *kDaemonPath = "/org/deepin/dde/Daemon1";
constexpr const char *kDaemonInterface = "org.deepin.dde.Daemon1";


} // namespace

CameraController::CameraController(QObject *parent)
    : BaseController(parent)
    , m_daemonInterface(nullptr)
{
    m_daemonInterface = new QDBusInterface(
        kDaemonService,
        kDaemonPath,
        kDaemonInterface,
        QDBusConnection::systemBus(),
        this
    );

    if (!m_daemonInterface->isValid()) {
        qWarning() << "Failed to connect to Daemon1 service:"
                   << m_daemonInterface->lastError().message();
    }
}

CameraController::~CameraController()
{
}

QStringList CameraController::commandActions()
{
    return QStringList{
        "toggle",
        "on",
        "off"
    };
}

QMap<QString, QString> CameraController::commandActionHelp()
{
    return {
        {"toggle", "Toggle camera privacy (camera off/on)"},
        {"on", "Enable the camera (privacy off)"},
        {"off", "Disable the camera (privacy on)"}
    };
}

QStringList CameraController::supportedActions() const
{
    return commandActions();
}

bool CameraController::execute(const QString &action, const QStringList &args)
{
    Q_UNUSED(args);

    if (action == "toggle") {
        return toggle();
    } else if (action == "on") {
        return setPrivacy(false);
    } else if (action == "off") {
        return setPrivacy(true);
    }

    qWarning() << "Unknown camera action:" << action;
    return false;
}

QString CameraController::actionHelp(const QString &action) const
{
    return commandActionHelp().value(action);
}

bool CameraController::toggle()
{
    bool known = false;
    const bool privacy = currentPrivacy(&known);
    if (!known) {
        // No camera device to switch: the hotkey only drives hardware, so do
        // nothing rather than launching a camera application.
        return false;
    }
    return setPrivacy(!privacy);
}

bool CameraController::setPrivacy(bool privacy)
{
    bool applied = false;
    if (!requestPrivacy(privacy, &applied)) {
        return false;
    }

    if (!applied) {
        // No camera the daemon can switch at the hardware level: stay silent
        // and do not fall back to the camera application.
        return false;
    }

    showOSD(privacy ? "CameraOff" : "CameraOn");
    return true;
}


bool CameraController::currentPrivacy(bool *known) const
{
    if (known) {
        *known = false;
    }

    if (!m_daemonInterface || !m_daemonInterface->isValid()) {
        qWarning() << "Daemon1 interface not available";
        return false;
    }

    QDBusMessage reply = m_daemonInterface->call("GetCameraPrivacy");
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning() << "GetCameraPrivacy failed:" << reply.errorMessage();
        return false;
    }

    const QList<QVariant> values = reply.arguments();
    if (values.size() < 2) {
        qWarning() << "GetCameraPrivacy returned unexpected arguments:" << values.size();
        return false;
    }

    const bool privacy = values.at(0).toBool();
    const bool deviceKnown = values.at(1).toBool();
    if (known) {
        *known = deviceKnown;
    }
    return privacy;
}

bool CameraController::requestPrivacy(bool privacy, bool *applied)
{
    if (applied) {
        *applied = false;
    }

    if (!m_daemonInterface || !m_daemonInterface->isValid()) {
        qWarning() << "Daemon1 interface not available";
        return false;
    }

    QDBusReply<bool> reply = m_daemonInterface->call("SetCameraPrivacy", privacy);
    if (!reply.isValid()) {
        qWarning() << "SetCameraPrivacy failed:" << reply.error().message();
        return false;
    }

    if (applied) {
        *applied = reply.value();
    }
    qDebug() << "SetCameraPrivacy privacy:" << privacy << "applied:" << reply.value();
    return true;
}


void CameraController::showOSD(const QString &signal)
{
    QDBusInterface osdInterface(
        "org.deepin.dde.Osd1",
        "/org/deepin/dde/shell/osd",
        "org.deepin.dde.shell.osd",
        QDBusConnection::sessionBus()
    );

    if (osdInterface.isValid()) {
        osdInterface.call("ShowOSD", signal);
    } else {
        qWarning() << "Failed to connect to OSD interface";
    }
}
