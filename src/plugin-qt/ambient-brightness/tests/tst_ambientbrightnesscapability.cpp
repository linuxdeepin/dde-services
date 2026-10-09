// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Supported 是"本机是否具备环境光传感器"的能力事实，与开关、盖子、休眠、
// 会话状态无关。本用例自建一条私有 D-Bus 充当 system bus，并模拟
// iio-sensor-proxy，验证该不变量。

#include "ambientbrightnessservice.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QProcess>
#include <QSignalSpy>

#include <signal.h>
#include <QTest>

using namespace dde::ambient_brightness;

namespace {

constexpr auto kSensorService = "net.hadess.SensorProxy";
constexpr auto kSensorPath = "/net/hadess/SensorProxy";
constexpr auto kSensorInterface = "net.hadess.SensorProxy";
constexpr auto kSensorObjectPath = "/net/hadess/SensorProxy/ambient_light";

// 模拟 iio-sensor-proxy。作为 service name owner 注册，
// 并提供 LookupAmbientLightSensor 供异步能力探测使用。
class StubSensorProxy : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.hadess.SensorProxy")
    Q_PROPERTY(bool HasAmbientLight READ hasAmbientLight)
    Q_PROPERTY(QString LightLevelUnit READ lightLevelUnit)
    Q_PROPERTY(double LightLevel READ lightLevel)

public:
    StubSensorProxy(bool hasAmbientLight, QString unit, QObject *parent = nullptr)
        : QObject(parent)
        , m_hasAmbientLight(hasAmbientLight)
        , m_unit(std::move(unit))
    {
    }

    ~StubSensorProxy() override
    {
        if (m_published) {
            m_bus.unregisterObject(QString::fromLatin1(kSensorPath));
            m_bus.unregisterService(QString::fromLatin1(kSensorService));
        }
    }

    bool hasAmbientLight() const { return m_hasAmbientLight; }
    QString lightLevelUnit() const { return m_unit; }
    double lightLevel() const { return 42.0; }

    bool publish(QDBusConnection bus)
    {
        if (!bus.registerService(QString::fromLatin1(kSensorService)))
            return false;
        if (!bus.registerObject(QString::fromLatin1(kSensorPath), this,
                                QDBusConnection::ExportAllProperties
                                    | QDBusConnection::ExportAllSlots))
            return false;
        m_bus = bus;
        m_published = true;
        return true;
    }

    int claimCount() const { return m_claimCount; }

public Q_SLOTS:
    QDBusObjectPath LookupAmbientLightSensor()
    {
        return QDBusObjectPath(QString::fromLatin1(kSensorObjectPath));
    }
    void ClaimLight() { ++m_claimCount; }
    void ReleaseLight() {}

private:
    QDBusConnection m_bus = QDBusConnection::systemBus();
    bool m_published = false;
    bool m_hasAmbientLight = false;
    QString m_unit;
    int m_claimCount = 0;
};

} // namespace

class AmbientBrightnessCapabilityTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void supportedFollowsHardwareNotSwitch();
    void supportedStaysTrueWhileRunning();
    void supportedFalseWhenSensorAbsent();
    void supportedFalseWhenUnitNotLux();
    void supportedNotWrittenBySamplingStop();

private:
    QProcess *m_busDaemon = nullptr;
    qint64 m_busDaemonPid = 0;
};

// 自建私有总线并伪装成 system bus：测试因此不依赖宿主机是否运行
// iio-sensor-proxy，也不需要手工导出环境变量即可由 ctest 运行。
void AmbientBrightnessCapabilityTest::initTestCase()
{
    QLoggingCategory::setFilterRules(QStringLiteral("dde.ambientbrightness=false"));

    m_busDaemon = new QProcess(this);
    m_busDaemon->start(QStringLiteral("dbus-daemon"),
                       { QStringLiteral("--session"), QStringLiteral("--print-address=1"),
                         QStringLiteral("--print-pid=1"), QStringLiteral("--fork") });
    QVERIFY(m_busDaemon->waitForFinished(5000));
    const QString output = QString::fromUtf8(m_busDaemon->readAllStandardOutput());
    const QString address = output.section(QLatin1Char('\n'), 0, 0).trimmed();
    m_busDaemonPid = output.section(QLatin1Char('\n'), 1, 1).trimmed().toLongLong();
    QVERIFY(!address.isEmpty());
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", address.toUtf8());
    QVERIFY(QDBusConnection::systemBus().isConnected());
}

