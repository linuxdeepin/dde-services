// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "audiocontroller.h"

#include <QDebug>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>

AudioController::AudioController(QObject *parent)
    : BaseController(parent)
    , m_audioInterface(nullptr)
{
    // Connect to audio service
    m_audioInterface = new QDBusInterface(
        "org.deepin.dde.Audio1",
        "/org/deepin/dde/Audio1",
        "org.deepin.dde.Audio1",
        QDBusConnection::sessionBus(),
        this
    );
    
    if (!m_audioInterface->isValid()) {
        qWarning() << "Failed to connect to Audio service:" 
                   << m_audioInterface->lastError().message();
    }
}

AudioController::~AudioController()
{
    if (m_audioInterface) {
        delete m_audioInterface;
    }
}

QStringList AudioController::supportedActions() const
{
    return QStringList{
        "mute-toggle",
        "volume-up",
        "volume-down",
        "mic-mute-toggle"
    };
}

bool AudioController::execute(const QString &action, const QStringList &args)
{
    Q_UNUSED(args);
    
    if (action == "mute-toggle") {
        return toggleSinkMute();
    } else if (action == "volume-up") {
        return changeSinkVolume(true);
    } else if (action == "volume-down") {
        return changeSinkVolume(false);
    } else if (action == "mic-mute-toggle") {
        return toggleSourceMute();
    }
    
    qWarning() << "Unknown audio action:" << action;
    return false;
}

QString AudioController::actionHelp(const QString &action) const
{
    static const QMap<QString, QString> helpMap = {
        {"mute-toggle", "Toggle speaker mute"},
        {"volume-up", "Increase speaker volume"},
        {"volume-down", "Decrease speaker volume"},
        {"mic-mute-toggle", "Toggle microphone mute"}
    };
    return helpMap.value(action);
}

bool AudioController::toggleSinkMute()
{
    if (!m_audioInterface || !m_audioInterface->isValid()) {
        qWarning() << "Audio interface not available";
        return false;
    }
    
    // Get default Sink path
    QString sinkPathStr = getDefaultSinkPath();
    if (sinkPathStr.isEmpty()) {
        return false;
    }
    
    // Create Sink interface
    QDBusInterface sinkInterface(
        "org.deepin.dde.Audio1",
        sinkPathStr,
        "org.deepin.dde.Audio1.Sink",
        QDBusConnection::sessionBus()
    );
    
    // Get current mute state
    QVariant muteVariant = sinkInterface.property("Mute");
    if (!muteVariant.isValid()) {
        qWarning() << "Failed to get mute property";
        return false;
    }
    
    bool currentMute = muteVariant.toBool();
    
    // Toggle mute state
    QDBusReply<void> reply = sinkInterface.call("SetMute", !currentMute);
    if (!reply.isValid()) {
        qWarning() << "Failed to set mute:" << reply.error().message();
        return false;
    }
    
    qDebug() << "Toggled sink mute:" << !currentMute;
    showOSD("AudioMute");
    
    return true;
}

bool AudioController::toggleSourceMute()
{
    if (!m_audioInterface || !m_audioInterface->isValid()) {
        qWarning() << "Audio interface not available";
        return false;
    }
    
    // Get default Source path
    QString sourcePathStr = getDefaultSourcePath();
    if (sourcePathStr.isEmpty()) {
        return false;
    }
    
    // Create Source interface
    QDBusInterface sourceInterface(
        "org.deepin.dde.Audio1",
        sourcePathStr,
        "org.deepin.dde.Audio1.Source",
        QDBusConnection::sessionBus()
    );
    
    // Get current mute state
    QVariant muteVariant = sourceInterface.property("Mute");
    if (!muteVariant.isValid()) {
        qWarning() << "Failed to get mute property";
        return false;
    }
    
    bool currentMute = muteVariant.toBool();
    
    // Toggle mute state
    QDBusReply<void> reply = sourceInterface.call("SetMute", !currentMute);
    if (!reply.isValid()) {
        qWarning() << "Failed to set source mute:" << reply.error().message();
        return false;
    }
    
    qDebug() << "Toggled source mute:" << !currentMute;
    showOSD(currentMute ? "AudioMicMuteOff" : "AudioMicMuteOn");
    
    return true;
}

bool AudioController::changeSinkVolume(bool raised)
{
    if (!m_audioInterface || !m_audioInterface->isValid()) {
        qWarning() << "Audio interface not available";
        return false;
    }
    
    // Get default Sink path
    QString sinkPathStr = getDefaultSinkPath();
    if (sinkPathStr.isEmpty()) {
        return false;
    }
    
    // Create Sink interface
    QDBusInterface sinkInterface(
        "org.deepin.dde.Audio1",
        sinkPathStr,
        "org.deepin.dde.Audio1.Sink",
        QDBusConnection::sessionBus()
    );
    
    // Get current volume
    QVariant volumeVariant = sinkInterface.property("Volume");
    if (!volumeVariant.isValid()) {
        qWarning() << "Failed to get volume property";
        return false;
    }
    
    double currentVolume = volumeVariant.toDouble();
    double step = 0.05;
    double newVolume = currentVolume + (raised ? step : -step);
    
    // Clamp volume range
    if (newVolume < 0.0) newVolume = 0.0;
    if (newVolume > 1.5) newVolume = 1.5;
    
    // If currently muted, unmute
    QVariant muteVariant = sinkInterface.property("Mute");
    if (muteVariant.isValid() && muteVariant.toBool()) {
        sinkInterface.call("SetMute", false);
    }
    
    // Set new volume (value, isPlay)
    QDBusReply<void> reply = sinkInterface.call("SetVolume", newVolume, true);
    if (!reply.isValid()) {
        qWarning() << "Failed to set volume:" << reply.error().message();
        return false;
    }
    
    qDebug() << "Changed sink volume:" << currentVolume << "->" << newVolume;
    showOSD(raised ? "AudioUp" : "AudioDown");
    
    return true;
}

void AudioController::showOSD(const QString &signal)
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

QString AudioController::getDefaultSinkPath()
{
    QVariant sinkVariant = m_audioInterface->property("DefaultSink");
    if (!sinkVariant.isValid()) {
        qWarning() << "Failed to get DefaultSink property";
        return QString();
    }
    
    QDBusObjectPath sinkPath = sinkVariant.value<QDBusObjectPath>();
    if (sinkPath.path().isEmpty()) {
        qWarning() << "DefaultSink path is empty";
        return QString();
    }
    
    return sinkPath.path();
}

QString AudioController::getDefaultSourcePath()
{
    QVariant sourceVariant = m_audioInterface->property("DefaultSource");
    if (!sourceVariant.isValid()) {
        qWarning() << "Failed to get DefaultSource property";
        return QString();
    }
    
    QDBusObjectPath sourcePath = sourceVariant.value<QDBusObjectPath>();
    if (sourcePath.path().isEmpty()) {
        qWarning() << "DefaultSource path is empty";
        return QString();
    }
    
    return sourcePath.path();
}
