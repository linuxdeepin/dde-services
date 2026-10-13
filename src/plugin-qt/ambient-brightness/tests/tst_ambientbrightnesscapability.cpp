// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Supported 是"本机是否具备环境光传感器"的能力事实，与开关、盖子、休眠、
// 会话状态无关。
//
// 本用例不启动 D-Bus 守护进程，也不访问宿主机真实总线：服务状态机由
// handleSensorPresentChanged() 等公开入口直接驱动，能力结论等价于异步探测
// 回调的结果；私有连接与进程级 DBUS_SYSTEM_BUS_ADDRESS 都指向一个不可达
// 地址。因此构建沙箱里没有 dbus-daemon 也能由 ctest 运行。
//
// 不覆盖（需要真实总线，属集成范畴）：LookupAmbientLightSensor 的回复解析、
// ClaimLight/ReleaseLight 调用、LightLevelUnit 单位校验、WaitingForSample→Active。

#include "ambientbrightnessservice.h"

#include <QDBusConnection>
#include <QSignalSpy>

#include <QTest>

using namespace dde::ambient_brightness;

namespace {

/// 本用例全程使用这个不可达地址：私有连接指向它，进程级 DBUS_SYSTEM_BUS_ADDRESS
/// 也指向它，因此连析构路径里的静态 systemBus() 也不会去连宿主机真实总线。
constexpr auto kOfflineBusAddress = "unix:path=/tmp/tst-ambient-brightness-offline";

/// 服务构造函数需要一个 QDBusConnection；本用例不依赖总线，给一个连接不上的
/// 地址即可，任何意外的总线访问都只会失败。
QDBusConnection offlineConnection()
{
    return QDBusConnection::connectToBus(QString::fromLatin1(kOfflineBusAddress),
                                         QStringLiteral("tst-ambient-brightness"));
}

} // namespace

class AmbientBrightnessCapabilityTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void supportedFollowsHardwareNotSwitch();
    void supportedTrueWhileLidClosedKeepsSwitchOn();
    void stopSamplingKeepsSupported();
    void probeAbsentReportsUnavailable();
    void supportedTracksHardwareGoingOffline();
    void staysUnavailableAfterSamplingStop();
    void unresponsiveSensorKeepsSupported();
};

void AmbientBrightnessCapabilityTest::initTestCase()
{
    QLoggingCategory::setFilterRules(QStringLiteral("dde.ambientbrightness=false"));
    // 必须在第一次 QDBusConnection::systemBus() 之前设置（Qt 会缓存该连接）：
    // 否则服务析构路径里的静态 systemBus().disconnect() 会连上宿主机真实总线。
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", kOfflineBusAddress);
    // 前提断言：未探测时能力为"未知"（Supported=false、State=Unavailable），
    // 且开关默认开启；本文件多个用例依赖这两个前提。
    AmbientBrightnessService service(offlineConnection());
    QVERIFY(!service.supported());
    QCOMPARE(service.state(), QStringLiteral("Unavailable"));
    QVERIFY(service.enabled());
}

// 冷启动前就把开关置为关闭，随后探测到硬件：Supported 必须为 true —— 这正是
// 原先 Supported 被误报为 false 的场景。
void AmbientBrightnessCapabilityTest::supportedFollowsHardwareNotSwitch()
{
    AmbientBrightnessService service(offlineConnection());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);
    QVERIFY(QMetaObject::invokeMethod(&service, "onAutomaticBrightnessEnabledChanged",
                                      Qt::DirectConnection, Q_ARG(bool, false)));
    QVERIFY(!service.enabled());

    service.handleSensorPresentChanged(true);

    QCOMPARE(supportedSpy.count(), 1);
    QVERIFY(service.supported());
    QVERIFY(!service.enabled());
    QCOMPARE(service.state(), QStringLiteral("Disabled"));
}

