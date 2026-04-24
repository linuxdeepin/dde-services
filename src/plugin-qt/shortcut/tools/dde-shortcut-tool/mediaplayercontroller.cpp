// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "mediaplayercontroller.h"

#include <QDebug>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QDBusConnectionInterface>

MediaPlayerController::MediaPlayerController(QObject *parent) 
    : BaseController(parent)
{
}

MediaPlayerController::~MediaPlayerController()
{
}

QStringList MediaPlayerController::supportedActions() const
{
    return QStringList{
        "play",
        "play-pause",
        "pause",
        "stop",
        "previous",
        "next",
        "rewind",
        "forward"
    };
}

bool MediaPlayerController::execute(const QString &action, const QStringList &args)
{
    Q_UNUSED(args);
    
    QString playerService = getActivePlayer();
    if (playerService.isEmpty()) {
        qDebug() << "No active media player found";
        return false;
    }
    
    QDBusInterface playerInterface(
        playerService,
        "/org/mpris/MediaPlayer2",
        "org.mpris.MediaPlayer2.Player",
        QDBusConnection::sessionBus()
    );
    
    if (!playerInterface.isValid()) {
        qWarning() << "Failed to connect to media player:" << playerInterface.lastError().message();
        return false;
    }
    
    qDebug() << "[HandlerAction] active player dest name:" << playerService;
    
    if (action == "play" || action == "play-pause") {
        return callPlayerMethod(playerInterface, "PlayPause");
    } else if (action == "pause") {
        return callPlayerMethod(playerInterface, "Pause");
    } else if (action == "stop") {
        return callPlayerMethod(playerInterface, "Stop");
    } else if (action == "previous") {
        return previousTrack(playerInterface);
    } else if (action == "next") {
        return nextTrack(playerInterface);
    } else if (action == "rewind") {
        return rewindPlayer(playerInterface);
    } else if (action == "forward") {
        return forwardPlayer(playerInterface);
    }
    
    qWarning() << "Unknown media action:" << action;
    return false;
}

QString MediaPlayerController::actionHelp(const QString &action) const
{
    static const QMap<QString, QString> helpMap = {
        {"play", "Play or toggle play/pause"},
        {"play-pause", "Toggle play/pause"},
        {"pause", "Pause playback"},
        {"stop", "Stop playback"},
        {"previous", "Go to previous track"},
        {"next", "Go to next track"},
        {"rewind", "Rewind 5 seconds"},
        {"forward", "Forward 5 seconds"}
    };
    return helpMap.value(action);
}

QString MediaPlayerController::getActivePlayer()
{
    // Get all MPRIS2 services
    QDBusConnectionInterface *interface = QDBusConnection::sessionBus().interface();
    if (!interface) {
        return QString();
    }
    
    QDBusReply<QStringList> reply = interface->registeredServiceNames();
    if (!reply.isValid()) {
        return QString();
    }
    
    QStringList services = reply.value();
    QStringList mprisServices;
    
    // Filter MPRIS2 services (Go version uses org.mpris.MediaPlayer2 prefix)
    for (const QString &service : services) {
        if (service.startsWith("org.mpris.MediaPlayer2")) {
            mprisServices.append(service);
        }
    }
    
    int length = mprisServices.size();
    if (length == 0) {
        return QString();
    }
    
    qDebug() << "Found" << length << "MPRIS players:" << mprisServices;
    
    // Go version selection logic
    for (const QString &service : mprisServices) {
        QDBusInterface playerInterface(
            service,
            "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player",
            QDBusConnection::sessionBus()
        );
        
        if (!playerInterface.isValid()) {
            continue;
        }
        
        // If only one player, return it directly
        if (length == 1) {
            qDebug() << "Using single player:" << service;
            return service;
        }
        
        // If two players and one is VLC, prefer VLC
        if (length == 2 && service.contains("vlc", Qt::CaseInsensitive)) {
            qDebug() << "Using VLC player:" << service;
            return service;
        }
        
        // Find currently playing player
        QString status = getPlaybackStatus(playerInterface);
        if (status == "Playing") {
            m_prevPlayer = service;
            qDebug() << "Found playing player:" << service;
            return service;
        }
        
        // If this is the previously used player
        if (m_prevPlayer == service) {
            qDebug() << "Using previous player:" << service;
            return service;
        }
    }
    
    // Default to first player
    qDebug() << "Using first available player:" << mprisServices.first();
    return mprisServices.first();
}

