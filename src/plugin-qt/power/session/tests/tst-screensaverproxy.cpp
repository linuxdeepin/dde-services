// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "sessiondbusproxy.h"
#include "../../powerconstants.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <cstdio>

using namespace PowerDBus;

class PowerFixture : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.Power1")
    Q_PROPERTY(int BatteryScreensaverDelay READ delay)
    Q_PROPERTY(int LinePowerScreensaverDelay READ delay)
public:
    int delay() const { return 300; }
};

class ScreensaverFixture : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.ScreenSaver")
    Q_PROPERTY(bool lockScreenAtAwake READ lockScreenAtAwake)
    Q_PROPERTY(bool isRunning READ isRunning)
public:
    bool lockScreenAtAwake() const { return true; }
    bool isRunning() const { return m_running; }

public Q_SLOTS:
    void Start() { setRunning(true); }
    void Stop() { setRunning(false); }

private:
    void setRunning(bool running)
    {
        m_running = running;
        auto message = QDBusMessage::createSignal(kScreensaverPath,
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
        message << QLatin1String(kScreensaver)
                << QVariantMap{{QStringLiteral("isRunning"), running}} << QStringList();
        QDBusConnection::sessionBus().send(message);
    }
    bool m_running = false;
};

static int runScreensaver(QCoreApplication &app)
{
    ScreensaverFixture screensaver;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(kScreensaverPath, &screensaver,
                           QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots)
        || !bus.registerService(kScreensaver))
        return 1;

    std::puts("READY");
    std::fflush(stdout);
    // Match the real screensaver: read Power1 before entering its event loop.
    // A synchronous Introspect from Power1 would make both sides wait.
    bool success = true;
    for (const auto *property : {"BatteryScreensaverDelay", "LinePowerScreensaverDelay"}) {
        auto message = QDBusMessage::createMethodCall(kService, kPath,
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        message << QLatin1String(kInterface) << QLatin1String(property);
        const auto reply = bus.call(message, QDBus::Block, 800);
        success = success && reply.type() == QDBusMessage::ReplyMessage;
    }
    std::puts(success ? "POWER_READ_OK" : "POWER_READ_TIMEOUT");
    std::fflush(stdout);
    QTimer::singleShot(15000, &app, &QCoreApplication::quit);
    return app.exec();
}

class ScreensaverProxyTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void startupAndNotifications()
    {
        PowerFixture power;
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerObject(kPath, &power, QDBusConnection::ExportAllProperties));
        QVERIFY(bus.registerService(kService));

        SessionDBusProxy proxy;
        QSignalSpy runningChanged(&proxy, &SessionDBusProxy::isRunningChanged);
        QTest::qWait(100);
        QProcess screensaver;
        screensaver.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--screensaver-fixture")});
        QVERIFY(screensaver.waitForStarted());
        QVERIFY(screensaver.waitForReadyRead());
        QByteArray output = screensaver.readAllStandardOutput();
        QVERIFY(output.contains("READY"));

        QTRY_VERIFY_WITH_TIMEOUT((output += screensaver.readAllStandardOutput()).contains("POWER_READ_"), 5000);
        QVERIFY2(output.contains("POWER_READ_OK"), output.constData());
        QTRY_VERIFY_WITH_TIMEOUT(proxy.lockScreenAtAwake(), 5000);

        proxy.startScreenSaver();
        QTRY_VERIFY(proxy.screensaverRunning());
        QVERIFY(!runningChanged.isEmpty());
        proxy.stopScreenSaver();
        QTRY_VERIFY(!proxy.screensaverRunning());
        proxy.startScreenSaver();
        QTRY_VERIFY(proxy.screensaverRunning());
        screensaver.terminate();
        QVERIFY(screensaver.waitForFinished());
        QTRY_VERIFY(!proxy.screensaverRunning());

        bus.unregisterService(kService);
        bus.unregisterObject(kPath);
    }
};

int main(int argc, char **argv)
{
    // Never connect the test's unrelated system proxies to the real power service.
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/power-screensaver-test-bus");
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--screensaver-fixture")))
        return runScreensaver(app);
    ScreensaverProxyTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst-screensaverproxy.moc"