// 开关保持开启、盖子合上（硬件存在但不该采样）：Supported 依然为 true，
// 证明它来自硬件探测而不是开关状态。
void AmbientBrightnessCapabilityTest::supportedTrueWhileLidClosedKeepsSwitchOn()
{
    AmbientBrightnessService service(offlineConnection());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);
    QVERIFY(QMetaObject::invokeMethod(&service, "onLidClosed", Qt::DirectConnection));

    service.handleSensorPresentChanged(true);

    QCOMPARE(supportedSpy.count(), 1);
    QVERIFY(service.supported());
    QVERIFY(service.enabled());
    QCOMPARE(service.state(), QStringLiteral("Disabled"));
}

// 运行中关闭开关（→ stopSampling）：只停止采样，Supported 不得被改写。
void AmbientBrightnessCapabilityTest::stopSamplingKeepsSupported()
{
    AmbientBrightnessService service(offlineConnection());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);
    QVERIFY(QMetaObject::invokeMethod(&service, "onLidClosed", Qt::DirectConnection));
    service.handleSensorPresentChanged(true);
    QVERIFY(service.supported());

    service.Enable(false);

    QVERIFY(!service.enabled());
    QVERIFY(service.supported());
    QCOMPARE(service.state(), QStringLiteral("Disabled"));
    QCOMPARE(supportedSpy.count(), 1);
}

void AmbientBrightnessCapabilityTest::probeAbsentReportsUnavailable()
{
    AmbientBrightnessService service(offlineConnection());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);

    service.handleSensorPresentChanged(false);

    QVERIFY(!service.supported());
    QCOMPARE(service.state(), QStringLiteral("Unavailable"));
    // 本来就是 false：不得发出多余的 supportedChanged。
    QCOMPARE(supportedSpy.count(), 0);
}

void AmbientBrightnessCapabilityTest::supportedTracksHardwareGoingOffline()
{
    AmbientBrightnessService service(offlineConnection());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);
    QVERIFY(QMetaObject::invokeMethod(&service, "onLidClosed", Qt::DirectConnection));

    service.handleSensorPresentChanged(true);
    QVERIFY(service.supported());
    QCOMPARE(supportedSpy.count(), 1);

    // 传感器服务退出：Supported 随之变 false 并对外广播。
    service.handleSensorPresentChanged(false);

    QCOMPARE(supportedSpy.count(), 2);
    QCOMPARE(supportedSpy.at(1).at(0).toBool(), false);
    QVERIFY(!service.supported());
    QCOMPARE(service.state(), QStringLiteral("Unavailable"));
}

void AmbientBrightnessCapabilityTest::staysUnavailableAfterSamplingStop()
{
    AmbientBrightnessService service(offlineConnection());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);
    service.handleSensorPresentChanged(false);
    QVERIFY(!service.supported());

    service.Enable(false);

    QVERIFY(!service.enabled());
    QVERIFY(!service.supported());
    QCOMPARE(service.state(), QStringLiteral("Unavailable"));
    QCOMPARE(supportedSpy.count(), 0);
}

// 传感器不应答（总线不可达）时，采样路径只能改 State：Supported 归能力探测独占，
// 不能因为"连不上传感器"就被清成 false，否则开关一开一关就会误报"本机没有光感"。
void AmbientBrightnessCapabilityTest::unresponsiveSensorKeepsSupported()
{
    AmbientBrightnessService service(offlineConnection());
    QSignalSpy supportedSpy(&service, &AmbientBrightnessService::supportedChanged);

    service.handleSensorPresentChanged(true); // shouldRun() 为真 → 同步尝试连接传感器

    QVERIFY(service.supported());
    QCOMPARE(service.state(), QStringLiteral("Unavailable"));
    QCOMPARE(supportedSpy.count(), 1); // 只被探测改写这一次
}

QTEST_GUILESS_MAIN(AmbientBrightnessCapabilityTest)

#include "tst_ambientbrightnesscapability.moc"
