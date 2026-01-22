// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef AUDIOCONTROLLER_H
#define AUDIOCONTROLLER_H

#include "basecontroller.h"

class QDBusInterface;

class AudioController : public BaseController 
{
    Q_OBJECT

public:
    explicit AudioController(QObject *parent = nullptr);
    ~AudioController() override;

    // BaseController interface
    QString name() const override { return "audio"; }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;

    // Audio control methods
    bool toggleSinkMute();
    bool toggleSourceMute();
    bool changeSinkVolume(bool raised);

private:
    void showOSD(const QString &signal);

    // Helper functions: get default audio device
    QString getDefaultSinkPath();
    QString getDefaultSourcePath();

    QDBusInterface *m_audioInterface;
};

#endif // AUDIOCONTROLLER_H
