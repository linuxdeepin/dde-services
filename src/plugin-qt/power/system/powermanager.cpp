// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "powermanager.h"
#include "batterymanager.h"
#include "batterydevice.h"
#include "systemdbusproxy.h"
#include "../powerconstants.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QVariantMap>
#include <QMetaProperty>
#include <QProcess>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <DConfig>
#include <QLoggingCategory>

using namespace PowerDBus;
using namespace PowerFS;
using namespace PowerDConfig;

Q_LOGGING_CATEGORY(logPowerSystem, "dde.power.system")
static constexpr auto kLegacyDBusError = "org.deepin.dde.DBus.Error.Unnamed";

static bool readProcLidClosed(bool &closed)
{
    QFile file{QLatin1String(kLidStatePath)};
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray state = file.readAll();
    if (state.contains("closed")) {
        closed = true;
        return true;
    }
    if (state.contains("open")) {
        closed = false;
        return true;
    }
    return false;
}

static bool isValidPowerMode(const QString &mode)
{
    return mode == QLatin1String("balance")
        || mode == QLatin1String("powersave")
        || mode == QLatin1String("performance")
        || mode == QLatin1String("lowBattery");
}

SystemPowerManager::SystemPowerManager(QDBusConnection *conn, const QString &svc,
                                       QObject *p)
    : QObject(p)
    , m_conn(conn)
{
    Q_UNUSED(svc);
}

SystemPowerManager::~SystemPowerManager()
{
    if (m_configReader) {
        QMetaObject::invokeMethod(m_configReader, &QObject::deleteLater,
                                  Qt::BlockingQueuedConnection);
        m_configReader = nullptr;
    }
    for (auto *battery : std::as_const(m_batteries))
        m_conn->unregisterObject(battery->objectPath().path());
    m_conn->unregisterObject(kPath);
}

bool SystemPowerManager::initialize()
{
    bool ok = m_conn->registerObject(kPath, this,
        QDBusConnection::ExportAllSlots |
        QDBusConnection::ExportAllSignals |
        QDBusConnection::ExportAllProperties);
    if (!ok) {
        qWarning(logPowerSystem) << "registerObject failed";
        return false;
    }

    m_powerControlProcess = new QProcess(this);
    connect(m_powerControlProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exitCode != 0)
            qWarning(logPowerSystem) << "deepin-power-control failed:" << exitCode;
        runNextPowerControl();
    });
    connect(m_powerControlProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning(logPowerSystem) << "Failed to start deepin-power-control";
            QMetaObject::invokeMethod(this, &SystemPowerManager::runNextPowerControl,
                                      Qt::QueuedConnection);
        }
    });
    initLidSwitch();
    initPowerSavingDConfig();


    m_batteryManager = new BatteryManager(this, this);
    m_onBattery = m_batteryManager->onBattery();
    connect(m_batteryManager, &BatteryManager::onBatteryChanged, this, [this](bool onBatt) {
        if (m_onBattery == onBatt)
            return;
        m_onBattery = onBatt;
        Q_EMIT onBatteryChanged();
        recalcBatteryLow();
        updatePowerMode(false);
    });


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

    m_initDone = true;
    recalcBatteryLow();
    initializePowerMode();
    return true;
}
void SystemPowerManager::initializePowerMode()
{
    if (m_mode == QLatin1String("performance")) {
        updatePowerMode(true);
        return;
    }

    QDBusInterface displayManager(kDisplayManagerService, kDisplayManagerPath,
                                  kDisplayManagerIface, QDBusConnection::systemBus());
    const auto sessions = qdbus_cast<QList<QDBusObjectPath>>(
        displayManager.property("Sessions"));
    if (!sessions.isEmpty()) {
        updatePowerMode(true);
        return;
    }

    applyMode(QStringLiteral("performance"));
    if (!QDBusConnection::systemBus().connect(
            kDisplayManagerService, kDisplayManagerPath, kDisplayManagerIface,
            QStringLiteral("SessionAdded"), this,
            SLOT(onDisplaySessionAdded(QDBusObjectPath)))) {
        qWarning(logPowerSystem) << "Failed to watch display-manager sessions";
        updatePowerMode(true);
    }
}

