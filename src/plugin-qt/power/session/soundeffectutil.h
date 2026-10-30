// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef SOUNDEFFECTUTIL_H
#define SOUNDEFFECTUTIL_H

#include <QString>
#include <QDBusMessage>
#include <QDBusConnection>

namespace SoundEffect {

inline void playSystemSound(const QString &soundEvent)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.deepin.dde.SoundEffect1"),
        QStringLiteral("/org/deepin/dde/SoundEffect1"),
        QStringLiteral("org.deepin.dde.SoundEffect1"),
        QStringLiteral("PlaySound"));
    msg << soundEvent;
    QDBusConnection::sessionBus().asyncCall(msg);
}

} // namespace SoundEffect

#endif // SOUNDEFFECTUTIL_H
