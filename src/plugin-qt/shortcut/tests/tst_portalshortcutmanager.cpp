// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "config/portalshortcutstore.h"
#include "core/portalshortcutmanager.h"

#include <QQueue>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <functional>

namespace {

PortalShortcutInfo makeInfo(const QString &appId,
                            const QString &shortcutId,
                            const QString &description,
                            const QString &hotkey,
                            bool enabled = true)
{
    PortalShortcutInfo info;
    info.id = PortalShortcutStore::internalId(appId, shortcutId);
    info.appId = appId;
    info.shortcutId = shortcutId;
    info.description = description;
    if (!hotkey.isEmpty())
        info.hotkeys = {hotkey};
    info.enabled = enabled;
    return info;
}

PortalShortcutUpdate makeUpdate(const QString &shortcutId,
                                const QString &description,
                                const QString &hotkey,
                                bool enabled = true)
{
    PortalShortcutUpdate update;
    update.shortcutId = shortcutId;
    update.description = description;
    if (!hotkey.isEmpty())
        update.hotkeys = {hotkey};
    update.enabled = enabled;
    return update;
}

class FakeStorage : public PortalShortcutStore
{
public:
    FakeStorage()
        : PortalShortcutStore(QStringLiteral("/tmp/dde-portal-shortcuts-test.ini"))
    {
    }

    bool load(QString *errorMessage) override
    {
        ++loadCount;
        if (onLoad)
            onLoad();
        if (failLoad) {
            if (errorMessage)
                *errorMessage = QStringLiteral("injected reload failure");
            return false;
        }
        return true;
    }

    PortalShortcutInfoList shortcutsForApp(const QString &appId) const override
    {
        PortalShortcutInfoList result;
        for (const PortalShortcutInfo &record : records) {
            if (record.appId == appId)
                result.append(record);
        }
        std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
            return left.shortcutId < right.shortcutId;
        });
        return result;
    }

    std::optional<PortalShortcutInfo> shortcut(const QString &appId,
                                               const QString &shortcutId) const override
    {
        const auto it = records.constFind(PortalShortcutStore::internalId(appId, shortcutId));
        if (it == records.cend())
            return std::nullopt;
        return *it;
    }

    bool apply(const PortalShortcutInfoList &shortcuts, QString *errorMessage) override
    {
        ++applyCount;
        if (onApply)
            onApply();
        if (failApply && !persistBeforeFailure) {
            if (errorMessage)
                *errorMessage = QStringLiteral("injected persistence failure");
            return false;
        }
        for (PortalShortcutInfo shortcut : shortcuts) {
            shortcut.active = false;
            records.insert(shortcut.id, shortcut);
        }
        if (failApply) {
            if (errorMessage)
                *errorMessage = QStringLiteral("injected compensation failure");
            return false;
        }
        return true;
    }

    QHash<QString, PortalShortcutInfo> records;
    int applyCount = 0;
    int loadCount = 0;
    bool failApply = false;
    bool failLoad = false;
    bool persistBeforeFailure = false;
    std::function<void()> onApply;
    std::function<void()> onLoad;
};

class FakeRuntime : public PortalShortcutRuntime
{
public:
    bool beginPortalWrite() override
    {
        if (busy || locked)
            return false;
        locked = true;
        return true;
    }

    void endPortalWrite() override
    {
        locked = false;
    }

    PortalRuntimeConflict lookupPortalConflict(const QString &hotkey,
                                               const QStringList &excludeIds) const override
    {
        const PortalRuntimeConflict conflict = conflicts.value(hotkey);
        return excludeIds.contains(conflict.id) ? PortalRuntimeConflict{} : conflict;
    }

    bool registerPortalShortcut(const KeyConfig &config) override
    {
        ++registerCount;
        if (failRegister)
            return false;
        active.insert(config.getId(), config);
        return true;
    }

    void unregisterPortalShortcut(const QString &id) override
    {
        ++unregisterCount;
        active.remove(id);
    }

    bool commitPortalShortcuts() override
    {
        ++commitCount;
        if (onCommit) {
            auto callback = std::move(onCommit);
            callback();
        }
        return commitResults.isEmpty() ? true : commitResults.dequeue();
    }

    bool busy = false;
    bool locked = false;
    bool failRegister = false;
    int registerCount = 0;
    int unregisterCount = 0;
    int commitCount = 0;
    QHash<QString, KeyConfig> active;
    QHash<QString, PortalRuntimeConflict> conflicts;
    QQueue<bool> commitResults;
    std::function<void()> onCommit;
};

} // namespace

class TestPortalShortcutManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void persistedHistoryDoesNotRegister();
    void emptyDescriptionUsesShortcutIdAtRuntime();
    void commitIsIdempotentAndDeactivates();
    void closedSessionRejectsLateCommit();
    void referencesArePerSession();
    void existingBindingWinsOverNewSuggestion();
    void restoreReactivatesOnlyPreviouslyBoundSubset();
    void bindOmitsOrdinaryConflicts();
    void requestInternalConflictRejectsBatch();
    void backendFailureDoesNotPersist();
    void persistenceFailureRestoresRuntime();
    void persistenceCompensationFailureReconcilesNewKeys();
    void runtimeCompensationFailureRebuildsBoundSubset();
    void reconciliationFailureClearsActualSubset_data();
    void reconciliationFailureClearsActualSubset();
    void closeDuringReconciliationRemovesLastReference();
    void acceptsSupplementaryUnicodeBindings();
    void persistenceWindowSuppressesEvents();
    void configureOnlyChangesUserFields();
    void emitsPressAndReleaseOnly();
    void heldShortcutTransition_data();
    void heldShortcutTransition();
    void unrelatedWriteDoesNotSwallowRelease();
    void configurePublishesActualSessionBindings();
    void busyDeactivationRetriesOnce();
    void deactivationDuringCommitDoesNotLeaveLateSession();
    void backendLossDuringCommitReleasesLateSession();
    void deactivateAllSessionsReleasesSharedBinding();
};

void TestPortalShortcutManager::persistedHistoryDoesNotRegister()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    const PortalShortcutInfo history = makeInfo(QStringLiteral("org.example.App"),
                                                QStringLiteral("open"),
                                                QStringLiteral("Open"),
                                                QStringLiteral("Ctrl+Alt+O"));
    storage->records.insert(history.id, history);
    PortalShortcutManager manager(&runtime, storage);

    QVERIFY(manager.load());
    QCOMPARE(runtime.registerCount, 0);
    const auto listed = manager.listShortcuts(history.appId);
    QCOMPARE(listed.size(), 1);
    QVERIFY(!listed.constFirst().active);
}

void TestPortalShortcutManager::emptyDescriptionUsesShortcutIdAtRuntime()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.App");
    const QString shortcutId = QStringLiteral("open");
    const PortalShortcutUpdateList request = {
        makeUpdate(shortcutId, QString(), QStringLiteral("Ctrl+Alt+O")),
    };

    PortalOperationError error;
    const auto result = manager.commitSessionBindings(appId,
                                                       QStringLiteral("session-empty-description"),
                                                       request,
                                                       &error);
    QVERIFY2(result.has_value(), qPrintable(error.message));
    QCOMPARE(result->size(), 1);
    QVERIFY(result->constFirst().description.isEmpty());

    const QString id = PortalShortcutStore::internalId(appId, shortcutId);
    QVERIFY(runtime.active.contains(id));
    QCOMPARE(runtime.active.value(id).displayName, shortcutId);
    QVERIFY(storage->records.value(id).description.isEmpty());
}

void TestPortalShortcutManager::commitIsIdempotentAndDeactivates()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.App");
    const PortalShortcutUpdateList request = {
        makeUpdate(QStringLiteral("open"), QStringLiteral("Open"), QStringLiteral("Ctrl+Alt+O")),
    };

    PortalOperationError error;
    const auto first = manager.commitSessionBindings(appId, QStringLiteral("session-1"), request, &error);
    QVERIFY2(first.has_value(), qPrintable(error.message));
    QCOMPARE(first->size(), 1);
    QVERIFY(first->constFirst().active);
    QCOMPARE(runtime.registerCount, 1);
    QCOMPARE(storage->applyCount, 1);

    const auto duplicate = manager.commitSessionBindings(appId,
                                                          QStringLiteral("session-1"),
                                                          request,
                                                          &error);
    QVERIFY(duplicate.has_value());
    QCOMPARE(runtime.registerCount, 1);
    QCOMPARE(storage->applyCount, 1);

    manager.deactivateSession(appId, QStringLiteral("session-1"));
    QCOMPARE(runtime.unregisterCount, 1);
    QVERIFY(runtime.active.isEmpty());
    QVERIFY(!manager.listShortcuts(appId).constFirst().active);
    QCOMPARE(storage->records.size(), 1);
}

void TestPortalShortcutManager::closedSessionRejectsLateCommit()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.App");
    const PortalShortcutUpdateList request = {
        makeUpdate(QStringLiteral("open"), QStringLiteral("Open"), QStringLiteral("Ctrl+Alt+O")),
    };

    PortalOperationError error;
    QVERIFY(manager.commitSessionBindings(appId, QStringLiteral("session-1"), request, &error));
    manager.deactivateSession(appId, QStringLiteral("session-1"));

    error = {};
    const auto late = manager.commitSessionBindings(appId,
                                                     QStringLiteral("session-1"),
                                                     request,
                                                     &error);
    QVERIFY(!late.has_value());
    QCOMPARE(error.name, QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"));
    QCOMPARE(storage->applyCount, 1);
    QVERIFY(runtime.active.isEmpty());
}