void SystemPowerManager::onDisplaySessionAdded(const QDBusObjectPath &)
{
    QDBusConnection::systemBus().disconnect(
        kDisplayManagerService, kDisplayManagerPath, kDisplayManagerIface,
        QStringLiteral("SessionAdded"), this,
        SLOT(onDisplaySessionAdded(QDBusObjectPath)));
    updatePowerMode(true);
}

void SystemPowerManager::registerBattery(BatteryDevice *battery)
{
    if (!battery || m_batteries.contains(battery))
        return;
    if (!m_conn->registerObject(battery->objectPath().path(), battery,
                                QDBusConnection::ExportAllSlots |
                                QDBusConnection::ExportAllProperties)) {
        qWarning(logPowerSystem) << "Failed to register battery" << battery->objectPath().path();
        return;
    }
    m_batteries.append(battery);
    Q_EMIT BatteryAdded(battery->objectPath());
}

void SystemPowerManager::unregisterBattery(BatteryDevice *battery)
{
    if (!battery || !m_batteries.removeOne(battery))
        return;
    m_conn->unregisterObject(battery->objectPath().path());
    Q_EMIT BatteryRemoved(battery->objectPath());
}

void SystemPowerManager::initLidSwitch()
{
    SystemDBusProxy proxy;
    const QString chassis = proxy.chassis();
    if (chassis != QLatin1String("laptop") && chassis != QLatin1String("convertible"))
        return;

    QDBusInterface upower(kUPowerService, kUPowerPath, kUPowerService,
                          QDBusConnection::systemBus());
    const QVariant lidIsPresent = upower.property("LidIsPresent");
    const QVariant lidIsClosed = upower.property("LidIsClosed");
    const bool watchUPower = lidIsPresent.isValid() && lidIsClosed.isValid();
    if (watchUPower) {
        m_hasLidSwitch = lidIsPresent.toBool();
        m_lidClosed = lidIsClosed.toBool();
    } else {
        if (!readProcLidClosed(m_lidClosed))
            return;
        m_hasLidSwitch = true;
    }

    if (!m_hasLidSwitch)
        return;

    if (watchUPower) {
        QDBusConnection::systemBus().connect(
            kUPowerService, kUPowerPath,
            "org.freedesktop.DBus.Properties", "PropertiesChanged",
            this, SLOT(onUPowerPropertiesChanged(QString,QVariantMap,QStringList)));
        return;
    }

    // ponytail: poll the legacy proc fallback only when UPower is unavailable.
    auto *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, [this] {
        bool closed = false;
        if (!readProcLidClosed(closed) || m_lidClosed == closed)
            return;
        m_lidClosed = closed;
        Q_EMIT lidClosedChanged();
        handleLidSwitchEvent(closed);
    });
    timer->start();
}

void SystemPowerManager::onUPowerPropertiesChanged(const QString &interface,
                                                    const QVariantMap &changed,
                                                    const QStringList &)
{
    if (interface != QLatin1String(kUPowerService))
        return;

    if (changed.contains("LidIsClosed")) {
        bool closed = changed.value("LidIsClosed").toBool();
        if (m_lidClosed != closed) {
            m_lidClosed = closed;
            Q_EMIT lidClosedChanged();
        }
        handleLidSwitchEvent(closed);
    }
}

void SystemPowerManager::handleLidSwitchEvent(bool closed)
{
    qDebug(logPowerSystem) << "handleLidSwitchEvent: closed=" << closed;
    if (closed) {
        Q_EMIT LidClosed();
    } else {
        Q_EMIT LidOpened();
    }
}