bool MediaPlayerController::callPlayerMethod(QDBusInterface &interface, const QString &method)
{
    QDBusReply<void> reply = interface.call(method);
    if (!reply.isValid()) {
        qWarning() << "Failed to call" << method << ":" << reply.error().message();
        return false;
    }
    
    qDebug() << "Called media player method:" << method;
    return true;
}

bool MediaPlayerController::seekPlayer(QDBusInterface &interface, qint64 offset)
{
    QDBusReply<void> reply = interface.call("Seek", offset);
    if (!reply.isValid()) {
        qWarning() << "Failed to seek:" << reply.error().message();
        return false;
    }
    
    qDebug() << "Seeked media player:" << offset;
    return true;
}

bool MediaPlayerController::previousTrack(QDBusInterface &interface)
{
    // Go version logic: Previous() + Play(), return if Previous fails
    QDBusReply<void> reply = interface.call("Previous");
    if (!reply.isValid()) {
        qWarning() << "Failed to call Previous:" << reply.error().message();
        return false;
    }
    qDebug() << "Called media player method: Previous";
    
    return callPlayerMethod(interface, "Play");
}

bool MediaPlayerController::nextTrack(QDBusInterface &interface)
{
    // Go version logic: Next() + Play(), return if Next fails
    QDBusReply<void> reply = interface.call("Next");
    if (!reply.isValid()) {
        qWarning() << "Failed to call Next:" << reply.error().message();
        return false;
    }
    qDebug() << "Called media player method: Next";
    
    return callPlayerMethod(interface, "Play");
}

bool MediaPlayerController::rewindPlayer(QDBusInterface &interface)
{
    // Go version logic: check current position, calculate offset
    const qint64 playerDelta = 5000 * 1000; // 5 seconds in microseconds
    
    // Get current position
    QVariant posVariant = interface.property("Position");
    if (!posVariant.isValid()) {
        qWarning() << "Failed to get current position";
        return false;
    }
    
    qint64 currentPos = posVariant.toLongLong();
    qint64 offset = 0;
    
    // Go version logic: only set negative offset when pos-playerDelta > 0
    if (currentPos - playerDelta > 0) {
        offset = -playerDelta;
    }
    // Note: In Go version, if pos-playerDelta <= 0, offset stays 0 (no seek)
    
    if (offset != 0) {
        if (!seekPlayer(interface, offset)) {
            return false;
        }
    }
    
    // Check playback status, play if not Playing
    QString status = getPlaybackStatus(interface);
    if (status.isEmpty()) {
        qWarning() << "Failed to get playback status";
        return false;
    }
    
    if (status != "Playing") {
        return callPlayerMethod(interface, "PlayPause");
    }
    
    return true;
}

bool MediaPlayerController::forwardPlayer(QDBusInterface &interface)
{
    // Go version logic: seek forward directly
    const qint64 playerDelta = 5000 * 1000; // 5 seconds in microseconds
    
    if (!seekPlayer(interface, playerDelta)) {
        return false;
    }
    
    // Check playback status, play if not Playing
    QString status = getPlaybackStatus(interface);
    if (status.isEmpty()) {
        qWarning() << "Failed to get playback status";
        return false;
    }
    
    if (status != "Playing") {
        return callPlayerMethod(interface, "PlayPause");
    }
    
    return true;
}

QString MediaPlayerController::getPlaybackStatus(QDBusInterface &interface)
{
    QVariant statusVariant = interface.property("PlaybackStatus");
    if (statusVariant.isValid()) {
        return statusVariant.toString();
    }
    return QString();
}