void AmbientBrightnessCapabilityTest::cleanupTestCase()
{
    if (m_busDaemonPid > 0)
        ::kill(static_cast<pid_t>(m_busDaemonPid), SIGTERM);
}

void AmbientBrightnessCapabilityTest::supportedFollowsHardwareNotSwitch()
{
    StubSensorProxy proxy(true, QStringLiteral("lux"));
    QVERIFY(proxy.publish(QDBusConnection::systemBus()));

    AmbientBrightnessService service(QDBusConnection::systemBus());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);
    // 冷启动前就把开关置为关闭：这正是原先 Supported 被误报为 false 的场景。
    QVERIFY(QMetaObject::invokeMethod(&service, "onAutomaticBrightnessEnabledChanged",
                                      Qt::DirectConnection, Q_ARG(bool, false)));
    QVERIFY(service.initialize());

    // 异步探测返回"硬件存在"后，Supported 必须为 true。
    QTRY_COMPARE_WITH_TIMEOUT(supportedSpy.count(), 1, 5000);
    QVERIFY(service.supported());
    QVERIFY(!service.enabled());
    QCOMPARE(service.state(), QStringLiteral("Disabled"));
    QCOMPARE(proxy.claimCount(), 0);
}

void AmbientBrightnessCapabilityTest::supportedStaysTrueWhileRunning()
{
    StubSensorProxy proxy(true, QStringLiteral("lux"));
    QVERIFY(proxy.publish(QDBusConnection::systemBus()));

    AmbientBrightnessService service(QDBusConnection::systemBus());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);
    QVERIFY(service.initialize());

    QTRY_COMPARE_WITH_TIMEOUT(supportedSpy.count(), 1, 5000);
    QVERIFY(service.supported());
    // 开关默认开启：探测通过后应真实 Claim 传感器。
    QTRY_COMPARE_WITH_TIMEOUT(proxy.claimCount(), 1, 5000);
    QCOMPARE(service.state(), QStringLiteral("WaitingForSample"));

    // 运行中关闭开关：释放 Claim，但能力事实不变。
    QVERIFY(QMetaObject::invokeMethod(&service, "onAutomaticBrightnessEnabledChanged",
                                      Qt::DirectConnection, Q_ARG(bool, false)));
    QVERIFY(service.supported());
    QCOMPARE(service.state(), QStringLiteral("Disabled"));
    QCOMPARE(supportedSpy.count(), 1);
}

void AmbientBrightnessCapabilityTest::supportedFalseWhenSensorAbsent()
{
    // 没有 iio-sensor-proxy：能力探测应判定为不支持。
    AmbientBrightnessService service(QDBusConnection::systemBus());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);
    QVERIFY(service.initialize());

    QTRY_COMPARE_WITH_TIMEOUT(service.state(), QStringLiteral("Unavailable"), 5000);
    QVERIFY(!service.supported());
    QCOMPARE(supportedSpy.count(), 0);
}

void AmbientBrightnessCapabilityTest::supportedFalseWhenUnitNotLux()
{
    StubSensorProxy proxy(true, QStringLiteral("counts"));
    QVERIFY(proxy.publish(QDBusConnection::systemBus()));

    AmbientBrightnessService service(QDBusConnection::systemBus());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);
    QVERIFY(service.initialize());

    // 探测先报 true（服务在且提供光感对象），随后单位校验否定 → true 再 false。
    QTRY_COMPARE_WITH_TIMEOUT(supportedSpy.count(), 2, 5000);
    QVERIFY(!service.supported());
    QCOMPARE(service.state(), QStringLiteral("Unavailable"));
    QCOMPARE(proxy.claimCount(), 0);
}

void AmbientBrightnessCapabilityTest::supportedNotWrittenBySamplingStop()
{
    StubSensorProxy proxy(true, QStringLiteral("lux"));
    QVERIFY(proxy.publish(QDBusConnection::systemBus()));

    AmbientBrightnessService service(QDBusConnection::systemBus());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);
    QVERIFY(service.initialize());

    QTRY_COMPARE_WITH_TIMEOUT(proxy.claimCount(), 1, 5000);
    QVERIFY(service.supported());

    // 合盖只应停止采样；Supported 是能力事实，不得被采样停止改写。
    QVERIFY(QMetaObject::invokeMethod(&service, "onLidClosed", Qt::DirectConnection));
    QVERIFY(service.supported());
    QCOMPARE(service.state(), QStringLiteral("Disabled"));
    QCOMPARE(supportedSpy.count(), 1);
}

QTEST_GUILESS_MAIN(AmbientBrightnessCapabilityTest)

#include "tst_ambientbrightnesscapability.moc"
