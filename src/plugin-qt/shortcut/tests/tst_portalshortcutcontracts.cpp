// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "config/portalshortcutstore.h"
#include "config/configloader.h"
#include "backend/abstractkeyhandler.h"
#include "core/actionexecutor.h"
#include "core/keybindingmanager.h"
#include "core/portalshortcuttypes.h"
#include "core/sessionactivemonitor.h"
#include "core/translationmanager.h"
#include "core/xdgshortcutparser.h"
#include "core/qkeysequenceconverter.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaMethod>
#include <QQueue>
#include <QSignalSpy>
#include <QTest>
#include <QXmlStreamReader>

#include <algorithm>
#include <functional>
#include <utility>

class TestPortalShortcutContracts : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void dbusSignatures();
    void stableInternalId();
    void rejectsCorruptPersistedFields();
    void schemaHasEmptyDefaults();
    void interfaceHasFrozenSignatures();
    void keybindingMetaObjectHasPortalContract();
    void keybindingRuntimeIntrospectionHasPortalContract();
    void defersConfigChangeDuringCommit();
    void legacyListsExcludePortalShortcuts();
    void portalDigitsRemainSymbolic_data();
    void portalDigitsRemainSymbolic();
    void acceptsSupplementaryUnicodeRecords();
    void lateRegistrationFailureShrinksSessionBindings();
    void queuedRegistrationFailurePreservesRollback();
    void keypadTriggersRoundTrip_data();
    void keypadTriggersRoundTrip();
    void parsesSpecificationExamples();
    void rejectsMalformedTriggers_data();
    void rejectsMalformedTriggers();
};

namespace {

class FakeKeyHandler : public AbstractKeyHandler
{
public:
    using AbstractKeyHandler::AbstractKeyHandler;

    bool registerKey(const KeyConfig &config) override
    {
        activeConfigs.insert(config.getId(), config);
        return true;
    }

    bool unregisterKey(const QString &shortcutId) override
    {
        activeConfigs.remove(shortcutId);
        return true;
    }

    bool commitSync() override
    {
        ++commitDepth;
        maxCommitDepth = std::max(maxCommitDepth, commitDepth);
        const auto callback = std::exchange(onCommit, std::function<void()>{});
        if (callback)
            callback();
        --commitDepth;
        return commitResults.isEmpty() ? true : commitResults.dequeue();
    }

    QHash<QString, KeyConfig> activeConfigs;
    QQueue<bool> commitResults;
    std::function<void()> onCommit;
    int commitDepth = 0;
    int maxCommitDepth = 0;
};

class MemoryPortalStore : public PortalShortcutStore
{
public:
    MemoryPortalStore() : PortalShortcutStore(QStringLiteral("/tmp/unused-portal-contract.ini")) {}

    bool load(QString *) override { return true; }
    PortalShortcutInfoList shortcutsForApp(const QString &appId) const override
    {
        PortalShortcutInfoList result;
        for (const PortalShortcutInfo &record : records) {
            if (record.appId == appId)
                result.append(record);
        }
        return result;
    }
    std::optional<PortalShortcutInfo> shortcut(const QString &appId,
                                              const QString &shortcutId) const override
    {
        const auto it = records.constFind(internalId(appId, shortcutId));
        return it == records.cend() ? std::nullopt
                                   : std::optional<PortalShortcutInfo>(*it);
    }
    bool apply(const PortalShortcutInfoList &shortcuts, QString *error) override
    {
        for (const PortalShortcutInfo &record : shortcuts) {
            if (!validate(record, error))
                return false;
        }
        for (PortalShortcutInfo record : shortcuts) {
            record.active = false;
            records.insert(record.id, record);
        }
        return true;
    }

