// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QProcess>
#include <QTest>
#include <QXmlStreamReader>

namespace {

constexpr auto ProviderService = "org.deepin.dde.GlobalShortcutsProvider1";
constexpr auto ProviderPath = "/org/deepin/dde/GlobalShortcutsProvider1";
constexpr auto ProviderInterface = "org.deepin.dde.GlobalShortcutsProvider1";

struct InterfaceContract
{
    QHash<QString, QString> methods;
    QHash<QString, QString> properties;
};

InterfaceContract readInterface(const QString &xmlText, const QString &interfaceName)
{
    QXmlStreamReader xml(xmlText);
    InterfaceContract contract;
    bool inInterface = false;
    bool inMethod = false;
    QString methodName;
    QString inputSignature;
    QString outputSignature;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QStringView element = xml.name();
            if (element == u"interface") {
                inInterface = xml.attributes().value(u"name") == interfaceName;
            } else if (inInterface && element == u"method") {
                inMethod = true;
                methodName = xml.attributes().value(u"name").toString();
                inputSignature.clear();
                outputSignature.clear();
            } else if (inInterface && inMethod && element == u"arg") {
                const QString type = xml.attributes().value(u"type").toString();
                if (xml.attributes().value(u"direction") == u"out")
                    outputSignature += type;
                else
                    inputSignature += type;
            } else if (inInterface && element == u"property") {
                contract.properties.insert(xml.attributes().value(u"name").toString(),
                                           xml.attributes().value(u"type").toString());
            }
        } else if (xml.isEndElement()) {
            const QStringView element = xml.name();
            if (inInterface && inMethod && element == u"method") {
                contract.methods.insert(methodName,
                                        inputSignature + QStringLiteral("->") + outputSignature);
                inMethod = false;
            } else if (element == u"interface" && inInterface) {
                inInterface = false;
            }
        }
    }
    return contract;
}

} // namespace

class TestGlobalShortcutsProviderContract : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void activationServiceFile();
    void objectIsExportedBeforeServiceRegistration();
    void runtimeIntrospection();
};

void TestGlobalShortcutsProviderContract::activationServiceFile()
{
    QFile file(QStringLiteral(PROVIDER_SERVICE_FILE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray contents = file.readAll();
    QVERIFY(contents.contains("Name=org.deepin.dde.GlobalShortcutsProvider1"));
    QVERIFY(contents.contains("/dde-global-shortcuts-provider"));
}

void TestGlobalShortcutsProviderContract::objectIsExportedBeforeServiceRegistration()
{
    QFile file(QStringLiteral(PROVIDER_SOURCE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray source = file.readAll();
    const qsizetype objectRegistration = source.indexOf("bus.registerObject(");
    const qsizetype serviceRegistration = source.indexOf("bus.registerService(");
    QVERIFY(objectRegistration >= 0);
    QVERIFY(serviceRegistration >= 0);
    QVERIFY(objectRegistration < serviceRegistration);
}

void TestGlobalShortcutsProviderContract::runtimeIntrospection()
{
    QProcess provider;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    provider.setProcessEnvironment(environment);
    provider.setProgram(QStringLiteral(PROVIDER_EXECUTABLE_PATH));
    provider.start();
    QVERIFY2(provider.waitForStarted(3000), qPrintable(provider.errorString()));

    QDBusConnection bus = QDBusConnection::sessionBus();
    QTRY_VERIFY_WITH_TIMEOUT(bus.interface()->isServiceRegistered(
                                     QString::fromLatin1(ProviderService)),
                             5000);

    QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(ProviderService),
            QString::fromLatin1(ProviderPath),
            QStringLiteral("org.freedesktop.DBus.Introspectable"),
            QStringLiteral("Introspect"));
    QDBusPendingCallWatcher watcher(bus.asyncCall(message));
    QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(), 3000);
    QDBusPendingReply<QString> reply = watcher;
    QVERIFY2(!reply.isError(), qPrintable(reply.error().message()));

    const InterfaceContract contract = readInterface(
            reply.value(), QString::fromLatin1(ProviderInterface));
    QCOMPARE(contract.methods.size(), 3);
    QCOMPARE(contract.methods.value(QStringLiteral("BeginBind")),
             QStringLiteral("sossa(ssasasb)->"));
    QCOMPARE(contract.methods.value(QStringLiteral("ConfigureShortcuts")),
             QStringLiteral("ossa(ssssasasbb)a{sv}->"));
    QCOMPARE(contract.methods.value(QStringLiteral("CancelRequest")), QStringLiteral("s->"));
    QCOMPARE(contract.properties.value(QStringLiteral("version")), QStringLiteral("u"));

    provider.terminate();
    if (!provider.waitForFinished(3000)) {
        provider.kill();
        QVERIFY(provider.waitForFinished(3000));
    }
}

QTEST_GUILESS_MAIN(TestGlobalShortcutsProviderContract)

#include "tst_globalshortcutsprovidercontract.moc"