void TestPortalShortcutManager::referencesArePerSession()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.App");
    const PortalShortcutUpdateList request = {
        makeUpdate(QStringLiteral("open"), QStringLiteral("Open"), QStringLiteral("Ctrl+Alt+O")),
    };
    PortalOperationError error;

    QVERIFY(manager.commitSessionBindings(appId, QStringLiteral("session-1"), request, &error));
    QVERIFY(manager.commitSessionBindings(appId, QStringLiteral("session-2"), request, &error));
    QCOMPARE(runtime.registerCount, 1);

    manager.deactivateSession(appId, QStringLiteral("session-1"));
    QCOMPARE(runtime.unregisterCount, 0);
    QVERIFY(manager.listShortcuts(appId).constFirst().active);

    manager.deactivateSession(appId, QStringLiteral("session-2"));
    QCOMPARE(runtime.unregisterCount, 1);
    QVERIFY(!manager.listShortcuts(appId).constFirst().active);
}

void TestPortalShortcutManager::existingBindingWinsOverNewSuggestion()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    const QString appId = QStringLiteral("org.example.App");
    const PortalShortcutInfo history = makeInfo(appId,
                                                QStringLiteral("open"),
                                                QStringLiteral("Old description"),
                                                QStringLiteral("Ctrl+Alt+O"));
    storage->records.insert(history.id, history);
    PortalShortcutManager manager(&runtime, storage);

    PortalShortcutUpdate update = makeUpdate(QStringLiteral("open"),
                                             QStringLiteral("New description"),
                                             QStringLiteral("Ctrl+Alt+N"),
                                             false);
    update.preferredKeys = {QStringLiteral("Ctrl+Shift+N")};
    PortalOperationError error;
    const auto bound = manager.commitSessionBindings(appId,
                                                     QStringLiteral("session-1"),
                                                     {update},
                                                     &error);
    QVERIFY2(bound.has_value(), qPrintable(error.message));
    QCOMPARE(bound->constFirst().hotkeys, QStringList({QStringLiteral("Ctrl+Alt+O")}));
    QVERIFY(bound->constFirst().enabled);
    QCOMPARE(bound->constFirst().description, QStringLiteral("New description"));
    QCOMPARE(bound->constFirst().preferredKeys,
             QStringList({QStringLiteral("Ctrl+Shift+N")}));
}

void TestPortalShortcutManager::restoreReactivatesOnlyPreviouslyBoundSubset()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    const QString appId = QStringLiteral("org.example.App");
    const PortalShortcutInfo first = makeInfo(appId,
                                              QStringLiteral("first"),
                                              QStringLiteral("First"),
                                              QStringLiteral("Ctrl+Alt+1"));
    const PortalShortcutInfo second = makeInfo(appId,
                                               QStringLiteral("second"),
                                               QStringLiteral("Second"),
                                               QStringLiteral("Ctrl+Alt+2"));
    storage->records.insert(first.id, first);
    storage->records.insert(second.id, second);
    PortalShortcutManager manager(&runtime, storage);
    PortalOperationError error;
    const PortalShortcutUpdateList declared = {
        makeUpdate(first.shortcutId, first.description, QString()),
        makeUpdate(second.shortcutId, second.description, QString()),
    };

    const auto restored = manager.restoreSessionBindings(
            appId,
            QStringLiteral("session-restore"),
            declared,
            {first.shortcutId},
            &error);
    QVERIFY2(restored.has_value(), qPrintable(error.message));
    QCOMPARE(restored->size(), 1);
    QCOMPARE(restored->constFirst().shortcutId, first.shortcutId);
    QCOMPARE(runtime.active.size(), 1);
    QVERIFY(runtime.active.contains(first.id));
    QVERIFY(!runtime.active.contains(second.id));
    QCOMPARE(storage->applyCount, 0);

    const auto configured = manager.applySessionUpdates(
            appId,
            QStringLiteral("session-restore"),
            {makeUpdate(first.shortcutId,
                        first.description,
                        QStringLiteral("Ctrl+Alt+1")),
             makeUpdate(second.shortcutId,
                        second.description,
                        QStringLiteral("Ctrl+Alt+2"))},
            &error);
    QVERIFY2(configured.has_value(), qPrintable(error.message));
    QCOMPARE(configured->size(), 2);
    QCOMPARE(runtime.active.size(), 2);
}

