// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QDBusObjectPath>
#include <QObject>
#include <functional>

class BatteryDevice : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.Power1.Battery")

    Q_PROPERTY(QString SysfsPath READ sysfsPath CONSTANT)
    Q_PROPERTY(bool IsPresent READ isPresent NOTIFY isPresentChanged)
    Q_PROPERTY(QString Manufacturer READ manufacturer NOTIFY manufacturerChanged)
    Q_PROPERTY(QString ModelName READ modelName NOTIFY modelNameChanged)
    Q_PROPERTY(QString SerialNumber READ serialNumber NOTIFY serialNumberChanged)
    Q_PROPERTY(QString Name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString Technology READ technology NOTIFY technologyChanged)
    Q_PROPERTY(double Energy READ energy NOTIFY energyChanged)
    Q_PROPERTY(double EnergyFull READ energyFull NOTIFY energyFullChanged)
    Q_PROPERTY(double EnergyFullDesign READ energyFullDesign NOTIFY energyFullDesignChanged)
    Q_PROPERTY(double EnergyRate READ energyRate NOTIFY energyRateChanged)
    Q_PROPERTY(double Voltage READ voltage NOTIFY voltageChanged)
    Q_PROPERTY(double Percentage READ percentage NOTIFY percentageChanged)
    Q_PROPERTY(double Capacity READ capacity NOTIFY capacityChanged)
    Q_PROPERTY(uint Status READ status NOTIFY statusChanged)
    Q_PROPERTY(quint64 TimeToEmpty READ timeToEmpty NOTIFY timeToEmptyChanged)
    Q_PROPERTY(quint64 TimeToFull READ timeToFull NOTIFY timeToFullChanged)
    Q_PROPERTY(qint64 UpdateTime READ updateTime NOTIFY updateTimeChanged)

public:
    explicit BatteryDevice(QString sysfsPath, QObject *parent = nullptr);

public Q_SLOTS:
    void Refresh();

private Q_SLOTS:
    void notifyPropertyChanged();

public:
    bool refresh();
    QDBusObjectPath objectPath() const;
    void setRefreshDoneCallback(std::function<void()> callback);
    void setStatus(uint status);

    QString sysfsPath() const { return m_sysfsPath; }
    bool isPresent() const { return m_present; }
    QString manufacturer() const { return m_manufacturer; }
    QString modelName() const { return m_modelName; }
    QString serialNumber() const { return m_serialNumber; }
    QString name() const { return m_name; }
    QString technology() const { return m_technology; }
    double energy() const { return m_energy; }
    double energyFull() const { return m_energyFull; }
    double energyFullDesign() const { return m_energyFullDesign; }
    double energyRate() const { return m_energyRate; }
    double voltage() const { return m_voltage; }
    double percentage() const { return m_percentage; }
    double capacity() const { return m_capacity; }
    uint status() const { return m_status; }
    quint64 timeToEmpty() const { return m_timeToEmpty; }
    quint64 timeToFull() const { return m_timeToFull; }
    qint64 updateTime() const { return m_updateTime; }

Q_SIGNALS:
    void isPresentChanged();
    void manufacturerChanged();
    void modelNameChanged();
    void serialNumberChanged();
    void nameChanged();
    void technologyChanged();
    void energyChanged();
    void energyFullChanged();
    void energyFullDesignChanged();
    void energyRateChanged();
    void voltageChanged();
    void percentageChanged();
    void capacityChanged();
    void statusChanged();
    void timeToEmptyChanged();
    void timeToFullChanged();
    void updateTimeChanged();

private:
    QString readText(const QString &name) const;
    double readScaled(const QString &name) const;

    QString m_sysfsPath;
    bool m_present = false;
    QString m_manufacturer;
    QString m_modelName;
    QString m_serialNumber;
    QString m_name;
    QString m_technology;
    double m_energy = 0;
    double m_energyFull = 0;
    double m_energyFullDesign = 0;
    double m_energyRate = 0;
    double m_voltage = 0;
    double m_percentage = 0;
    double m_capacity = 0;
    uint m_status = 0;
    quint64 m_timeToEmpty = 0;
    quint64 m_timeToFull = 0;
    qint64 m_updateTime = 0;
    std::function<void()> m_refreshDone;
};
