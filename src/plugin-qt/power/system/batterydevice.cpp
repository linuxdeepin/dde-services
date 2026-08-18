// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "batterydevice.h"
#include "../powerconstants.h"

#include <QFile>
#include <QFileInfo>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QMetaProperty>
#include <QVariantMap>

using namespace PowerDBus;

BatteryDevice::BatteryDevice(QString sysfsPath, QObject *parent)
    : QObject(parent)
    , m_sysfsPath(std::move(sysfsPath))
{
    refresh();

    const QMetaObject *mo = metaObject();
    const int slotIndex = mo->indexOfSlot("notifyPropertyChanged()");
    if (slotIndex >= 0) {
        const QMetaMethod slot = mo->method(slotIndex);
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QMetaProperty property = mo->property(i);
            if (property.hasNotifySignal())
                connect(this, property.notifySignal(), this, slot);
        }
    }
}
void BatteryDevice::Refresh()
{
    refresh();
    if (m_refreshDone)
        m_refreshDone();
}

void BatteryDevice::setRefreshDoneCallback(std::function<void()> callback)
{
    m_refreshDone = callback;
}

void BatteryDevice::setStatus(uint status)
{
    if (m_status != status) {
        m_status = status;
        Q_EMIT statusChanged();
    }
}


QString BatteryDevice::readText(const QString &name) const
{
    QFile file(m_sysfsPath + '/' + name);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

double BatteryDevice::readScaled(const QString &name) const
{
    bool ok = false;
    const double value = readText(name).toDouble(&ok);
    return ok ? value / 1000000.0 : 0.0;
}

bool BatteryDevice::refresh()
{
    const bool isBattery = readText("type").compare(
        QLatin1String("Battery"), Qt::CaseInsensitive) == 0;
    const QString present = isBattery ? readText(QStringLiteral("present")) : QString();
    if (!isBattery || (!present.isEmpty() && present == QLatin1String("0"))) {
        if (m_present) {
            m_present = false;
            Q_EMIT isPresentChanged();
        }
        return false;
    }

    double voltageDesign = readScaled("voltage_max_design");
    if (voltageDesign <= 1)
        voltageDesign = readScaled("voltage_min_design");
    if (voltageDesign <= 1)
        voltageDesign = readScaled("voltage_present");
    if (voltageDesign <= 1)
        voltageDesign = readScaled("voltage_now");
    if (voltageDesign <= 1)
        voltageDesign = 10;

    double energy = readScaled("energy_now");
    if (energy < 0.01)
        energy = readScaled("energy_avg");

    double energyFull = readScaled("energy_full");
    double energyFullDesign = readScaled("energy_full_design");
    if (energyFull < 0.01) {
        energyFull = readScaled("charge_full") * voltageDesign;
        energyFullDesign = readScaled("charge_full_design") * voltageDesign;
    }
    if (energyFull < 0.01 && energyFullDesign > 0.01)
        energyFull = energyFullDesign;

    double capacity = 100;
    if (energyFull > 0 && energyFullDesign > 0)
        capacity = qBound(0.0, energyFull * 100.0 / energyFullDesign, 100.0);

    double energyRate = qAbs(readScaled("power_now"));
    if (energyRate < 0.01) {
        if (energy < 0.01) {
            energy = readScaled("charge_now");
            if (energy < 0.01)
                energy = readScaled("charge_avg");
            energy *= voltageDesign;
        }
        const double chargeFull = qMax(readScaled("charge_full"),
                                       readScaled("charge_full_design"));
        energyRate = qAbs(readScaled("current_now"));
        if (chargeFull > 0)
            energyRate *= voltageDesign;
    }

    if (energy > energyFull)
        energyFull = energy;

    double voltage = readScaled("voltage_now");
    if (voltage < 0.01)
        voltage = readScaled("voltage_avg");
    if (energyRate > 100 || energy < 0.1)
        energyRate = 0;

    bool percentageValid = false;
    double percentage = readText("capacity").toDouble(&percentageValid);
    if (percentageValid) {
        percentage = qBound(0.0, percentage, 100.0);
        if (energy < 0.1 && energyFull > 0)
            energy = energyFull * percentage / 100.0;
    } else if (energyFull > 0) {
        percentage = qBound(0.0, energy * 100.0 / energyFull, 100.0);
    } else {
        percentage = 0;
    }

    const QString state = readText("status");
    uint status = 0;
    if (state.compare(QLatin1String("Charging"), Qt::CaseInsensitive) == 0)
        status = 1;
    else if (state.compare(QLatin1String("Discharging"), Qt::CaseInsensitive) == 0)
        status = 2;
    else if (state.compare(QLatin1String("Not charging"), Qt::CaseInsensitive) == 0)
        status = 3;
    else if (state.compare(QLatin1String("Full"), Qt::CaseInsensitive) == 0)
        status = 4;
    else if (state.compare(QLatin1String("FullCharging"), Qt::CaseInsensitive) == 0)
        status = 5;

    quint64 timeToEmpty = status == 2 && energyRate > 0
        ? static_cast<quint64>(3600.0 * energy / energyRate) : 0;
    quint64 timeToFull = status == 1 && energyRate > 0
        ? static_cast<quint64>(3600.0 * qMax(0.0, energyFull - energy) / energyRate) : 0;
    if (timeToEmpty > 240ULL * 60 * 60)
        timeToEmpty = 0;
    if (timeToFull > 20ULL * 60 * 60 || (status == 1 && m_status == 2))
        timeToFull = 0;

#define UPDATE(member, value, signal) \
    do { \
        const auto next = (value); \
        if (member != next) { member = next; Q_EMIT signal(); } \
    } while (false)

    UPDATE(m_present, readText("present") != QLatin1String("0"), isPresentChanged);
    UPDATE(m_manufacturer, readText("manufacturer"), manufacturerChanged);
    UPDATE(m_modelName, readText("model_name"), modelNameChanged);
    UPDATE(m_serialNumber, readText("serial_number"), serialNumberChanged);
    UPDATE(m_name, QFileInfo(m_sysfsPath).fileName(), nameChanged);
    UPDATE(m_technology, readText("technology"), technologyChanged);
    UPDATE(m_energy, energy, energyChanged);
    UPDATE(m_energyFull, energyFull, energyFullChanged);
    UPDATE(m_energyFullDesign, energyFullDesign, energyFullDesignChanged);
    UPDATE(m_energyRate, energyRate, energyRateChanged);
    UPDATE(m_voltage, voltage, voltageChanged);
    UPDATE(m_percentage, percentage, percentageChanged);
    UPDATE(m_capacity, capacity, capacityChanged);
    UPDATE(m_status, status, statusChanged);
    UPDATE(m_timeToEmpty, timeToEmpty, timeToEmptyChanged);
    UPDATE(m_timeToFull, timeToFull, timeToFullChanged);
    UPDATE(m_updateTime, QDateTime::currentSecsSinceEpoch(), updateTimeChanged);
#undef UPDATE
    return true;
}

void BatteryDevice::notifyPropertyChanged()
{
    const int signalIndex = senderSignalIndex();
    const QMetaObject *mo = metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty property = mo->property(i);
        if (!property.hasNotifySignal() || property.notifySignal().methodIndex() != signalIndex)
            continue;
        QDBusMessage message = QDBusMessage::createSignal(
            objectPath().path(), QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"));
        message << QStringLiteral("org.deepin.dde.Power1.Battery")
                << QVariantMap{{QString::fromLatin1(property.name()), property.read(this)}}
                << QStringList();
        QDBusConnection::systemBus().send(message);
        return;
    }
}

QDBusObjectPath BatteryDevice::objectPath() const
{
    // Preserve legacy object paths while encoding punctuation forbidden in D-Bus path
    // elements.
    QString name = QFileInfo(m_sysfsPath).fileName();
    name.replace('-', "_x0").replace('.', "_x1").replace(':', "_x2");
    return QDBusObjectPath(QString::fromLatin1(kPath) + "/battery_" + name);
}
