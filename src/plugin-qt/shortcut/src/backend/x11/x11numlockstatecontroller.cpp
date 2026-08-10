// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "x11numlockstatecontroller.h"

#include "backend/abstractkeyhandler.h"
#include "shortcutlogging.h"

#include <DConfig>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QVariant>

namespace {
constexpr const char *kKeyboardConfigAppId = "org.deepin.dde.keybinding";
constexpr const char *kKeyboardConfigName = "org.deepin.dde.keybinding.keyboard";
constexpr const char *kLegacyKeyboardConfigAppId = "org.deepin.dde.daemon";
constexpr const char *kLegacyKeyboardConfigName = "org.deepin.dde.daemon.keyboard";
constexpr const char *kSaveNumLockStateKey = "saveNumlockState";
constexpr const char *kNumLockStateKey = "numlockState";

constexpr const char *kPowerService = "org.deepin.dde.Power1";
constexpr const char *kPowerPath = "/org/deepin/dde/Power1";
constexpr const char *kPowerInterface = "org.deepin.dde.Power1";
constexpr const char *kHasBatteryProperty = "HasBattery";

constexpr uint kNumLockOff = 0;
constexpr uint kNumLockOn = 1;
constexpr uint kNumLockUnknown = 2;
}

X11NumLockStateController::X11NumLockStateController(AbstractKeyHandler *keyHandler,
                                                     QObject *parent)
    : QObject(parent)
    , m_keyHandler(keyHandler)
{
    if (!m_keyHandler) {
        qCWarning(logShortcut) << "X11NumLockStateController: key handler is unavailable";
        return;
    }

    m_lastState = state();
    connect(m_keyHandler, &AbstractKeyHandler::numLockStateChanged,
            this, &X11NumLockStateController::updateState);

    m_keyboardConfig = Dtk::Core::DConfig::create(
            QString::fromLatin1(kKeyboardConfigAppId),
            QString::fromLatin1(kKeyboardConfigName),
            QString(), this);
    if (!m_keyboardConfig || !m_keyboardConfig->isValid()) {
        qCWarning(logShortcut) << "X11NumLockStateController: keyboard config is unavailable";
        return;
    }

    migrateLegacyState();
    restoreState();
}

uint X11NumLockStateController::state() const
{
    return m_keyHandler && m_keyHandler->getNumLockState() ? kNumLockOn : kNumLockOff;
}

void X11NumLockStateController::setState(uint state)
{
    if (!m_keyHandler)
        return;

    const uint desiredState = state == kNumLockOff ? kNumLockOff : kNumLockOn;
    m_keyHandler->setNumLockState(desiredState == kNumLockOn);

    const uint actualState = this->state();
    if (actualState != desiredState) {
        qCWarning(logShortcut) << "X11NumLockStateController: failed to set NumLock state to"
                               << desiredState << "; actual state is" << actualState;
    }
    updateState(actualState == kNumLockOn);
}

void X11NumLockStateController::migrateLegacyState()
{
    const QString stateKey = QString::fromLatin1(kNumLockStateKey);
    const QString saveStateKey = QString::fromLatin1(kSaveNumLockStateKey);
    bool ok = false;
    const uint configuredState = m_keyboardConfig->value(stateKey).toUInt(&ok);
    if (!ok || configuredState != kNumLockUnknown)
        return;

    auto *legacyConfig = Dtk::Core::DConfig::create(
            QString::fromLatin1(kLegacyKeyboardConfigAppId),
            QString::fromLatin1(kLegacyKeyboardConfigName),
            QString(), this);
    if (!legacyConfig || !legacyConfig->isValid()) {
        if (legacyConfig)
            legacyConfig->deleteLater();
        return;
    }

    bool migrated = false;
    const QVariant legacySaveState = legacyConfig->value(saveStateKey);
    if (legacySaveState.isValid()) {
        m_keyboardConfig->setValue(saveStateKey, legacySaveState.toBool());
        migrated = true;
    }

    const uint legacyState = legacyConfig->value(stateKey).toUInt(&ok);
    if (ok && (legacyState == kNumLockOff || legacyState == kNumLockOn)) {
        m_keyboardConfig->setValue(stateKey, static_cast<int>(legacyState));
        migrated = true;
    }
    if (migrated)
        qCInfo(logShortcut) << "X11NumLockStateController: migrated legacy NumLock config";
    legacyConfig->deleteLater();
}

