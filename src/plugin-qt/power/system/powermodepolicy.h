// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// Pure power-mode + low-battery decision logic, extracted from SystemPowerManager
// so the mode mapping and low-battery threshold are testable in isolation.

// Look up the DSPC config string for a logical power mode in the runtime mapping
// JSON ({ "<mode>": { "DSPCConfig": "..." }, ... }).
inline QString mappedDspcMode(const QString &mode, const QString &powerMappingConfig)
{
    const QJsonObject mapping = QJsonDocument::fromJson(powerMappingConfig.toUtf8()).object();
    const QJsonObject entry = mapping.value(mode).toObject();
    return entry.value(QStringLiteral("DSPCConfig")).toString();
}

// Resolve a logical mode to a concrete DSPC mode, applying the fallback chain and
// validating the result against the four accepted DSPC values.
inline QString resolveDspcMode(const QString &logicalMode, const QString &powerMappingConfig)
{
    QString dspc = mappedDspcMode(logicalMode, powerMappingConfig);
    if (dspc.isEmpty())
        dspc = logicalMode == QLatin1String("lowBattery") ? QStringLiteral("lowbat")
             : logicalMode == QLatin1String("powersave") ? QStringLiteral("saving")
             : logicalMode;
    if (dspc != QLatin1String("performance") && dspc != QLatin1String("balance")
        && dspc != QLatin1String("saving") && dspc != QLatin1String("lowbat")) {
        dspc = logicalMode == QLatin1String("lowBattery") ? QStringLiteral("lowbat")
             : logicalMode == QLatin1String("powersave") ? QStringLiteral("saving")
             : logicalMode;
    }
    return dspc;
}

// The logical mode applied when battery-low overrides a "powersave" request.
inline QString lowBatteryLogicalMode(bool batteryLow, const QString &mode)
{
    return batteryLow && mode == QLatin1String("powersave") ? QStringLiteral("lowBattery") : mode;
}

// Low-battery detection: only meaningful when a battery is present and the
// reported percentage is at or below the configured auto-battery threshold.
inline bool isBatteryLow(bool hasBattery, double percentage, double threshold)
{
    return hasBattery && percentage <= threshold;
}
