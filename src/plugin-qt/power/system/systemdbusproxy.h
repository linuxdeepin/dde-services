// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>

class QDBusConnection;

class SystemDBusProxy : public QObject {
    Q_OBJECT
public:
    explicit SystemDBusProxy(QObject *parent = nullptr);

    QString chassis() const;
    bool lidIsPresent() const;
    bool lidIsClosed() const;
};