void TestPortalShortcutManager::bindOmitsOrdinaryConflicts()
{
    FakeRuntime runtime;
    runtime.conflicts.insert(
            QStringLiteral("Ctrl+Alt+O"),
            {QStringLiteral("builtin.open"),
             ShortcutOrigin::Builtin,
             true,
             QStringLiteral("org.deepin.dde.keybinding"),
             QStringLiteral("builtin.open")});
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    PortalOperationError error;
    const auto bound = manager.commitSessionBindings(
            QStringLiteral("org.example.App"),
            QStringLiteral("session-1"),
            {makeUpdate(QStringLiteral("open"),
                        QStringLiteral("Open"),
                        QStringLiteral("Ctrl+Alt+O"))},
            &error);

    QVERIFY2(bound.has_value(), qPrintable(error.message));
    QVERIFY(bound->isEmpty());
    QCOMPARE(storage->records.size(), 1);
    const QString id = PortalShortcutStore::internalId(QStringLiteral("org.example.App"),
                                                        QStringLiteral("open"));
    QVERIFY(!storage->records.value(id).enabled);
    QCOMPARE(storage->records.value(id).hotkeys,
             QStringList({QStringLiteral("Ctrl+Alt+O")}));
    QCOMPARE(runtime.registerCount, 0);

    runtime.conflicts.clear();
    error = {};
    const auto retried = manager.commitSessionBindings(
            QStringLiteral("org.example.App"),
            QStringLiteral("session-2"),
            {makeUpdate(QStringLiteral("open"),
                        QStringLiteral("Open"),
                        QStringLiteral("Ctrl+Alt+N"))},
            &error);
    QVERIFY2(retried.has_value(), qPrintable(error.message));
    QCOMPARE(retried->size(), 1);
    QVERIFY(retried->constFirst().enabled);
    QCOMPARE(retried->constFirst().hotkeys,
             QStringList({QStringLiteral("Ctrl+Alt+N")}));
}

void TestPortalShortcutManager::requestInternalConflictRejectsBatch()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    PortalOperationError error;
    const auto bound = manager.commitSessionBindings(
            QStringLiteral("org.example.App"),
            QStringLiteral("session-1"),
            {makeUpdate(QStringLiteral("first"),
                        QStringLiteral("First"),
                        QStringLiteral("Ctrl+Alt+O")),
             makeUpdate(QStringLiteral("second"),
                        QStringLiteral("Second"),
                        QStringLiteral("Ctrl+Alt+O"))},
            &error);

    QVERIFY(!bound.has_value());
    QCOMPARE(error.name, QStringLiteral("org.deepin.dde.Keybinding1.Error.Conflict"));
    QCOMPARE(storage->applyCount, 0);
    QCOMPARE(runtime.registerCount, 0);
}

void TestPortalShortcutManager::backendFailureDoesNotPersist()
{
    FakeRuntime runtime;
    runtime.commitResults.enqueue(false);
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    PortalOperationError error;
    const auto bound = manager.commitSessionBindings(
            QStringLiteral("org.example.App"),
            QStringLiteral("session-1"),
            {makeUpdate(QStringLiteral("open"),
                        QStringLiteral("Open"),
                        QStringLiteral("Ctrl+Alt+O"))},
            &error);

    QVERIFY(!bound.has_value());
    QCOMPARE(storage->applyCount, 0);
    QVERIFY(runtime.active.isEmpty());
    QVERIFY(runtime.unregisterCount >= 1);
}

void TestPortalShortcutManager::persistenceFailureRestoresRuntime()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    storage->failApply = true;
    PortalShortcutManager manager(&runtime, storage);
    PortalOperationError error;
    const auto bound = manager.commitSessionBindings(
            QStringLiteral("org.example.App"),
            QStringLiteral("session-1"),
            {makeUpdate(QStringLiteral("open"),
                        QStringLiteral("Open"),
                        QStringLiteral("Ctrl+Alt+O"))},
            &error);

    QVERIFY(!bound.has_value());
    QCOMPARE(storage->applyCount, 1);
    QVERIFY(runtime.active.isEmpty());
    QVERIFY(runtime.unregisterCount >= 1);
    QVERIFY(manager.listShortcuts(QStringLiteral("org.example.App")).isEmpty());
}