    QHash<QString, PortalShortcutInfo> records;
};

struct InterfaceContract
{
    QHash<QString, QString> methods;
    QHash<QString, QString> signalEntries;
};

InterfaceContract readInterface(const QString &xmlText, const QString &interfaceName)
{
    QXmlStreamReader xml(xmlText);
    InterfaceContract contract;
    bool inInterface = false;
    QString currentKind;
    QString name;
    QString input;
    QString output;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == u"interface") {
                inInterface = xml.attributes().value(u"name") == interfaceName;
            } else if (inInterface && (xml.name() == u"method" || xml.name() == u"signal")) {
                currentKind = xml.name().toString();
                name = xml.attributes().value(u"name").toString();
                input.clear();
                output.clear();
            } else if (inInterface && !currentKind.isEmpty() && xml.name() == u"arg") {
                const QString type = xml.attributes().value(u"type").toString();
                if (currentKind == u"signal" || xml.attributes().value(u"direction") == u"out")
                    output += type;
                else
                    input += type;
            }
        } else if (xml.isEndElement()) {
            if (inInterface && currentKind == u"method" && xml.name() == u"method") {
                contract.methods.insert(name, input + QStringLiteral("->") + output);
                currentKind.clear();
            } else if (inInterface && currentKind == u"signal" && xml.name() == u"signal") {
                contract.signalEntries.insert(name, output);
                currentKind.clear();
            } else if (inInterface && xml.name() == u"interface") {
                inInterface = false;
            }
        }
    }
    return contract;
}

} // namespace

void TestPortalShortcutContracts::initTestCase()
{
    registerPortalShortcutMetaTypes();
}

void TestPortalShortcutContracts::dbusSignatures()
{
    QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(
                     QMetaType::fromType<PortalShortcutInfo>())),
             QByteArray("(ssssasasbb)"));
    QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(
                     QMetaType::fromType<PortalShortcutInfoList>())),
             QByteArray("a(ssssasasbb)"));
    QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(
                     QMetaType::fromType<PortalShortcutUpdate>())),
             QByteArray("(ssasasb)"));
    QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(
                     QMetaType::fromType<PortalShortcutUpdateList>())),
             QByteArray("a(ssasasb)"));
    QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(
                     QMetaType::fromType<PortalShortcutConflict>())),
             QByteArray("(sssss)"));
    QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(
                     QMetaType::fromType<PortalShortcutConflictList>())),
             QByteArray("a(sssss)"));
    QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(
                     QMetaType::fromType<PortalTriggerList>())),
             QByteArray("a(ss)"));
}

void TestPortalShortcutContracts::stableInternalId()
{
    const QString first = PortalShortcutStore::internalId(
            QStringLiteral("org.example.Editor"), QStringLiteral("new-tab"));
    const QString same = PortalShortcutStore::internalId(
            QStringLiteral("org.example.Editor"), QStringLiteral("new-tab"));
    const QString differentApp = PortalShortcutStore::internalId(
            QStringLiteral("org.example.Other"), QStringLiteral("new-tab"));
    const QString differentAction = PortalShortcutStore::internalId(
            QStringLiteral("org.example.Editor"), QStringLiteral("new-window"));

    QCOMPARE(first, same);
    QVERIFY(first != differentApp);
    QVERIFY(first != differentAction);
    QCOMPARE(PortalShortcutStore::normalizeSubPath(first), first);
    QCOMPARE(PortalShortcutStore::normalizeSubPath(QStringLiteral("/") + first), first);
    QVERIFY(PortalShortcutStore::normalizeSubPath(first + QStringLiteral("/child")).isEmpty());
}