void SystemPowerManager::initPowerSavingDConfig()
{
    m_config = Dtk::Core::DConfig::create(kAppId, kPowerName, "", this);
    if (!m_config) return;

    migrateLegacyConfig();

    m_configReader = Dtk::Core::DConfig::create(kAppId, kPowerName);
    if (m_configReader)
        m_configReader->moveToThread(Dtk::Core::DConfig::globalThread());

    m_powerMappingConfig = m_config->value(kPowerMappingConfig).toString();

    auto apply = [this](const QString &key, const QVariant &value) {
        if (key == QLatin1String(kPowerSavingModeEnabled))
            setPowerSavingModeEnabled(value.toBool());
        else if (key == QLatin1String(kPowerSavingModeAuto))
            setPowerSavingModeAuto(value.toBool());
        else if (key == QLatin1String(kPowerSavingModeAutoWhenBatteryLow))
            setPowerSavingModeAutoWhenBatteryLow(value.toBool());
        else if (key == QLatin1String(kPowerSavingModeBrightnessDropPercent))
            setPowerSavingModeBrightnessDropPercent(value.toUInt());
        else if (key == QLatin1String(kPowerSavingModeAutoBatteryPercent))
            setPowerSavingModeAutoBatteryPercent(value.toUInt());
        else if (key == QLatin1String(kShortIdleEnable))
            m_shortIdleEnabled = value.toBool();
        else if (key == QLatin1String(kMode)) {
            QString mode = value.toString();
            if (!isValidPowerMode(mode)) {
                mode = QStringLiteral("balance");
                m_config->setValue(QLatin1String(kMode), mode);
            }
            if (m_loadingConfig) {
                m_mode = mode;
                if (mode != QLatin1String("powersave"))
                    m_lastMode = mode;
            } else {
                if (m_mode == mode)
                    return;
                m_suppressModeUpdate = true;
                if (m_mode == QLatin1String("powersave")
                    || mode == QLatin1String("powersave")) {
                    setPowerSavingModeAuto(false);
                    setPowerSavingModeAutoWhenBatteryLow(false);
                }
                m_suppressModeUpdate = false;
                setMode(mode);
            }
        } else if (key == QLatin1String(kPowerMappingConfig)) {
            m_powerMappingConfig = value.toString();
        }
    };

    auto load = [this, apply](const char *key) {
        const QString configKey = QLatin1String(key);
        const QVariant value = m_config->value(configKey);
        qDebug(logPowerSystem) << "DConfig load:" << configKey << value;
        apply(configKey, value);
    };

    m_loadingConfig = true;
    m_applyingConfig = true;
    load(kPowerSavingModeEnabled);
    load(kMode);
    load(kPowerSavingModeAuto);
    load(kPowerSavingModeAutoWhenBatteryLow);
    load(kPowerSavingModeBrightnessDropPercent);
    load(kPowerSavingModeAutoBatteryPercent);
    load(kShortIdleEnable);
    load(kPowerMappingConfig);
    m_applyingConfig = false;
    m_loadingConfig = false;

    connect(m_config, &Dtk::Core::DConfig::valueChanged, this,
            [this, apply](const QString &key) {
        if (m_writingConfigKeys.contains(key))
            return;

        auto *reader = m_configReader;
        if (!reader)
            return;

        QMetaObject::invokeMethod(reader, [this, reader, key, apply] {
            const QVariant value = reader->value(key);
            QMetaObject::invokeMethod(this, [this, key, value, apply] {
                qDebug(logPowerSystem) << "DConfig value changed:" << key << value;
                m_applyingConfig = true;
                apply(key, value);
                m_applyingConfig = false;
            });
        });
    });
}