void TestPortalShortcutManager::persistenceCompensationFailureReconcilesNewKeys()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.Reconcile");
    const QString action = QStringLiteral("action");
    const QString id = PortalShortcutStore::internalId(appId, action);
    const QString oldKey = QStringLiteral("Ctrl+Alt+O");
    const QString newKey = QStringLiteral("Ctrl+Alt+N");
    const PortalShortcutUpdateList emptyRequest = {makeUpdate(action, action, {}, false)};
    const PortalShortcutUpdateList boundRequest = {makeUpdate(action, action, oldKey)};
    PortalOperationError error;
    const auto empty = manager.commitSessionBindings(appId, QStringLiteral("empty"), emptyRequest, &error);
    QVERIFY(empty.has_value());
    QVERIFY(empty->isEmpty());
    QVERIFY(manager.commitSessionBindings(appId, QStringLiteral("bound"), boundRequest, &error));
    QSignalSpy releases(&manager, &PortalShortcutManager::shortcutReleased);
    QSignalSpy sessions(&manager, &PortalShortcutManager::sessionShortcutsChanged);
    QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Press));
    storage->failApply = true;
    storage->persistBeforeFailure = true;

    const auto failed = manager.applySessionUpdates(
            appId, QStringLiteral("bound"), {makeUpdate(action, action, newKey)}, &error);
    QVERIFY(!failed);
    QVERIFY(error.isValid());
    QCOMPARE(storage->loadCount, 1);
    QCOMPARE(runtime.active.value(id).hotkeys, QStringList{newKey});
    QCOMPARE(manager.listShortcuts(appId).constFirst().hotkeys, QStringList{newKey});
    QVERIFY(manager.listShortcuts(appId).constFirst().active);
    const auto stillEmpty = manager.commitSessionBindings(
            appId, QStringLiteral("empty"), emptyRequest, &error);
    QVERIFY(stillEmpty.has_value());
    QVERIFY(stillEmpty->isEmpty());
    QCOMPARE(releases.size(), 1);
    manager.handleKeyEvent(id, KeyEventFlag::Release);
    QCOMPARE(releases.size(), 1);
    QCOMPARE(sessions.size(), 2);
    for (const QList<QVariant> &signal : sessions) {
        const auto subset = qvariant_cast<PortalShortcutInfoList>(signal.at(2));
        if (signal.at(1).toString() == QLatin1String("empty"))
            QVERIFY(subset.isEmpty());
        else {
            QCOMPARE(subset.size(), 1);
            QCOMPARE(subset.constFirst().hotkeys, QStringList{newKey});
        }
    }
}

void TestPortalShortcutManager::runtimeCompensationFailureRebuildsBoundSubset()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.RuntimeReconcile");
    const QString oldKey = QStringLiteral("Ctrl+Alt+O");
    PortalOperationError error;
    const auto bound = manager.commitSessionBindings(
            appId, QStringLiteral("bound"), {makeUpdate(QStringLiteral("action"), {}, oldKey)}, &error);
    QVERIFY(bound.has_value());
    const QString id = bound->constFirst().id;
    runtime.commitResults.enqueue(false);
    runtime.commitResults.enqueue(false);
    const auto failed = manager.applySessionUpdates(
            appId, QStringLiteral("bound"),
            {makeUpdate(QStringLiteral("action"), {}, QStringLiteral("Ctrl+Alt+N"))}, &error);
    QVERIFY(!failed);
    QCOMPARE(storage->loadCount, 1);
    QCOMPARE(runtime.active.value(id).hotkeys, QStringList{oldKey});
    QVERIFY(manager.listShortcuts(appId).constFirst().active);
    QCOMPARE(manager.listShortcuts(appId).constFirst().hotkeys, QStringList{oldKey});
}

void TestPortalShortcutManager::reconciliationFailureClearsActualSubset_data()
{
    QTest::addColumn<bool>("failReload");
    QTest::newRow("storage-reload") << true;
    QTest::newRow("repeated-runtime-failure") << false;
}

void TestPortalShortcutManager::reconciliationFailureClearsActualSubset()
{
    QFETCH(bool, failReload);
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.Unavailable");
    PortalOperationError error;
    QVERIFY(manager.commitSessionBindings(
            appId, QStringLiteral("bound"),
            {makeUpdate(QStringLiteral("action"), {}, QStringLiteral("Ctrl+Alt+O"))}, &error));
    const int previousCommits = runtime.commitCount;
    storage->failApply = true;
    storage->failLoad = failReload;
    if (!failReload) {
        for (int i = 0; i < 4; ++i)
            runtime.commitResults.enqueue(false);
    }
    const auto failed = manager.applySessionUpdates(
            appId, QStringLiteral("bound"),
            {makeUpdate(QStringLiteral("action"), {}, QStringLiteral("Ctrl+Alt+N"))}, &error);
    QVERIFY(!failed);
    QVERIFY(runtime.commitCount - previousCommits <= 4);
    QVERIFY(runtime.active.isEmpty());
    QVERIFY(!manager.listShortcuts(appId).constFirst().active);
    storage->failApply = false;
    storage->failLoad = false;
    runtime.commitResults.clear();
    const auto recovered = manager.applySessionUpdates(
            appId, QStringLiteral("bound"),
            {makeUpdate(QStringLiteral("action"), {}, QStringLiteral("Ctrl+Alt+R"))}, &error);
    QVERIFY(recovered.has_value());
    QCOMPARE(recovered->size(), 1);
    QCOMPARE(runtime.active.size(), 1);
}

