// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>

class AbstractKeyHandler;

namespace Dtk::Core {
class DConfig;
}

class X11NumLockStateController : public QObject
{
    Q_OBJECT

public:
    explicit X11NumLockStateController(AbstractKeyHandler *keyHandler,
                                       QObject *parent = nullptr);

    uint state() const;
    void setState(uint state);

signals:
    void stateChanged(bool on);

private:
    void migrateLegacyState();
    void restoreState();
    void updateState(bool on);
    void persistState(uint state);
    bool shouldSaveState() const;
    bool hasBattery() const;

    AbstractKeyHandler *m_keyHandler = nullptr;
    Dtk::Core::DConfig *m_keyboardConfig = nullptr;
    uint m_lastState = 0;
};