void SystemPowerManager::migrateLegacyConfig()
{
    const QString legacyPath = QStringLiteral("/var/lib/dde-daemon/power/config.json");
    QFileInfo fileInfo(legacyPath);
    if (!fileInfo.exists() || fileInfo.isSymLink())
        return;

    QFile file(legacyPath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning(logPowerSystem) << "Cannot migrate legacy power config:" << error.errorString();
        return;
    }

    bool migrated = true;
    auto setBool = [this, &migrated](const char *key, bool value) {
        const QString configKey = QLatin1String(key);
        m_config->setValue(configKey, value);
        migrated = migrated && (m_config->value(configKey).toBool() == value);
    };
    auto setUInt = [this, &migrated](const char *key, uint value) {
        const QString configKey = QLatin1String(key);
        m_config->setValue(configKey, value);
        migrated = migrated && (m_config->value(configKey).toUInt() == value);
    };
    auto setString = [this, &migrated](const char *key, const QString &value) {
        const QString configKey = QLatin1String(key);
        m_config->setValue(configKey, value);
        migrated = migrated && (m_config->value(configKey).toString() == value);
    };

    const QJsonObject config = document.object();
    if (config.contains(QStringLiteral("PowerSavingModeEnabled")))
        setBool(kPowerSavingModeEnabled,
                config.value(QStringLiteral("PowerSavingModeEnabled")).toBool());
    if (config.contains(QStringLiteral("PowerSavingModeAuto")))
        setBool(kPowerSavingModeAuto,
                config.value(QStringLiteral("PowerSavingModeAuto")).toBool());
    if (config.contains(QStringLiteral("PowerSavingModeAutoWhenBatteryLow")))
        setBool(kPowerSavingModeAutoWhenBatteryLow,
                config.value(QStringLiteral("PowerSavingModeAutoWhenBatteryLow")).toBool());
    if (config.contains(QStringLiteral("PowerSavingModeBrightnessDropPercent"))) {
        uint drop = config.value(QStringLiteral("PowerSavingModeBrightnessDropPercent")).toInt();
        if (drop == 0)
            drop = 20;
        setUInt(kPowerSavingModeBrightnessDropPercent, drop);
    }
    if (config.contains(QStringLiteral("PowerSavingModeAutoBatteryPercent"))) {
        uint percent = config.value(QStringLiteral("PowerSavingModeAutoBatteryPercent")).toInt();
        if (percent < 10)
            percent = 20;
        setUInt(kPowerSavingModeAutoBatteryPercent, percent);
    }
    if (config.contains(QStringLiteral("Mode"))) {
        QString mode = config.value(QStringLiteral("Mode")).toString();
        if (!isValidPowerMode(mode))
            mode = QStringLiteral("balance");
        setString(kMode, mode);
    }

    // This is a one-time migration, matching dde-daemon. Keeping the legacy file would
    // reapply stale values over DConfig on every service restart. If the DConfig backend
    // does not echo the migrated values, preserve the legacy file for the next startup.
    if (!migrated) {
        qWarning(logPowerSystem) << "DConfig verification failed; legacy power config kept";
        return;
    }

    // Move the source out of the legacy path instead of unlinking it. That preserves a
    // recovery copy and avoids treating a racy replacement as a file to delete.
    if (QFileInfo(legacyPath).isSymLink())
        return;
    if (!QFile::rename(legacyPath, legacyPath + QLatin1String(".migrated")))
        qWarning(logPowerSystem) << "Failed to archive migrated legacy power config";
}

void SystemPowerManager::enqueuePowerControl(const QStringList &arguments)
{
    m_powerControlQueue.enqueue(arguments);
    if (!m_powerControlBusy)
        runNextPowerControl();
}

void SystemPowerManager::runNextPowerControl()
{
    if (m_powerControlQueue.isEmpty()) {
        m_powerControlBusy = false;
        return;
    }
    m_powerControlBusy = true;
    m_powerControlProcess->start(QStringLiteral("/usr/sbin/deepin-power-control"),
                                 m_powerControlQueue.dequeue());
}


void SystemPowerManager::updateHasBattery(bool has)
{
    if (m_hasBattery != has) {
        m_hasBattery = has;
        Q_EMIT hasBatteryChanged();
    }
}

