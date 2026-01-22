// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef MEDIAPLAYERCONTROLLER_H
#define MEDIAPLAYERCONTROLLER_H

#include "basecontroller.h"

class QDBusInterface;

class MediaPlayerController : public BaseController 
{
    Q_OBJECT
public:
    explicit MediaPlayerController(QObject *parent = nullptr);
    ~MediaPlayerController() override;
    
    // BaseController interface
    QString name() const override { return "media"; }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;
    
private:
    QString getActivePlayer();
    bool callPlayerMethod(QDBusInterface &interface, const QString &method);
    bool seekPlayer(QDBusInterface &interface, qint64 offset);
    bool previousTrack(QDBusInterface &interface);
    bool nextTrack(QDBusInterface &interface);
    bool rewindPlayer(QDBusInterface &interface);
    bool forwardPlayer(QDBusInterface &interface);
    QString getPlaybackStatus(QDBusInterface &interface);
    
    QString m_prevPlayer; // Remember the last used player
};

#endif