void TestPortalShortcutManager::closeDuringReconciliationRemovesLastReference()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.CloseReconcile");
    const QString session = QStringLiteral("bound");
    PortalOperationError error;
    const auto bound = manager.commitSessionBindings(
            appId, session,
            {makeUpdate(QStringLiteral("action"), {}, QStringLiteral("Ctrl+Alt+O"))}, &error);
    QVERIFY(bound.has_value());
    QSignalSpy releases(&manager, &PortalShortcutManager::shortcutReleased);
    QVERIFY(manager.handleKeyEvent(bound->constFirst().id, KeyEventFlag::Press));
    storage->failApply = true;
    storage->persistBeforeFailure = true;
    storage->onLoad = [&] {
        runtime.onCommit = [&] { manager.deactivateSession(appId, session); };
    };
    const auto failed = manager.applySessionUpdates(
            appId, session,
            {makeUpdate(QStringLiteral("action"), {}, QStringLiteral("Ctrl+Alt+N"))}, &error);
    QVERIFY(!failed);
    QVERIFY(runtime.active.isEmpty());
    QVERIFY(!manager.listShortcuts(appId).constFirst().active);
    QCOMPARE(releases.size(), 1);
}

void TestPortalShortcutManager::acceptsSupplementaryUnicodeBindings()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.Unicode");
    const QString action = QStringLiteral("microphone-\U0001f399");
    PortalOperationError error;
    const auto bound = manager.commitSessionBindings(
            appId, QStringLiteral("unicode"),
            {makeUpdate(action, QStringLiteral("Toggle microphone \U0001f399"),
                        QStringLiteral("Ctrl+Alt+M"))}, &error);
    QVERIFY(bound.has_value());
    QCOMPARE(bound->size(), 1);
    QCOMPARE(bound->constFirst().shortcutId, action);
    const auto invalid = manager.commitSessionBindings(
            appId, QStringLiteral("invalid"),
            {makeUpdate(QString::fromUtf16(u"\xd800", 1), {}, QStringLiteral("Ctrl+Alt+N"))}, &error);
    QVERIFY(!invalid);
}

void TestPortalShortcutManager::persistenceWindowSuppressesEvents()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    QSignalSpy pressed(&manager, &PortalShortcutManager::shortcutPressed);
    const QString appId = QStringLiteral("org.example.App");
    const QString id = PortalShortcutStore::internalId(appId, QStringLiteral("open"));
    bool consumedDuringPersistence = false;
    storage->onApply = [&] {
        consumedDuringPersistence = manager.handleKeyEvent(id, KeyEventFlag::Press);
    };

    PortalOperationError error;
    QVERIFY(manager.commitSessionBindings(
            appId,
            QStringLiteral("session-1"),
            {makeUpdate(QStringLiteral("open"),
                        QStringLiteral("Open"),
                        QStringLiteral("Ctrl+Alt+O"))},
            &error));
    QVERIFY(consumedDuringPersistence);
    QCOMPARE(pressed.size(), 0);

    QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Press));
    QCOMPARE(pressed.size(), 1);
}

void TestPortalShortcutManager::configureOnlyChangesUserFields()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.App");
    const PortalShortcutUpdate initial = makeUpdate(QStringLiteral("open"),
                                                    QStringLiteral("Application description"),
                                                    QStringLiteral("Ctrl+Alt+O"));
    PortalOperationError error;
    QVERIFY(manager.commitSessionBindings(appId,
                                          QStringLiteral("session-1"),
                                          {initial},
                                          &error));

    PortalShortcutUpdate configure = makeUpdate(QStringLiteral("open"),
                                                QStringLiteral("Untrusted replacement"),
                                                QStringLiteral("Ctrl+Alt+Y"));
    configure.preferredKeys = {QStringLiteral("Ctrl+Alt+P")};
    const auto bound = manager.applySessionUpdates(appId,
                                                   QStringLiteral("session-1"),
                                                   {configure},
                                                   &error);
    QVERIFY2(bound.has_value(), qPrintable(error.message));
    QCOMPARE(bound->constFirst().description, QStringLiteral("Application description"));
    QVERIFY(bound->constFirst().preferredKeys.isEmpty());
    QCOMPARE(bound->constFirst().hotkeys, QStringList({QStringLiteral("Ctrl+Alt+Y")}));
}

void TestPortalShortcutManager::emitsPressAndReleaseOnly()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.App");
    PortalOperationError error;
    QVERIFY(manager.commitSessionBindings(
            appId,
            QStringLiteral("session-1"),
            {makeUpdate(QStringLiteral("open"),
                        QStringLiteral("Open"),
                        QStringLiteral("Ctrl+Alt+O"))},
            &error));

    QSignalSpy pressed(&manager, &PortalShortcutManager::shortcutPressed);
    QSignalSpy released(&manager, &PortalShortcutManager::shortcutReleased);
    const QString id = PortalShortcutStore::internalId(appId, QStringLiteral("open"));
    QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Press));
    QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Press));
    QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Repeat));
    QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Release));
    QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Release));
    QCOMPARE(pressed.size(), 1);
    QCOMPARE(released.size(), 1);
}