void SystemPowerManager::updateBatteryInfo(double pct, uint status,
                                            quint64 tte, quint64 ttf, double cap)
{
    if (m_batteryPercentage != pct) { 
        qWarning(logPowerSystem) << "batteryPercentage changed:" << m_batteryPercentage << "→" << pct;
        m_batteryPercentage = pct;
        Q_EMIT batteryPercentageChanged();
    }

    if (m_batteryStatus != status) {
        m_batteryStatus = status;
        Q_EMIT batteryStatusChanged();
    }

    if (m_batteryTimeToEmpty != tte)
    {
        m_batteryTimeToEmpty = tte;
        Q_EMIT batteryTimeToEmptyChanged();
    }

    if (m_batteryTimeToFull != ttf) { 
        m_batteryTimeToFull = ttf; 
        Q_EMIT batteryTimeToFullChanged(); 
    }
    if (m_batteryCapacity != cap) { 
        m_batteryCapacity = cap; 
        Q_EMIT batteryCapacityChanged(); 
    }
    const bool wasBatteryLow = m_batteryLow;
    recalcBatteryLow();
    if (wasBatteryLow != m_batteryLow)
        updatePowerMode(false);
    Q_EMIT BatteryDisplayUpdate(QDateTime::currentSecsSinceEpoch());
}

void SystemPowerManager::notifyPropertyChanged()
{
    const int signalIndex = senderSignalIndex();
    const QMetaObject *mo = metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty property = mo->property(i);
        if (!property.hasNotifySignal() || property.notifySignal().methodIndex() != signalIndex)
            continue;
        QDBusMessage message = QDBusMessage::createSignal(
            kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"));
        message << QLatin1String(kInterface)
                << QVariantMap{{QString::fromLatin1(property.name()), property.read(this)}}
                << QStringList();
        m_conn->send(message);
        return;
    }
}

QList<QDBusObjectPath> SystemPowerManager::GetBatteries()
{
    QList<QDBusObjectPath> paths;
    paths.reserve(m_batteries.size());
    for (const auto *battery : std::as_const(m_batteries))
        paths.append(battery->objectPath());
    return paths;
}

void SystemPowerManager::Refresh()
{
    RefreshMains();
    RefreshBatteries();
}

void SystemPowerManager::RefreshBatteries()
{
    if (m_batteryManager)
        m_batteryManager->refreshBatteries();
}

void SystemPowerManager::RefreshMains()
{
    if (m_batteryManager)
        m_batteryManager->refreshMains();
}

// This mirrors the session persistence helper while retaining the system plugin's
// own loading/applying guards and DConfig instance.
void SystemPowerManager::persist(const char *key, const QVariant &value)
{
    if (!m_config || m_loadingConfig)
        return;
    const QString configKey = QLatin1String(key);
    if (m_applyingConfig && m_config->value(configKey) == value)
        return;
    m_writingConfigKeys.insert(configKey);
    m_config->setValue(configKey, value);
    m_writingConfigKeys.remove(configKey);
}

QString SystemPowerManager::mappedDspcMode(const QString &mode) const
{
    // Parse on demand so runtime DConfig mapping changes take effect immediately; this
    // path is used only when a power mode is applied.
    const QJsonObject mapping = QJsonDocument::fromJson(m_powerMappingConfig.toUtf8()).object();
    const QJsonObject entry = mapping.value(mode).toObject();
    return entry.value(QStringLiteral("DSPCConfig")).toString();
}

void SystemPowerManager::applyMode(const QString &mode)
{
    const QString logicalMode = m_batteryLow && mode == QLatin1String("powersave")
        ? QStringLiteral("lowBattery") : mode;
    QString dspc = mappedDspcMode(logicalMode);
    if (dspc.isEmpty())
        dspc = logicalMode == QLatin1String("lowBattery") ? QStringLiteral("lowbat")
             : logicalMode == QLatin1String("powersave") ? QStringLiteral("saving")
             : logicalMode;
    if (dspc != QLatin1String("performance") && dspc != QLatin1String("balance")
        && dspc != QLatin1String("saving") && dspc != QLatin1String("lowbat")) {
        qWarning(logPowerSystem) << "Ignoring invalid DSPC mode mapping:" << dspc;
        dspc = logicalMode == QLatin1String("lowBattery") ? QStringLiteral("lowbat")
             : logicalMode == QLatin1String("powersave") ? QStringLiteral("saving")
             : logicalMode;
    }
    enqueuePowerControl({QStringLiteral("set"), dspc});
}