void X11NumLockStateController::restoreState()
{
    const QString key = QString::fromLatin1(kNumLockStateKey);
    const QVariant configuredValue = m_keyboardConfig->value(key);
    bool ok = false;
    const uint configuredState = configuredValue.toUInt(&ok);
    const bool saveState = shouldSaveState();

    uint desiredState = configuredState;
    if (ok && configuredState == kNumLockUnknown) {
        const bool laptop = hasBattery();
        desiredState = laptop ? kNumLockOff : kNumLockOn;
        qCInfo(logShortcut) << "X11NumLockStateController: no saved NumLock state; using"
                            << (laptop ? "laptop default (off)" : "desktop default (on)");
        if (saveState)
            persistState(desiredState);
    } else if (ok && (configuredState == kNumLockOff || configuredState == kNumLockOn)) {
        if (!saveState) {
            qCDebug(logShortcut) << "X11NumLockStateController: state persistence is disabled";
            return;
        }
        qCInfo(logShortcut) << "X11NumLockStateController: restoring NumLock state:"
                            << desiredState;
    } else {
        qCWarning(logShortcut) << "X11NumLockStateController: invalid saved NumLock state:"
                               << configuredValue;
        return;
    }

    m_keyHandler->setNumLockState(desiredState == kNumLockOn);
    const uint actualState = state();
    if (actualState != desiredState) {
        qCWarning(logShortcut) << "X11NumLockStateController: failed to restore NumLock state to"
                               << desiredState << "; actual state is" << actualState;
    }
    m_lastState = actualState;
}

void X11NumLockStateController::updateState(bool on)
{
    const uint state = on ? kNumLockOn : kNumLockOff;
    if (m_lastState == state)
        return;

    m_lastState = state;
    persistState(state);
    emit stateChanged(on);
}

void X11NumLockStateController::persistState(uint state)
{
    if (!shouldSaveState())
        return;

    const uint normalizedState = state == kNumLockOn ? kNumLockOn : kNumLockOff;
    const QString key = QString::fromLatin1(kNumLockStateKey);
    bool ok = false;
    const uint savedState = m_keyboardConfig->value(key).toUInt(&ok);
    if (ok && savedState == normalizedState)
        return;

    m_keyboardConfig->setValue(key, static_cast<int>(normalizedState));
}

bool X11NumLockStateController::shouldSaveState() const
{
    return m_keyboardConfig && m_keyboardConfig->isValid()
            && m_keyboardConfig->value(QString::fromLatin1(kSaveNumLockStateKey)).toBool();
}

bool X11NumLockStateController::hasBattery() const
{
    // Keep the legacy dde-daemon fallback: a failed battery query is treated
    // as no battery, so the unknown NumLock state uses the desktop default.
    QDBusInterface power(QString::fromLatin1(kPowerService),
                         QString::fromLatin1(kPowerPath),
                         QString::fromLatin1(kPowerInterface),
                         QDBusConnection::systemBus());
    if (!power.isValid()) {
        qCWarning(logShortcut) << "X11NumLockStateController: failed to query HasBattery:"
                               << power.lastError().message()
                               << "; preserving legacy fallback and treating the device as having no battery";
        return false;
    }

    const QVariant value = power.property(kHasBatteryProperty);
    if (!value.isValid()) {
        qCWarning(logShortcut) << "X11NumLockStateController: HasBattery property is unavailable;"
                                  " preserving legacy fallback and treating the device as having no battery";
        return false;
    }
    return value.toBool();
}