void TestPortalShortcutManager::heldShortcutTransition_data()
{
    QTest::addColumn<QString>("operation");
    for (const char *operation : {"disable", "rebind", "rollback", "close", "close-all", "reconnect"})
        QTest::newRow(operation) << QString::fromLatin1(operation);
}

void TestPortalShortcutManager::heldShortcutTransition()
{
    QFETCH(QString, operation);
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.Held");
    const QString token = QStringLiteral("held-session");
    const QString id = PortalShortcutStore::internalId(appId, QStringLiteral("talk"));
    auto update = makeUpdate(QStringLiteral("talk"), QStringLiteral("Talk"),
                             QStringLiteral("Ctrl+Alt+7"));
    PortalOperationError error;
    QVERIFY(manager.commitSessionBindings(appId, token, {update}, &error));
    QSignalSpy pressed(&manager, &PortalShortcutManager::shortcutPressed);
    QSignalSpy released(&manager, &PortalShortcutManager::shortcutReleased);
    QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Press));

    if (operation == QLatin1String("close")) {
        manager.deactivateSession(appId, token);
    } else if (operation == QLatin1String("close-all")) {
        manager.deactivateAllSessions();
    } else if (operation == QLatin1String("reconnect")) {
        manager.restoreRuntimeBindings();
    } else {
        update.enabled = operation != QLatin1String("disable");
        update.hotkeys = {QStringLiteral("Ctrl+Alt+8")};
        storage->failApply = operation == QLatin1String("rollback");
        const auto result = manager.applySessionUpdates(appId, token, {update}, &error);
        QCOMPARE(result.has_value(), !storage->failApply);
        if (storage->failApply)
            QCOMPARE(manager.activeConfig(id)->hotkeys, QStringList{QStringLiteral("Ctrl+Alt+7")});
    }
    QCOMPARE(pressed.size(), 1);
    QCOMPARE(released.size(), 1);
    QCOMPARE(released.first().at(0).toString(), appId);
    QVERIFY(released.first().at(2).toULongLong() >= pressed.first().at(2).toULongLong());
    manager.handleKeyEvent(id, KeyEventFlag::Release);
    QCOMPARE(released.size(), 1);
    if (manager.activeConfig(id)) {
        QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Press));
        QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Release));
        QCOMPARE(pressed.size(), 2);
        QCOMPARE(released.size(), 2);
    }
}

void TestPortalShortcutManager::unrelatedWriteDoesNotSwallowRelease()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.Held");
    const QString id = PortalShortcutStore::internalId(appId, QStringLiteral("talk"));
    PortalOperationError error;
    QVERIFY(manager.commitSessionBindings(appId, QStringLiteral("first"),
                                          {makeUpdate(QStringLiteral("talk"), QStringLiteral("Talk"),
                                                      QStringLiteral("Ctrl+Alt+7"))},
                                          &error));
    QSignalSpy released(&manager, &PortalShortcutManager::shortcutReleased);
    QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Press));
    runtime.onCommit = [&] { QVERIFY(manager.handleKeyEvent(id, KeyEventFlag::Release)); };
    QVERIFY(manager.commitSessionBindings(appId, QStringLiteral("second"),
                                          {makeUpdate(QStringLiteral("toggle"),
                                                      QStringLiteral("Toggle"),
                                                      QStringLiteral("Ctrl+Alt+8"))},
                                          &error));
    QCOMPARE(released.size(), 1);
}

void TestPortalShortcutManager::configurePublishesActualSessionBindings()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    const QString appId = QStringLiteral("org.example.Isolation");
    const auto record = makeInfo(appId, QStringLiteral("talk"), QStringLiteral("Talk"),
                                 QStringLiteral("Ctrl+Alt+7"));
    storage->records.insert(record.id, record);
    runtime.conflicts.insert(record.hotkeys.first(),
                             {QStringLiteral("system-key"), ShortcutOrigin::Builtin, false, {}, {}});
    PortalShortcutManager manager(&runtime, storage);
    const auto update = makeUpdate(record.shortcutId, record.description, record.hotkeys.first());
    PortalOperationError error;
    const auto first = manager.commitSessionBindings(appId, QStringLiteral("first"), {update},
                                                     &error);
    QVERIFY(first && first->isEmpty());
    runtime.conflicts.clear();
    QVERIFY(manager.commitSessionBindings(appId, QStringLiteral("second"), {update}, &error));
    const auto unchanged = manager.commitSessionBindings(appId, QStringLiteral("first"), {update},
                                                         &error);
    QVERIFY(unchanged && unchanged->isEmpty());

    QSignalSpy changed(&manager, &PortalShortcutManager::sessionShortcutsChanged);
    QVERIFY(manager.applySessionUpdates(appId, QStringLiteral("second"), {update}, &error));
    QCOMPARE(changed.size(), 2);
    QSet<QString> tokens;
    for (const auto &event : changed) {
        tokens.insert(event.at(1).toString());
        const auto bound = event.at(2).value<PortalShortcutInfoList>();
        QCOMPARE(bound.size(), 1);
        QCOMPARE(bound.first().shortcutId, record.shortcutId);
    }
    QCOMPARE(tokens, QSet<QString>({QStringLiteral("first"), QStringLiteral("second")}));
    manager.deactivateSession(appId, QStringLiteral("second"));
    QVERIFY(manager.activeConfig(record.id));
    manager.deactivateSession(appId, QStringLiteral("first"));
    QVERIFY(!manager.activeConfig(record.id));
}