void SystemPowerManager::setMode(const QString &mode)
{
    if (!isValidPowerMode(mode)) {
        qWarning(logPowerSystem) << "Invalid power mode" << mode;
        return;
    }

    const bool powerSaving = mode == QLatin1String("powersave")
        || mode == QLatin1String("lowBattery");
    const QString exposedMode = mode == QLatin1String("lowBattery")
        ? QStringLiteral("powersave") : mode;

    applyMode(mode);
    const bool wasSettingMode = m_settingMode;
    m_settingMode = true;
    setPowerSavingModeEnabled(powerSaving);
    m_settingMode = wasSettingMode;
    if (!powerSaving)
        m_lastMode = mode;
    if (m_shortIdleState) {
        QTimer::singleShot(500, this, [this]() {
            if (m_shortIdleState)
                applyMode(QStringLiteral("powersave"));
        });
    }

    if (m_mode == exposedMode)
        return;
    m_mode = exposedMode;
    Q_EMIT modeChanged();
    persist(kMode, exposedMode);
}

void SystemPowerManager::setPowerSavingModeEnabled(bool value)
{
    if (m_psmEnabled == value)
        return;
    m_psmEnabled = value;
    Q_EMIT powerSavingModeEnabledChanged();
    persist(kPowerSavingModeEnabled, value);
    if (m_loadingConfig || m_settingMode)
        return;

    m_suppressModeUpdate = true;
    setPowerSavingModeAuto(false);
    setPowerSavingModeAutoWhenBatteryLow(false);
    m_suppressModeUpdate = false;
    setMode(value ? QStringLiteral("powersave") : QStringLiteral("balance"));
}

void SystemPowerManager::setPowerSavingModeAuto(bool value)
{
    if (m_psmAuto == value)
        return;
    m_psmAuto = value;
    Q_EMIT powerSavingModeAutoChanged();
    persist(kPowerSavingModeAuto, value);
    if (!m_loadingConfig && !m_suppressModeUpdate)
        updatePowerMode(false);
}

void SystemPowerManager::setPowerSavingModeAutoWhenBatteryLow(bool value)
{
    if (m_psmAutoLow == value)
        return;
    m_psmAutoLow = value;
    Q_EMIT powerSavingModeAutoWhenBatteryLowChanged();
    persist(kPowerSavingModeAutoWhenBatteryLow, value);
    recalcBatteryLow();
    if (!m_loadingConfig && !m_suppressModeUpdate)
        updatePowerMode(false);
}

void SystemPowerManager::setPowerSavingModeBrightnessDropPercent(uint value)
{
    if (m_psmDrop == value)
        return;
    m_psmDrop = value;
    Q_EMIT powerSavingModeBrightnessDropPercentChanged();
    persist(kPowerSavingModeBrightnessDropPercent, value);
}

void SystemPowerManager::setPowerSavingModeAutoBatteryPercent(uint value)
{
    if (m_psmAutoPct == value)
        return;
    m_psmAutoPct = value;
    Q_EMIT powerSavingModeAutoBatteryPercentChanged();
    persist(kPowerSavingModeAutoBatteryPercent, value);
    recalcBatteryLow();
    if (!m_loadingConfig && !m_suppressModeUpdate)
        updatePowerMode(false);
}

void SystemPowerManager::setPowerSavingModeBrightnessData(const QString &value)
{
    if (m_psmBrightnessData == value)
        return;
    m_psmBrightnessData = value;
    Q_EMIT powerSavingModeBrightnessDataChanged();
}

void SystemPowerManager::setSupportSwitchPowerMode(bool value)
{
    if (m_supportSwitchPowerMode == value)
        return;
    m_supportSwitchPowerMode = value;
    Q_EMIT supportSwitchPowerModeChanged();
}

void SystemPowerManager::SetCpuGovernor(const QString &gov)
{
    setCpuGovernor(gov);
}

void SystemPowerManager::SetCpuBoost(bool on)
{
    setCpuBoost(on);
}

void SystemPowerManager::LockCpuFreq(const QString &gov, int lockTime)
{
    Q_UNUSED(gov);
    Q_UNUSED(lockTime);
}