void TestPortalShortcutContracts::rejectsCorruptPersistedFields()
{
    PortalShortcutInfo shortcut;
    shortcut.appId = QStringLiteral("org.example.Editor");
    shortcut.shortcutId = QStringLiteral("new-tab");
    shortcut.id = PortalShortcutStore::internalId(shortcut.appId, shortcut.shortcutId);
    shortcut.description = QStringLiteral("Open a new tab");
    shortcut.hotkeys = {QStringLiteral("Ctrl+Alt+T")};
    shortcut.enabled = true;
    QVERIFY(PortalShortcutStore::validate(shortcut));

    PortalShortcutInfo invalid = shortcut;
    invalid.hotkeys = {QStringLiteral("not a portable shortcut")};
    QVERIFY(!PortalShortcutStore::validate(invalid));

    invalid = shortcut;
    invalid.appId = QString(256, QLatin1Char('a'));
    invalid.id = PortalShortcutStore::internalId(invalid.appId, invalid.shortcutId);
    QVERIFY(!PortalShortcutStore::validate(invalid));

    invalid = shortcut;
    invalid.description = QStringLiteral("unsafe\ntext");
    QVERIFY(!PortalShortcutStore::validate(invalid));
}

void TestPortalShortcutContracts::schemaHasEmptyDefaults()
{
    QFile file(QStringLiteral(PORTAL_SHORTCUT_SCHEMA_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject contents = QJsonDocument::fromJson(file.readAll())
                                         .object()
                                         .value(QStringLiteral("contents"))
                                         .toObject();

    const QStringList contentKeys = contents.keys();
    QCOMPARE(QSet<QString>(contentKeys.begin(), contentKeys.end()),
             QSet<QString>({QStringLiteral("appId"),
                            QStringLiteral("shortcutId"),
                            QStringLiteral("description"),
                            QStringLiteral("hotkeys"),
                            QStringLiteral("preferredKeys"),
                            QStringLiteral("enabled"),
                            QStringLiteral("category"),
                            QStringLiteral("modifiable")}));
    QCOMPARE(contents.value(QStringLiteral("appId")).toObject().value(QStringLiteral("value")).toString(),
             QString());
    QCOMPARE(contents.value(QStringLiteral("shortcutId")).toObject().value(QStringLiteral("value")).toString(),
             QString());
    QCOMPARE(contents.value(QStringLiteral("description")).toObject().value(QStringLiteral("value")).toString(),
             QString());
    QVERIFY(contents.value(QStringLiteral("hotkeys")).toObject().value(QStringLiteral("value")).toArray().isEmpty());
    QVERIFY(contents.value(QStringLiteral("preferredKeys")).toObject().value(QStringLiteral("value")).toArray().isEmpty());
    QVERIFY(!contents.value(QStringLiteral("enabled")).toObject().value(QStringLiteral("value")).toBool());
    QCOMPARE(contents.value(QStringLiteral("category")).toObject().value(QStringLiteral("value")).toString(),
             QString());
    QVERIFY(!contents.value(QStringLiteral("modifiable")).toObject().value(QStringLiteral("value")).toBool());
}

void TestPortalShortcutContracts::interfaceHasFrozenSignatures()
{
    QFile file(QStringLiteral(PORTAL_SHORTCUT_INTERFACE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray xml = file.readAll();
    QVERIFY(xml.contains("name=\"ListPortalShortcuts\""));
    QVERIFY(xml.contains("name=\"NormalizeXdgPortalTriggers\""));
    QVERIFY(xml.contains("name=\"CommitPortalSessionBindings\""));
    QVERIFY(xml.contains("name=\"RestorePortalSessionBindings\""));
    QVERIFY(xml.contains("name=\"ApplyPortalSessionUpdates\""));
    QVERIFY(xml.contains("name=\"DeactivatePortalShortcuts\""));
    QVERIFY(xml.contains("name=\"CheckPortalShortcutConflicts\""));
    QVERIFY(xml.contains("type=\"a(ssssasasbb)\""));
    QVERIFY(xml.contains("type=\"a(ssasasb)\""));
    QVERIFY(xml.contains("type=\"a(sssss)\""));
    QVERIFY(xml.contains("type=\"a(ss)\""));
    const auto contract = readInterface(QString::fromUtf8(xml),
                                        QStringLiteral("org.deepin.dde.Keybinding1"));
    QCOMPARE(contract.signalEntries.value(QStringLiteral("PortalSessionShortcutsChanged")),
             QStringLiteral("ssa(ssssasasbb)"));
}

void TestPortalShortcutContracts::keybindingMetaObjectHasPortalContract()
{
    const QMetaObject &metaObject = KeybindingManager::staticMetaObject;
    const auto method = [&metaObject](const char *signature) {
        const int index = metaObject.indexOfMethod(signature);
        return index >= 0 ? metaObject.method(index) : QMetaMethod{};
    };

    QCOMPARE(QByteArray(method("ListPortalShortcuts(QString)").typeName()),
             QByteArray("PortalShortcutInfoList"));
    QCOMPARE(QByteArray(method("NormalizeXdgPortalTriggers(PortalTriggerList)").typeName()),
             QByteArray("PortalTriggerList"));
    QCOMPARE(QByteArray(method("CommitPortalSessionBindings(QString,QString,PortalShortcutUpdateList)")
                                .typeName()),
             QByteArray("PortalShortcutInfoList"));
    QCOMPARE(QByteArray(method("RestorePortalSessionBindings(QString,QString,PortalShortcutUpdateList,QStringList)")
                                .typeName()),
             QByteArray("PortalShortcutInfoList"));
    QCOMPARE(QByteArray(method("ApplyPortalSessionUpdates(QString,QString,PortalShortcutUpdateList)")
                                .typeName()),
             QByteArray("PortalShortcutInfoList"));
    QCOMPARE(QByteArray(method("DeactivatePortalShortcuts(QString,QString)").typeName()),
             QByteArray("void"));
    QCOMPARE(QByteArray(method("CheckPortalShortcutConflicts(QString,PortalShortcutUpdateList)")
                                .typeName()),
             QByteArray("PortalShortcutConflictList"));

    QVERIFY(metaObject.indexOfSignal("PortalShortcutPressed(QString,QString,qulonglong)") >= 0);
    QVERIFY(metaObject.indexOfSignal("PortalShortcutReleased(QString,QString,qulonglong)") >= 0);
    QVERIFY(metaObject.indexOfSignal(
                    "PortalSessionShortcutsChanged(QString,QString,PortalShortcutInfoList)") >= 0);
    QVERIFY(metaObject.indexOfSignal(
                    "PortalShortcutsChanged(QString,PortalShortcutInfoList)")
            >= 0);
}

void TestPortalShortcutContracts::keybindingRuntimeIntrospectionHasPortalContract()
{
    constexpr auto ConnectionName = "portal-keybinding-contract";
    constexpr auto ServiceName = "org.deepin.dde.Keybinding1.ContractTest";
    constexpr auto ObjectPath = "/org/deepin/dde/Keybinding1";
    QDBusConnection serviceConnection = QDBusConnection::connectToBus(
            QDBusConnection::SessionBus, QString::fromLatin1(ConnectionName));
    QVERIFY(serviceConnection.isConnected());

    ConfigLoader loader;
    ActionExecutor executor;
    TranslationManager translations;
    FakeKeyHandler keyHandler;
    SessionActiveMonitor sessionMonitor;
    KeybindingManager manager(&loader, &executor, &translations, &keyHandler,
                              &sessionMonitor);
    QVERIFY(serviceConnection.registerObject(QString::fromLatin1(ObjectPath),
                                             &manager,
                                             QDBusConnection::ExportScriptableContents));
    QVERIFY(serviceConnection.registerService(QString::fromLatin1(ServiceName)));

    QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(ServiceName),
            QString::fromLatin1(ObjectPath),
            QStringLiteral("org.freedesktop.DBus.Introspectable"),
            QStringLiteral("Introspect"));
    QDBusPendingCallWatcher watcher(QDBusConnection::sessionBus().asyncCall(message));
    QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(), 3000);
    QDBusPendingReply<QString> reply = watcher;
    QVERIFY2(!reply.isError(), qPrintable(reply.error().message()));

    const auto contract = readInterface(
            reply.value(), QStringLiteral("org.deepin.dde.Keybinding1"));
    const auto &methods = contract.methods;
    QCOMPARE(methods.value(QStringLiteral("ListPortalShortcuts")),
             QStringLiteral("s->a(ssssasasbb)"));
    QCOMPARE(methods.value(QStringLiteral("NormalizeXdgPortalTriggers")),
             QStringLiteral("a(ss)->a(ss)"));
    QCOMPARE(methods.value(QStringLiteral("CommitPortalSessionBindings")),
             QStringLiteral("ssa(ssasasb)->a(ssssasasbb)"));
    QCOMPARE(methods.value(QStringLiteral("RestorePortalSessionBindings")),
             QStringLiteral("ssa(ssasasb)as->a(ssssasasbb)"));
    QCOMPARE(methods.value(QStringLiteral("ApplyPortalSessionUpdates")),
             QStringLiteral("ssa(ssasasb)->a(ssssasasbb)"));
    QCOMPARE(methods.value(QStringLiteral("DeactivatePortalShortcuts")),
             QStringLiteral("ss->"));
    QCOMPARE(methods.value(QStringLiteral("CheckPortalShortcutConflicts")),
             QStringLiteral("sa(ssasasb)->a(sssss)"));
    QCOMPARE(contract.signalEntries.value(QStringLiteral("PortalSessionShortcutsChanged")),
             QStringLiteral("ssa(ssssasasbb)"));

    serviceConnection.unregisterService(QString::fromLatin1(ServiceName));
    serviceConnection.unregisterObject(QString::fromLatin1(ObjectPath));
    QDBusConnection::disconnectFromBus(QString::fromLatin1(ConnectionName));
}

void TestPortalShortcutContracts::defersConfigChangeDuringCommit()
{
    ConfigLoader loader;
    ActionExecutor executor;
    TranslationManager translations;
    FakeKeyHandler keyHandler;
    SessionActiveMonitor sessionMonitor;
    KeybindingManager manager(&loader, &executor, &translations, &keyHandler,
                              &sessionMonitor);

    KeyConfig initial;
    initial.appId = QStringLiteral("org.example.Editor");
    initial.subPath = QStringLiteral("org.example.Editor.shortcut.open");
    initial.displayName = QStringLiteral("Open");
    initial.category = QStringLiteral("Applications");
    initial.enabled = true;
    initial.modifiable = true;
    initial.triggerType = static_cast<int>(TriggerType::Command);
    initial.triggerValue = {QStringLiteral("true")};
    initial.hotkeys = {QStringLiteral("<Control><Alt>O")};

    QVERIFY(QMetaObject::invokeMethod(&manager,
                                      "onKeyConfigAdded",
                                      Qt::DirectConnection,
                                      Q_ARG(KeyConfig, initial)));
    QCOMPARE(manager.GetShortcut(initial.getId()).hotkeys, initial.hotkeys);

    KeyConfig changed = initial;
    changed.hotkeys = {QStringLiteral("<Control><Alt>N")};
    bool changeDelivered = false;
    keyHandler.maxCommitDepth = 0;
    keyHandler.onCommit = [&] {
        changeDelivered = QMetaObject::invokeMethod(&loader,
                                                    "keyConfigChanged",
                                                    Qt::DirectConnection,
                                                    Q_ARG(KeyConfig, changed));
    };

    QVERIFY(QMetaObject::invokeMethod(&manager,
                                      "onBackendKeymapChanged",
                                      Qt::DirectConnection));
    QVERIFY(changeDelivered);
    QCOMPARE(keyHandler.maxCommitDepth, 1);
    QCOMPARE(manager.GetShortcut(initial.getId()).hotkeys, initial.hotkeys);

    QTRY_COMPARE(manager.GetShortcut(initial.getId()).hotkeys, changed.hotkeys);
    QCOMPARE(keyHandler.maxCommitDepth, 1);
    QCOMPARE(keyHandler.activeConfigs.value(initial.getId()).hotkeys,
             QStringList({QStringLiteral("Ctrl+Alt+N")}));
}

void TestPortalShortcutContracts::legacyListsExcludePortalShortcuts()
{
    ConfigLoader loader;
    ActionExecutor executor;
    TranslationManager translations;
    FakeKeyHandler keyHandler;
    SessionActiveMonitor sessionMonitor;
    KeybindingManager manager(&loader, &executor, &translations, &keyHandler,
                              &sessionMonitor);

    KeyConfig portal;
    portal.origin = ShortcutOrigin::Portal;
    portal.appId = QStringLiteral("org.example.PortalApp");
    portal.subPath = QStringLiteral("org.deepin.dde.keybinding.shortcut.portal.test");
    portal.displayName = QStringLiteral("Portal action");
    portal.category = QStringLiteral("Applications");
    portal.enabled = true;
    portal.modifiable = true;
    portal.triggerType = static_cast<int>(TriggerType::PortalNotification);
    portal.triggerValue = {QStringLiteral("portal-action")};
    portal.hotkeys = {QStringLiteral("Ctrl+Alt+P")};

    QVERIFY(QMetaObject::invokeMethod(&manager,
                                      "onKeyConfigAdded",
                                      Qt::DirectConnection,
                                      Q_ARG(KeyConfig, portal)));

    QVERIFY(manager.ListAllShortcuts().isEmpty());
    QVERIFY(manager.ListShortcutsByApp(portal.appId).isEmpty());
    QVERIFY(manager.ListShortcutsByCategory(portal.category).isEmpty());
    QVERIFY(manager.ListCategories().isEmpty());
    QVERIFY(manager.SearchShortcuts(QStringLiteral("Portal")).isEmpty());

    const ShortcutInfo conflict = manager.LookupConflictShortcut(QStringLiteral("Ctrl+Alt+P"));
    QCOMPARE(conflict.id, portal.getId());
    QVERIFY(!conflict.modifiable);
}

void TestPortalShortcutContracts::parsesSpecificationExamples()
{
    QCOMPARE(XdgShortcutParser::normalize(QStringLiteral("a")),
             std::optional<QString>(QStringLiteral("A")));
    QCOMPARE(XdgShortcutParser::normalize(QStringLiteral("CTRL+a")),
             std::optional<QString>(QStringLiteral("Ctrl+A")));
    QCOMPARE(XdgShortcutParser::normalize(QStringLiteral("CTRL+SHIFT+a")),
             std::optional<QString>(QStringLiteral("Ctrl+Shift+A")));
    QCOMPARE(XdgShortcutParser::normalize(QStringLiteral("CTRL+ALT+Return")),
             std::optional<QString>(QStringLiteral("Ctrl+Alt+Return")));
    QVERIFY(XdgShortcutParser::normalize(QStringLiteral("LOGO+XF86AudioMute")).has_value());
    QCOMPARE(XdgShortcutParser::normalize(QStringLiteral("CTRL+KP_1")),
             std::optional<QString>(QStringLiteral("Ctrl+Num+1")));
    QCOMPARE(XdgShortcutParser::normalize(QStringLiteral("CTRL+semicolon")),
             std::optional<QString>(QStringLiteral("Ctrl+;")));
    QCOMPARE(XdgShortcutParser::normalize(QStringLiteral("CTRL+asterisk")),
             std::optional<QString>(QStringLiteral("Ctrl+*")));
}

void TestPortalShortcutContracts::portalDigitsRemainSymbolic_data()
{
    QTest::addColumn<QString>("digit");
    for (int i = 0; i <= 9; ++i) {
        const QByteArray name = QByteArray::number(i);
        QTest::newRow(name.constData()) << QString::number(i);
    }
}

void TestPortalShortcutContracts::portalDigitsRemainSymbolic()
{
    QFETCH(QString, digit);
    ConfigLoader loader;
    ActionExecutor executor;
    TranslationManager translations;
    FakeKeyHandler keyHandler;
    SessionActiveMonitor sessionMonitor;
    KeybindingManager manager(&loader, &executor, &translations, &keyHandler,
                              &sessionMonitor, nullptr, nullptr, new MemoryPortalStore);
    const auto bound = manager.CommitPortalSessionBindings(
            QStringLiteral("org.example.Digits"), QStringLiteral("digits"),
            {{QStringLiteral("digit"), QStringLiteral("Digit"), {digit}, {}, true}});
    QCOMPARE(bound.size(), 1);
    QVERIFY(keyHandler.activeConfigs.contains(bound.constFirst().id));
    QCOMPARE(keyHandler.activeConfigs.value(bound.constFirst().id).hotkeys, QStringList{digit});
    QCOMPARE(keyHandler.activeConfigs.value(bound.constFirst().id).origin, ShortcutOrigin::Portal);
}

void TestPortalShortcutContracts::acceptsSupplementaryUnicodeRecords()
{
    PortalShortcutInfo record;
    record.appId = QStringLiteral("org.example.Unicode");
    record.shortcutId = QStringLiteral("microphone-\U0001f399");
    record.description = QStringLiteral("Toggle microphone \U0001f399");
    record.id = PortalShortcutStore::internalId(record.appId, record.shortcutId);
    record.hotkeys = {QStringLiteral("Ctrl+Alt+M")};
    record.enabled = true;
    QVERIFY(PortalShortcutStore::validate(record));
    record.description = QString::fromUtf16(u"\xd800", 1);
    QVERIFY(!PortalShortcutStore::validate(record));
    record.description = QStringLiteral("invalid\ncontrol");
    QVERIFY(!PortalShortcutStore::validate(record));
}

void TestPortalShortcutContracts::lateRegistrationFailureShrinksSessionBindings()
{
    ConfigLoader loader;
    ActionExecutor executor;
    TranslationManager translations;
    FakeKeyHandler keyHandler;
    SessionActiveMonitor sessionMonitor;
    KeybindingManager manager(&loader, &executor, &translations, &keyHandler,
                              &sessionMonitor, nullptr, nullptr, new MemoryPortalStore);
    const QString appId = QStringLiteral("org.example.LateFailure");
    const auto bound = manager.CommitPortalSessionBindings(
            appId, QStringLiteral("late-failure"),
            {{QStringLiteral("action"), QStringLiteral("Action"),
              {QStringLiteral("Ctrl+Alt+P")}, {}, true}});
    QCOMPARE(bound.size(), 1);
    const QString id = bound.constFirst().id;
    QSignalSpy releases(&manager, &KeybindingManager::PortalShortcutReleased);
    QSignalSpy sessions(&manager, &KeybindingManager::PortalSessionShortcutsChanged);
    Q_EMIT keyHandler.keyActivated(id, 0x1);
    Q_EMIT keyHandler.registrationFailed(id);
    QCOMPARE(releases.size(), 1);
    QCOMPARE(sessions.size(), 1);
    QVERIFY(qvariant_cast<PortalShortcutInfoList>(sessions.constFirst().at(2)).isEmpty());
    QVERIFY(keyHandler.activeConfigs.isEmpty());
    const auto listed = manager.ListPortalShortcuts(appId);
    QCOMPARE(listed.size(), 1);
    QVERIFY(listed.constFirst().enabled);
    QVERIFY(!listed.constFirst().active);
    Q_EMIT keyHandler.keyActivated(id, 0x2);
    QCOMPARE(releases.size(), 1);
}

void TestPortalShortcutContracts::queuedRegistrationFailurePreservesRollback()
{
    ConfigLoader loader;
    ActionExecutor executor;
    TranslationManager translations;
    FakeKeyHandler keyHandler;
    SessionActiveMonitor sessionMonitor;
    KeybindingManager manager(&loader, &executor, &translations, &keyHandler,
                              &sessionMonitor, nullptr, nullptr, new MemoryPortalStore);
    const QString appId = QStringLiteral("org.example.QueuedFailure");
    const QString session = QStringLiteral("queued-failure");
    const QString oldKey = QStringLiteral("Ctrl+Alt+P");
    const auto bound = manager.CommitPortalSessionBindings(
            appId, session,
            {{QStringLiteral("action"), QStringLiteral("Action"), {oldKey}, {}, true}});
    QCOMPARE(bound.size(), 1);
    const QString id = bound.constFirst().id;
    QSignalSpy sessions(&manager, &KeybindingManager::PortalSessionShortcutsChanged);
    keyHandler.commitResults.enqueue(false);
    keyHandler.commitResults.enqueue(true);
    keyHandler.onCommit = [&] { Q_EMIT keyHandler.registrationFailed(id); };
    const auto failed = manager.ApplyPortalSessionUpdates(
            appId, session,
            {{QStringLiteral("action"), QStringLiteral("Action"),
              {QStringLiteral("Ctrl+Alt+N")}, {}, true}});
    QVERIFY(failed.isEmpty());
    QTest::qWait(20);
    const auto listed = manager.ListPortalShortcuts(appId);
    QCOMPARE(listed.size(), 1);
    QVERIFY(listed.constFirst().active);
    QCOMPARE(listed.constFirst().hotkeys, QStringList{oldKey});
    QCOMPARE(keyHandler.activeConfigs.value(id).hotkeys, QStringList{oldKey});
    QCOMPARE(sessions.size(), 0);
}

void TestPortalShortcutContracts::keypadTriggersRoundTrip_data()
{
    QTest::addColumn<QString>("keysym");
    for (int i = 0; i <= 9; ++i) {
        const QByteArray name = "KP_" + QByteArray::number(i);
        QTest::newRow(name.constData()) << QString::fromLatin1(name);
    }
    for (const char *name : {"KP_Enter", "KP_Add", "KP_Subtract", "KP_Multiply",
                             "KP_Divide", "KP_Decimal", "KP_Home", "KP_Delete"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void TestPortalShortcutContracts::keypadTriggersRoundTrip()
{
    QFETCH(QString, keysym);
    const auto normalized = XdgShortcutParser::normalize(QStringLiteral("CTRL+") + keysym);
    QVERIFY(normalized.has_value());
    QCOMPARE(QKeySequenceConverter::qKeySequenceToXkb(*normalized),
             QStringLiteral("<Control>") + keysym);
    QVERIFY(*normalized != QStringLiteral("Ctrl+1"));
}

void TestPortalShortcutContracts::rejectsMalformedTriggers_data()
{
    QTest::addColumn<QString>("trigger");
    QTest::newRow("empty") << QString();
    QTest::newRow("lowercase-modifier") << QStringLiteral("ctrl+a");
    QTest::newRow("duplicate-modifier") << QStringLiteral("CTRL+CTRL+a");
    QTest::newRow("unknown-modifier") << QStringLiteral("HYPER+a");
    QTest::newRow("unsupported-numlock-modifier") << QStringLiteral("NUM+KP_1");
    QTest::newRow("empty-component") << QStringLiteral("CTRL++a");
    QTest::newRow("trailing-separator") << QStringLiteral("CTRL+a+");
    QTest::newRow("unknown-keysym") << QStringLiteral("CTRL+DefinitelyNotAKeysym");
    QTest::newRow("invalid-identifier") << QStringLiteral("CTRL+Page Up");
}

void TestPortalShortcutContracts::rejectsMalformedTriggers()
{
    QFETCH(QString, trigger);
    QVERIFY(!XdgShortcutParser::normalize(trigger).has_value());
}

QTEST_MAIN(TestPortalShortcutContracts)

#include "tst_portalshortcutcontracts.moc"