void TestPortalShortcutManager::busyDeactivationRetriesOnce()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.App");
    PortalOperationError error;
    QVERIFY(manager.commitSessionBindings(
            appId,
            QStringLiteral("session-1"),
            {makeUpdate(QStringLiteral("open"),
                        QStringLiteral("Open"),
                        QStringLiteral("Ctrl+Alt+O"))},
            &error));

    runtime.busy = true;
    manager.deactivateSession(appId, QStringLiteral("session-1"));
    QCOMPARE(runtime.unregisterCount, 0);
    runtime.busy = false;

    QTRY_COMPARE(runtime.unregisterCount, 1);
    QVERIFY(runtime.active.isEmpty());
    QVERIFY(!manager.listShortcuts(appId).constFirst().active);
}

void TestPortalShortcutManager::deactivationDuringCommitDoesNotLeaveLateSession()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.App");
    const QString sessionToken = QStringLiteral("session-1");
    const PortalShortcutUpdateList request = {
        makeUpdate(QStringLiteral("open"),
                   QStringLiteral("Open"),
                   QStringLiteral("Ctrl+Alt+O")),
    };

    runtime.onCommit = [&manager, appId, sessionToken] {
        manager.deactivateSession(appId, sessionToken);
    };
    PortalOperationError error;
    const auto result = manager.commitSessionBindings(appId, sessionToken, request, &error);
    QVERIFY(!result.has_value());
    QCOMPARE(error.name, QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"));
    QVERIFY(runtime.active.isEmpty());
    QCOMPARE(storage->records.size(), 1);
    QVERIFY(!manager.listShortcuts(appId).constFirst().active);

    error = {};
    QVERIFY(!manager.commitSessionBindings(appId, sessionToken, request, &error));
    QCOMPARE(error.name, QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"));
}

void TestPortalShortcutManager::backendLossDuringCommitReleasesLateSession()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.App");
    const QString sessionToken = QStringLiteral("session-1");
    const PortalShortcutUpdateList request = {
        makeUpdate(QStringLiteral("open"),
                   QStringLiteral("Open"),
                   QStringLiteral("Ctrl+Alt+O")),
    };

    runtime.onCommit = [&manager] { manager.deactivateAllSessions(); };
    PortalOperationError error;
    const auto result = manager.commitSessionBindings(appId, sessionToken, request, &error);
    QVERIFY(!result.has_value());
    QCOMPARE(error.name, QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"));
    QVERIFY(runtime.active.isEmpty());
    QVERIFY(!manager.listShortcuts(appId).constFirst().active);

    error = {};
    QVERIFY(!manager.restoreSessionBindings(appId,
                                            sessionToken,
                                            request,
                                            {QStringLiteral("open")},
                                            &error));
    QCOMPARE(error.name, QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"));
}

void TestPortalShortcutManager::deactivateAllSessionsReleasesSharedBinding()
{
    FakeRuntime runtime;
    auto *storage = new FakeStorage;
    PortalShortcutManager manager(&runtime, storage);
    const QString appId = QStringLiteral("org.example.App");
    const PortalShortcutUpdateList request = {
        makeUpdate(QStringLiteral("open"),
                   QStringLiteral("Open"),
                   QStringLiteral("Ctrl+Alt+O")),
    };

    PortalOperationError error;
    QVERIFY(manager.commitSessionBindings(appId, QStringLiteral("session-1"), request, &error));
    QVERIFY(manager.commitSessionBindings(appId, QStringLiteral("session-2"), request, &error));
    QCOMPARE(runtime.active.size(), 1);

    manager.deactivateAllSessions();
    QVERIFY(runtime.active.isEmpty());
    QVERIFY(!manager.listShortcuts(appId).constFirst().active);

    error = {};
    QVERIFY(!manager.commitSessionBindings(appId, QStringLiteral("session-1"), request, &error));
    QCOMPARE(error.name, QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"));
    error = {};
    QVERIFY(!manager.commitSessionBindings(appId, QStringLiteral("session-2"), request, &error));
    QCOMPARE(error.name, QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"));
}

QTEST_GUILESS_MAIN(TestPortalShortcutManager)

#include "tst_portalshortcutmanager.moc"