void SystemPowerManager::SetMode(const QString &mode)
{
    if (m_mode == mode) {
        const QString error = QStringLiteral("repeat set mode");
        if (calledFromDBus())
            sendErrorReply(QLatin1String(kLegacyDBusError), error);
        else
            qWarning(logPowerSystem) << error;
        return;
    }
    if (!isValidPowerMode(mode)) {
        const QString error = QStringLiteral("PowerMode \"%1\" mode is not supported").arg(mode);
        if (calledFromDBus())
            sendErrorReply(QLatin1String(kLegacyDBusError), error);
        else
            qWarning(logPowerSystem) << error;
        return;
    }

    m_suppressModeUpdate = true;
    if (m_mode == QLatin1String("powersave") || mode == QLatin1String("powersave")) {
        setPowerSavingModeAuto(false);
        setPowerSavingModeAutoWhenBatteryLow(false);
    }
    m_suppressModeUpdate = false;
    setMode(mode);
}

void SystemPowerManager::SetTlpMode(const QString &mode)
{
    QString error;
    if (m_tlpMode == mode)
        error = QStringLiteral("repeat set tlp mode");
    else if (!isValidPowerMode(mode))
        error = QStringLiteral("PowerMode \"%1\" mode is not supported").arg(mode);
    if (!error.isEmpty()) {
        if (calledFromDBus())
            sendErrorReply(QLatin1String(kLegacyDBusError), error);
        else
            qWarning(logPowerSystem) << error;
        return;
    }

    m_tlpMode = mode;
    Q_EMIT tlpModeChanged();
    applyMode(mode);
}

void SystemPowerManager::SetShortIdleState(bool state)
{
    qInfo(logPowerSystem) << "SetShortIdleState:" << state;
    if (!m_shortIdleEnabled) {
        qInfo(logPowerSystem) << "Short idle is disabled by DConfig";
        return;
    }
    if (m_shortIdleState == state) {
        qInfo(logPowerSystem) << "Short idle state is unchanged:" << state;
        return;
    }

    m_shortIdleState = state;
    Q_EMIT shortIdleStateChanged();
    persist(kShortIdleState, state);

    const QString powerState = state ? QStringLiteral("powersave") : m_mode;
    applyMode(powerState);
    enqueuePowerControl({QStringLiteral("idle"), QStringLiteral("wifi"),
                         state ? QStringLiteral("on") : QStringLiteral("off")});
}

void SystemPowerManager::recalcBatteryLow()
{
    bool old = m_batteryLow;
    // Legacy dde-daemon tracked low battery from capacity only. AC state gates the
    // general battery-power auto switch; the low-battery auto switch is independent.
    m_batteryLow = m_hasBattery
                   && m_batteryPercentage <= static_cast<double>(m_psmAutoPct);
    qDebug(logPowerSystem) << "recalcBatteryLow:" << old << "→" << m_batteryLow
                             << "(HasBattery=" << m_hasBattery
                             << " pct=" << m_batteryPercentage
                             << " threshold=" << m_psmAutoPct << ")";
}

void SystemPowerManager::updatePowerMode(bool init)
{
    if (!m_initDone)
        return;

    bool enablePowerSave = m_psmAuto && m_onBattery;
    bool enableLowPower = m_psmAutoLow && m_batteryLow;

    qDebug(logPowerSystem) << "updatePowerMode: init=" << init
                             << " PSMAuto=" << m_psmAuto
                             << " OnBattery=" << m_onBattery
                             << " PSMAutoLow=" << m_psmAutoLow
                             << " BatteryLow=" << m_batteryLow
                             << " currentMode=" << m_mode
                             << " lastMode=" << m_lastMode;

    // When both automatic switches are disabled, preserve the user's current mode.
    // dde-daemon returned here instead of forcing a rollback to lastMode.
    if (!m_psmAuto && !m_psmAutoLow && !init)
        return;

    QString target = init ? m_mode : m_lastMode;
    if (enablePowerSave || enableLowPower)
        target = "powersave";
    setMode(target);
}
