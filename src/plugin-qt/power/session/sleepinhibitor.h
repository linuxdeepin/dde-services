// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>

class SleepInhibitor : public QObject {
    Q_OBJECT
public:
    explicit SleepInhibitor(QObject *parent = nullptr);
    ~SleepInhibitor() override;

    void block();
    void unblock();

Q_SIGNALS:
    void aboutToSleep();
    void wokeUp();

private Q_SLOTS:
    void handleSleep(bool beforeSleep);
    void onNameOwnerChanged(const QString &name, const QString &,
                            const QString &newOwner);

private:
    void inhibit();
    int m_fd = -1;
};
