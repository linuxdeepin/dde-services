// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "portalshortcutmanager.h"

#include "config/portalshortcutstore.h"
#include "qkeysequenceconverter.h"
#include "xdgshortcutparser.h"

#include <QDebug>
#include <QKeySequence>

#include <algorithm>

namespace {

const QString BusyError = QStringLiteral("org.deepin.dde.Keybinding1.Error.Busy");
const QString ConflictError = QStringLiteral("org.deepin.dde.Keybinding1.Error.Conflict");
const QString InvalidArgsError = QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs");
const QString FailedError = QStringLiteral("org.freedesktop.DBus.Error.Failed");
const QString UnknownObjectError = QStringLiteral("org.freedesktop.DBus.Error.UnknownObject");

} // namespace

class PortalShortcutManager::WriteGuard
{
public:
    explicit WriteGuard(PortalShortcutManager *manager)
        : m_manager(manager)
        , m_runtime(manager ? manager->m_runtime : nullptr)
        , m_locked(m_runtime && m_runtime->beginPortalWrite())
    {
    }

    ~WriteGuard()
    {
        if (m_locked) {
            m_runtime->endPortalWrite();
            m_manager->processQueuedDeactivations();
        }
    }

    bool isLocked() const { return m_locked; }

private:
    PortalShortcutManager *m_manager = nullptr;
    PortalShortcutRuntime *m_runtime = nullptr;
    bool m_locked = false;
};

PortalShortcutManager::PortalShortcutManager(PortalShortcutRuntime *runtime, QObject *parent)
    : PortalShortcutManager(runtime, new PortalShortcutStore, parent)
{
    m_store->setParent(this);
}

PortalShortcutManager::PortalShortcutManager(PortalShortcutRuntime *runtime,
                                             PortalShortcutStore *store,
                                             QObject *parent)
    : QObject(parent)
    , m_runtime(runtime)
    , m_store(store)
{
    Q_ASSERT(m_runtime);
    Q_ASSERT(m_store);
    if (!m_store->parent())
        m_store->setParent(this);
    m_eventClock.start();
}

bool PortalShortcutManager::load()
{
    QString error;
    if (!m_store->load(&error)) {
        qCritical() << "PortalShortcutManager: failed to load persisted shortcuts" << error;
        return false;
    }
    return true;
}

PortalShortcutInfoList PortalShortcutManager::listShortcuts(const QString &appId) const
{
    return decoratedShortcuts(appId);
}

std::optional<PortalTriggerList> PortalShortcutManager::normalizeTriggers(
        const PortalTriggerList &requested,
        PortalOperationError *error) const
{
    QString parserError;
    const auto normalized = XdgShortcutParser::normalizeBatch(requested, &parserError);
    if (!normalized)
        setError(error, InvalidArgsError, parserError);
    return normalized;
}

std::optional<PortalShortcutInfoList> PortalShortcutManager::commitSessionBindings(
        const QString &appId,
        const QString &sessionToken,
        const PortalShortcutUpdateList &requested,
        PortalOperationError *error)
{
    PortalShortcutUpdateList normalized;
    if (sessionToken.isEmpty()
        || !normalizeUpdates(appId, requested, normalized, error)) {
        if (sessionToken.isEmpty())
            setError(error, InvalidArgsError, QStringLiteral("The session token is empty"));
        return std::nullopt;
    }

    if (m_closedTokens.contains(sessionToken)) {
        setError(error, UnknownObjectError, QStringLiteral("The Portal session is already closed"));
        return std::nullopt;
    }

    const auto existingSession = m_sessions.constFind(sessionToken);
    if (existingSession != m_sessions.cend()) {
        if (existingSession->appId != appId || existingSession->request != normalized) {
            setError(error,
                     InvalidArgsError,
                     QStringLiteral("The session token was reused for a different request"));
            return std::nullopt;
        }
        return boundShortcuts(*existingSession);
    }

    const quint64 lifecycleGeneration = m_lifecycleGeneration;
    WriteGuard guard(this);
    if (!guard.isLocked()) {
        setError(error, BusyError, QStringLiteral("Another shortcut write is in progress"));
        return std::nullopt;
    }

    PortalShortcutInfoList desiredList;
    desiredList.reserve(normalized.size());
    QHash<QString, PortalShortcutInfo> desiredRecords;
    QSet<QString> successfulHistoryIds;
    SessionState newSession;
    newSession.appId = appId;
    newSession.request = normalized;
    for (const PortalShortcutUpdate &update : normalized) {
        PortalShortcutInfo record;
        const auto stored = m_store->shortcut(appId, update.shortcutId);
        if (stored && stored->enabled && !stored->hotkeys.isEmpty()) {
            record = *stored;
            record.description = update.description;
            record.preferredKeys = update.preferredKeys;
            successfulHistoryIds.insert(update.shortcutId);
        } else {
            if (stored)
                record = *stored;
            else {
                record.id = PortalShortcutStore::internalId(appId, update.shortcutId);
                record.appId = appId;
                record.shortcutId = update.shortcutId;
            }
            record.description = update.description;
            record.hotkeys = update.hotkeys;
            record.preferredKeys = update.preferredKeys;
            record.enabled = update.enabled;
        }
        record.active = false;
        desiredList.append(record);
        desiredRecords.insert(record.id, record);
        newSession.declaredOrder.append(update.shortcutId);
        newSession.declaredIds.insert(update.shortcutId);
    }

    const QSet<QString> affectedIds(desiredRecords.keyBegin(), desiredRecords.keyEnd());
    const PortalShortcutConflictList conflicts = conflictsForRecords(appId,
                                                                     desiredList,
                                                                     affectedIds);
    if (hasInternalConflict(conflicts)) {
        setError(error,
                 ConflictError,
                 QStringLiteral("The Bind request contains duplicate hotkeys"));
        return std::nullopt;
    }
    const QSet<QString> blockedIds = blockedShortcutIds(conflicts);
    for (PortalShortcutInfo &record : desiredList) {
        if (blockedIds.contains(record.shortcutId)
                && !successfulHistoryIds.contains(record.shortcutId)) {
            record.enabled = false;
            desiredRecords.insert(record.id, record);
        }
    }

    QHash<QString, QSet<QString>> desiredReferences;
    for (const PortalShortcutInfo &record : desiredList) {
        QSet<QString> references = m_references.value(record.id);
        if (record.enabled && !record.hotkeys.isEmpty() && !blockedIds.contains(record.shortcutId)) {
            references.insert(sessionToken);
            newSession.boundIds.insert(record.shortcutId);
        }
        desiredReferences.insert(record.id, references);
    }

    QSet<QString> runtimeChanges;
    for (const PortalShortcutInfo &record : desiredList) {
        const bool oldActive = !m_references.value(record.id).isEmpty();
        const bool newActive = !desiredReferences.value(record.id).isEmpty();
        if (oldActive != newActive)
            runtimeChanges.insert(record.id);
    }
    const RuntimeSnapshot snapshot = snapshotRuntime(runtimeChanges);
    if (!transitionRuntime(runtimeChanges, desiredRecords, desiredReferences, snapshot, error))
        return std::nullopt;

    QString persistenceError;
    if (!m_store->apply(desiredList, &persistenceError)) {
        reconcileRuntime(snapshot, appId);
        setError(error, FailedError, persistenceError);
        return std::nullopt;
    }

    publishReferences(affectedIds, desiredReferences, desiredRecords);
    m_transitionInProgress = false;
    m_sessions.insert(sessionToken, newSession);
    if (m_closedTokens.contains(sessionToken)
        || lifecycleGeneration != m_lifecycleGeneration) {
        m_queuedDeactivations.insert(sessionToken, appId);
        setError(error, UnknownObjectError, QStringLiteral("The Portal session was closed during commit"));
        return std::nullopt;
    }
    Q_EMIT shortcutsChanged(appId, decoratedShortcuts(appId));
    return boundShortcuts(m_sessions.value(sessionToken));
}

std::optional<PortalShortcutInfoList> PortalShortcutManager::restoreSessionBindings(
        const QString &appId,
        const QString &sessionToken,
        const PortalShortcutUpdateList &declared,
        const QStringList &activeShortcutIds,
        PortalOperationError *error)
{
    PortalShortcutUpdateList normalized;
    if (sessionToken.isEmpty()
        || !normalizeUpdates(appId, declared, normalized, error)) {
        if (sessionToken.isEmpty())
            setError(error, InvalidArgsError, QStringLiteral("The session token is empty"));
        return std::nullopt;
    }

    QSet<QString> declaredIds;
    for (const PortalShortcutUpdate &update : normalized)
        declaredIds.insert(update.shortcutId);
    QSet<QString> requestedActiveIds;
    for (const QString &shortcutId : activeShortcutIds) {
        if (shortcutId.isEmpty() || requestedActiveIds.contains(shortcutId)
            || !declaredIds.contains(shortcutId)) {
            setError(error,
                     InvalidArgsError,
                     QStringLiteral("The restored active shortcut set is invalid"));
            return std::nullopt;
        }
        requestedActiveIds.insert(shortcutId);
    }

    if (m_closedTokens.contains(sessionToken)) {
        setError(error, UnknownObjectError, QStringLiteral("The Portal session is already closed"));
        return std::nullopt;
    }

    const auto existingSession = m_sessions.constFind(sessionToken);
    if (existingSession != m_sessions.cend()) {
        if (existingSession->appId != appId || existingSession->request != normalized) {
            setError(error,
                     InvalidArgsError,
                     QStringLiteral("The session token was reused for a different request"));
            return std::nullopt;
        }
        return boundShortcuts(*existingSession);
    }

    const quint64 lifecycleGeneration = m_lifecycleGeneration;
    WriteGuard guard(this);
    if (!guard.isLocked()) {
        setError(error, BusyError, QStringLiteral("Another shortcut write is in progress"));
        return std::nullopt;
    }

    PortalShortcutInfoList desiredList;
    PortalShortcutInfoList activeCandidates;
    QHash<QString, PortalShortcutInfo> desiredRecords;
    SessionState restoredSession;
    restoredSession.appId = appId;
    restoredSession.request = normalized;
    for (const PortalShortcutUpdate &update : normalized) {
        const auto stored = m_store->shortcut(appId, update.shortcutId);
        if (!stored) {
            setError(error,
                     UnknownObjectError,
                     QStringLiteral("A persisted Portal shortcut record is missing"));
            return std::nullopt;
        }
        PortalShortcutInfo record = *stored;
        record.active = false;
        desiredList.append(record);
        desiredRecords.insert(record.id, record);
        restoredSession.declaredOrder.append(update.shortcutId);
        restoredSession.declaredIds.insert(update.shortcutId);
        if (requestedActiveIds.contains(update.shortcutId))
            activeCandidates.append(record);
    }

    const QSet<QString> affectedIds(desiredRecords.keyBegin(), desiredRecords.keyEnd());
    const PortalShortcutConflictList conflicts = conflictsForRecords(
            appId, activeCandidates, affectedIds);
    if (hasInternalConflict(conflicts)) {
        setError(error,
                 ConflictError,
                 QStringLiteral("The restored Portal shortcuts contain duplicate hotkeys"));
        return std::nullopt;
    }
    const QSet<QString> blockedIds = blockedShortcutIds(conflicts);

    QHash<QString, QSet<QString>> desiredReferences;
    for (const PortalShortcutInfo &record : desiredList) {
        QSet<QString> references = m_references.value(record.id);
        if (requestedActiveIds.contains(record.shortcutId)
            && record.enabled && !record.hotkeys.isEmpty()
            && !blockedIds.contains(record.shortcutId)) {
            references.insert(sessionToken);
            restoredSession.boundIds.insert(record.shortcutId);
        }
        desiredReferences.insert(record.id, references);
    }

    QSet<QString> runtimeChanges;
    for (const PortalShortcutInfo &record : desiredList) {
        const bool oldActive = !m_references.value(record.id).isEmpty();
        const bool newActive = !desiredReferences.value(record.id).isEmpty();
        if (oldActive != newActive)
            runtimeChanges.insert(record.id);
    }
    const RuntimeSnapshot snapshot = snapshotRuntime(runtimeChanges);
    if (!transitionRuntime(runtimeChanges,
                           desiredRecords,
                           desiredReferences,
                           snapshot,
                           error)) {
        return std::nullopt;
    }

    publishReferences(affectedIds, desiredReferences, desiredRecords);
    m_transitionInProgress = false;
    m_sessions.insert(sessionToken, restoredSession);
    if (m_closedTokens.contains(sessionToken)
        || lifecycleGeneration != m_lifecycleGeneration) {
        m_queuedDeactivations.insert(sessionToken, appId);
        setError(error, UnknownObjectError, QStringLiteral("The Portal session was closed during restore"));
        return std::nullopt;
    }
    Q_EMIT shortcutsChanged(appId, decoratedShortcuts(appId));
    return boundShortcuts(m_sessions.value(sessionToken));
}

std::optional<PortalShortcutInfoList> PortalShortcutManager::applySessionUpdates(
        const QString &appId,
        const QString &sessionToken,
        const PortalShortcutUpdateList &updates,
        PortalOperationError *error)
{
    const auto sessionIt = m_sessions.constFind(sessionToken);
    if (sessionIt == m_sessions.cend() || sessionIt->appId != appId) {
        setError(error, UnknownObjectError, QStringLiteral("The Portal session does not exist"));
        return std::nullopt;
    }

    PortalShortcutUpdateList normalized;
    if (!normalizeUpdates(appId, updates, normalized, error))
        return std::nullopt;
    for (const PortalShortcutUpdate &update : normalized) {
        if (!sessionIt->declaredIds.contains(update.shortcutId)) {
            setError(error,
                     InvalidArgsError,
                     QStringLiteral("An update is outside the session declaration"));
            return std::nullopt;
        }
    }

    const quint64 lifecycleGeneration = m_lifecycleGeneration;
    WriteGuard guard(this);
    if (!guard.isLocked()) {
        setError(error, BusyError, QStringLiteral("Another shortcut write is in progress"));
        return std::nullopt;
    }

    PortalShortcutInfoList desiredList;
    QHash<QString, PortalShortcutInfo> desiredRecords;
    for (const PortalShortcutUpdate &update : normalized) {
        const auto stored = m_store->shortcut(appId, update.shortcutId);
        if (!stored) {
            setError(error,
                     UnknownObjectError,
                     QStringLiteral("A declared Portal shortcut record is missing"));
            return std::nullopt;
        }
        PortalShortcutInfo record = *stored;
        // Configure owns only user binding fields. Application metadata stays
        // at the newest value accepted through Bind.
        record.hotkeys = update.hotkeys;
        record.enabled = update.enabled;
        record.active = false;
        desiredList.append(record);
        desiredRecords.insert(record.id, record);
    }

    const QSet<QString> affectedIds(desiredRecords.keyBegin(), desiredRecords.keyEnd());
    const PortalShortcutConflictList conflicts = conflictsForRecords(appId,
                                                                     desiredList,
                                                                     affectedIds);
    if (!conflicts.isEmpty()) {
        setError(error,
                 ConflictError,
                 QStringLiteral("A configured Portal shortcut is in conflict"));
        return std::nullopt;
    }

    QHash<QString, QSet<QString>> desiredReferences;
    for (const PortalShortcutInfo &record : desiredList) {
        QSet<QString> references;
        if (record.enabled && !record.hotkeys.isEmpty()) {
            for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
                if (it->appId == appId && it->declaredIds.contains(record.shortcutId))
                    references.insert(it.key());
            }
        }
        desiredReferences.insert(record.id, references);
    }

    QSet<QString> runtimeChanges;
    for (const PortalShortcutInfo &record : desiredList) {
        const bool oldActive = !m_references.value(record.id).isEmpty();
        const bool newActive = !desiredReferences.value(record.id).isEmpty();
        const auto active = m_activeConfigs.constFind(record.id);
        if (oldActive != newActive
            || (oldActive && active != m_activeConfigs.cend()
                && active->hotkeys != record.hotkeys)) {
            runtimeChanges.insert(record.id);
        }
    }

    const RuntimeSnapshot snapshot = snapshotRuntime(runtimeChanges);
    if (!transitionRuntime(runtimeChanges, desiredRecords, desiredReferences, snapshot, error))
        return std::nullopt;

    QString persistenceError;
    if (!m_store->apply(desiredList, &persistenceError)) {
        reconcileRuntime(snapshot, appId);
        setError(error, FailedError, persistenceError);
        return std::nullopt;
    }

    publishReferences(affectedIds, desiredReferences, desiredRecords);
    m_transitionInProgress = false;
    updateSessionBoundIds(affectedIds, desiredRecords);
    // Configure may enable a previously unbound action in several Sessions.
    // An application-wide active flag cannot represent their individual references.
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
        if (it->appId == appId && !m_closedTokens.contains(it.key()))
            Q_EMIT sessionShortcutsChanged(appId, it.key(), boundShortcuts(*it));
    }
    if (m_closedTokens.contains(sessionToken)
        || lifecycleGeneration != m_lifecycleGeneration) {
        m_queuedDeactivations.insert(sessionToken, appId);
        setError(error, UnknownObjectError, QStringLiteral("The Portal session was closed during configure"));
        return std::nullopt;
    }
    Q_EMIT shortcutsChanged(appId, decoratedShortcuts(appId));
    return boundShortcuts(m_sessions.value(sessionToken));
}

void PortalShortcutManager::deactivateSession(const QString &appId,
                                              const QString &sessionToken)
{
    deactivateSessionInternal(appId, sessionToken, true);
}

void PortalShortcutManager::deactivateAllSessions()
{
    ++m_lifecycleGeneration;
    m_deactivateAllPending = true;
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it)
        rememberClosedToken(it.key());

    processQueuedDeactivations();
    if (m_deactivateAllPending) {
        QTimer::singleShot(0, this, &PortalShortcutManager::processQueuedDeactivations);
    }
}

void PortalShortcutManager::processQueuedDeactivations()
{
    if (m_processingQueuedDeactivations)
        return;

    if (m_deactivateAllPending) {
        m_processingQueuedDeactivations = true;
        WriteGuard guard(this);
        if (!guard.isLocked()) {
            m_processingQueuedDeactivations = false;
            return;
        }

        QSet<QString> appIds;
        for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
            appIds.insert(it->appId);
            rememberClosedToken(it.key());
        }

        const QStringList activeIds = m_activeConfigs.keys();
        m_transitionInProgress = true;
        for (const QString &id : activeIds) {
            releasePressedShortcut(id);
            m_runtime->unregisterPortalShortcut(id);
        }
        if (!activeIds.isEmpty() && !m_runtime->commitPortalShortcuts()) {
            qCritical() << "PortalShortcutManager: failed to release all Portal shortcuts";
            for (const QString &id : activeIds)
                m_runtime->unregisterPortalShortcut(id);
            if (!m_runtime->commitPortalShortcuts())
                qCritical() << "PortalShortcutManager: release-all compensation failed";
        }

        m_sessions.clear();
        m_references.clear();
        m_activeConfigs.clear();
        m_queuedDeactivations.clear();
        m_deactivateAllPending = false;
        m_transitionInProgress = false;
        for (const QString &appId : std::as_const(appIds))
            Q_EMIT shortcutsChanged(appId, decoratedShortcuts(appId));
        m_processingQueuedDeactivations = false;
        return;
    }

    if (m_queuedDeactivations.isEmpty())
        return;

    m_processingQueuedDeactivations = true;
    const auto queued = m_queuedDeactivations;
    m_queuedDeactivations.clear();
    for (auto it = queued.cbegin(); it != queued.cend(); ++it)
        deactivateSessionInternal(it.value(), it.key(), false);
    m_processingQueuedDeactivations = false;
}

void PortalShortcutManager::deactivateSessionInternal(const QString &appId,
                                                       const QString &sessionToken,
                                                       bool allowQueuedRetry)
{
    const auto sessionIt = m_sessions.find(sessionToken);
    if (sessionIt == m_sessions.end() || sessionIt->appId != appId) {
        rememberClosedToken(sessionToken);
        m_queuedDeactivations.remove(sessionToken);
        return;
    }

    WriteGuard guard(this);
    if (!guard.isLocked()) {
        rememberClosedToken(sessionToken);
        const bool newlyQueued = !m_queuedDeactivations.contains(sessionToken);
        m_queuedDeactivations.insert(sessionToken, appId);
        if (allowQueuedRetry && newlyQueued) {
            QTimer::singleShot(0, this, &PortalShortcutManager::processQueuedDeactivations);
        }
        return;
    }
    m_queuedDeactivations.remove(sessionToken);

    QSet<QString> becameInactive;
    for (auto it = m_references.begin(); it != m_references.end(); ++it) {
        if (!it->remove(sessionToken))
            continue;
        if (it->isEmpty())
            becameInactive.insert(it.key());
    }

    m_transitionInProgress = true;
    for (const QString &id : becameInactive) {
        releasePressedShortcut(id);
        m_runtime->unregisterPortalShortcut(id);
        m_activeConfigs.remove(id);
    }
    if (!becameInactive.isEmpty() && !m_runtime->commitPortalShortcuts()) {
        qCritical() << "PortalShortcutManager: failed to commit session deactivation";
        // One bounded cleanup attempt; do not keep the application reference
        // alive after its Session has closed.
        for (const QString &id : becameInactive)
            m_runtime->unregisterPortalShortcut(id);
        if (!m_runtime->commitPortalShortcuts())
            qCritical() << "PortalShortcutManager: deactivation compensation failed";
    }
    m_transitionInProgress = false;

    m_sessions.erase(sessionIt);
    rememberClosedToken(sessionToken);
    Q_EMIT shortcutsChanged(appId, decoratedShortcuts(appId));
}

void PortalShortcutManager::rememberClosedToken(const QString &sessionToken)
{
    if (sessionToken.isEmpty() || m_closedTokens.contains(sessionToken))
        return;

    m_closedTokens.insert(sessionToken);
    m_closedTokenOrder.enqueue(sessionToken);
    while (m_closedTokenOrder.size() > MaximumRememberedClosedSessions)
        m_closedTokens.remove(m_closedTokenOrder.dequeue());
}

std::optional<PortalShortcutConflictList> PortalShortcutManager::checkConflicts(
        const QString &appId,
        const PortalShortcutUpdateList &shortcuts,
        PortalOperationError *error) const
{
    PortalShortcutUpdateList normalized;
    if (!normalizeUpdates(appId, shortcuts, normalized, error))
        return std::nullopt;

    PortalShortcutInfoList records;
    QSet<QString> affectedIds;
    for (const PortalShortcutUpdate &update : normalized) {
        PortalShortcutInfo record;
        record.id = PortalShortcutStore::internalId(appId, update.shortcutId);
        record.appId = appId;
        record.shortcutId = update.shortcutId;
        record.description = update.description;
        record.hotkeys = update.hotkeys;
        record.preferredKeys = update.preferredKeys;
        record.enabled = update.enabled;
        records.append(record);
        affectedIds.insert(record.id);
    }
    return conflictsForRecords(appId, records, affectedIds);
}

std::optional<KeyConfig> PortalShortcutManager::activeConfig(const QString &id) const
{
    const auto config = m_activeConfigs.constFind(id);
    if (config == m_activeConfigs.cend())
        return std::nullopt;
    return *config;
}

bool PortalShortcutManager::handleKeyEvent(const QString &id, int eventFlag)
{
    // Releases for existing presses must survive nested commit/persistence loops.
    if (eventFlag == KeyEventFlag::Release && releasePressedShortcut(id))
        return true;
    if (m_transitionInProgress)
        return m_activeConfigs.contains(id);

    const auto config = m_activeConfigs.constFind(id);
    if (config == m_activeConfigs.cend())
        return false;

    const auto record = m_store->shortcut(config->appId, config->triggerValue.value(0));
    if (!record)
        return true;

    const qulonglong timestamp = static_cast<qulonglong>(m_eventClock.elapsed());
    if (eventFlag == KeyEventFlag::Press && !m_pressedConfigs.contains(id)) {
        m_pressedConfigs.insert(id, *config);
        Q_EMIT shortcutPressed(record->appId, record->shortcutId, timestamp);
    }
    return true;
}

bool PortalShortcutManager::releasePressedShortcut(const QString &id)
{
    const auto it = m_pressedConfigs.find(id);
    if (it == m_pressedConfigs.end())
        return false;
    const KeyConfig config = *it;
    m_pressedConfigs.erase(it);
    Q_EMIT shortcutReleased(config.appId, config.triggerValue.value(0),
                            static_cast<qulonglong>(m_eventClock.elapsed()));
    return true;
}

void PortalShortcutManager::restoreRuntimeBindings()
{
    if (m_activeConfigs.isEmpty())
        return;
    WriteGuard guard(this);
    if (!guard.isLocked())
        return;

    QSet<QString> ids;
    for (auto it = m_activeConfigs.cbegin(); it != m_activeConfigs.cend(); ++it)
        ids.insert(it.key());
    // Reconnect has the same requirement as failed compensation: publish only
    // the previously bound live subset that the backend can actually restore.
    reconcileRuntime(snapshotRuntime(ids), {});
}

void PortalShortcutManager::handleRegistrationFailure(const QString &id)
{
    if (!m_activeConfigs.contains(id) && m_references.value(id).isEmpty())
        return;

    const bool wasTransitioning = m_transitionInProgress;
    m_transitionInProgress = true;
    releasePressedShortcut(id);
    const KeyConfig config = m_activeConfigs.take(id);
    m_references.remove(id);
    QSet<QString> appIds;
    if (!config.appId.isEmpty())
        appIds.insert(config.appId);
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        QString failedShortcutId;
        for (const QString &shortcutId : it->boundIds) {
            if (PortalShortcutStore::internalId(it->appId, shortcutId) == id) {
                failedShortcutId = shortcutId;
                break;
            }
        }
        if (!failedShortcutId.isEmpty()) {
            it->boundIds.remove(failedShortcutId);
            appIds.insert(it->appId);
        }
    }
    m_transitionInProgress = wasTransitioning;
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
        if (appIds.contains(it->appId) && !m_closedTokens.contains(it.key()))
            Q_EMIT sessionShortcutsChanged(it->appId, it.key(), boundShortcuts(*it));
    }
    for (const QString &appId : std::as_const(appIds))
        Q_EMIT shortcutsChanged(appId, decoratedShortcuts(appId));
}

bool PortalShortcutManager::containsControlCharacter(const QString &text)
{
    if (!text.isValidUtf16())
        return true;
    for (char32_t character : text.toUcs4()) {
        const QChar::Category category = QChar::category(character);
        if (category == QChar::Other_Control || category == QChar::Other_Format
            || category == QChar::Other_Surrogate || category == QChar::Other_PrivateUse
            || category == QChar::Other_NotAssigned) {
            return true;
        }
    }
    return false;
}

QString PortalShortcutManager::normalizeInternalHotkey(const QString &hotkey)
{
    const QString trimmed = hotkey.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > MaximumHotkeyLength
        || containsControlCharacter(trimmed)) {
        return {};
    }

    const QKeySequence sequence = QKeySequence::fromString(trimmed, QKeySequence::PortableText);
    if (sequence.isEmpty() || sequence.count() != 1)
        return {};
    return sequence.toString(QKeySequence::PortableText);
}

bool PortalShortcutManager::normalizeUpdates(const QString &appId,
                                             const PortalShortcutUpdateList &input,
                                             PortalShortcutUpdateList &normalized,
                                             PortalOperationError *error)
{
    if (appId.isEmpty() || appId.size() > MaximumAppIdLength
        || containsControlCharacter(appId)
        || input.size() > PortalShortcutStore::MaximumShortcutsPerApplication) {
        setError(error, InvalidArgsError, QStringLiteral("The application or shortcut count is invalid"));
        return false;
    }

    QSet<QString> ids;
    normalized.clear();
    normalized.reserve(input.size());
    for (PortalShortcutUpdate update : input) {
        if (update.shortcutId.isEmpty()
            || update.shortcutId.size() > MaximumShortcutIdLength
            || containsControlCharacter(update.shortcutId)
            || ids.contains(update.shortcutId)
            || update.description.size() > MaximumDescriptionLength
            || containsControlCharacter(update.description)
            || update.hotkeys.size() > 1
            || update.preferredKeys.size() > 1) {
            setError(error, InvalidArgsError, QStringLiteral("A Portal shortcut update is invalid"));
            return false;
        }
        ids.insert(update.shortcutId);

        for (QStringList *hotkeys : {&update.hotkeys, &update.preferredKeys}) {
            if (hotkeys->isEmpty())
                continue;
            const QString value = normalizeInternalHotkey(hotkeys->constFirst());
            if (value.isEmpty()) {
                setError(error,
                         InvalidArgsError,
                         QStringLiteral("A Portal shortcut contains an invalid hotkey"));
                return false;
            }
            *hotkeys = {value};
        }
        normalized.append(update);
    }
    return true;
}

KeyConfig PortalShortcutManager::toRuntimeConfig(const PortalShortcutInfo &shortcut)
{
    KeyConfig config;
    config.origin = ShortcutOrigin::Portal;
    config.appId = shortcut.appId;
    config.subPath = shortcut.id;
    // The Portal contract requires the description property to be present, but
    // does not require a non-empty string. Keep the externally visible value
    // unchanged and use the stable action ID only for the runtime identity.
    config.displayName = shortcut.description.isEmpty() ? shortcut.shortcutId
                                                        : shortcut.description;
    config.category = QStringLiteral("Applications");
    config.enabled = shortcut.enabled;
    config.modifiable = true;
    config.triggerType = static_cast<int>(TriggerType::PortalNotification);
    config.triggerValue = {shortcut.shortcutId};
    config.hotkeys = shortcut.hotkeys;
    config.keyEventFlags = KeyEventFlag::Press | KeyEventFlag::Release;
    return config;
}

QString PortalShortcutManager::conflictType(const PortalRuntimeConflict &conflict)
{
    if (conflict.origin == ShortcutOrigin::Portal)
        return QStringLiteral("ExistingPortal");
    if (conflict.origin == ShortcutOrigin::Custom)
        return QStringLiteral("ExistingCustom");
    return conflict.modifiable ? QStringLiteral("ExistingBuiltin")
                               : QStringLiteral("SystemReserved");
}

void PortalShortcutManager::setError(PortalOperationError *error,
                                     const QString &name,
                                     const QString &message)
{
    if (error) {
        error->name = name;
        error->message = message;
    }
}

PortalShortcutInfoList PortalShortcutManager::decoratedShortcuts(const QString &appId) const
{
    PortalShortcutInfoList shortcuts = m_store->shortcutsForApp(appId);
    for (PortalShortcutInfo &shortcut : shortcuts)
        shortcut.active = !m_references.value(shortcut.id).isEmpty();
    return shortcuts;
}

PortalShortcutInfoList PortalShortcutManager::boundShortcuts(const SessionState &session) const
{
    PortalShortcutInfoList shortcuts;
    for (const QString &shortcutId : session.declaredOrder) {
        if (!session.boundIds.contains(shortcutId))
            continue;
        const auto record = m_store->shortcut(session.appId, shortcutId);
        if (record) {
            PortalShortcutInfo bound = *record;
            bound.active = true;
            shortcuts.append(bound);
        }
    }
    return shortcuts;
}

PortalShortcutConflictList PortalShortcutManager::conflictsForRecords(
        const QString &appId,
        const PortalShortcutInfoList &records,
        const QSet<QString> &affectedIds) const
{
    PortalShortcutConflictList conflicts;
    QHash<QString, QString> requestHotkeys;
    for (const PortalShortcutInfo &record : records) {
        if (!record.enabled || record.hotkeys.isEmpty())
            continue;
        const QString hotkey = record.hotkeys.constFirst();
        const auto duplicate = requestHotkeys.constFind(hotkey);
        if (duplicate != requestHotkeys.cend()) {
            conflicts.append({record.shortcutId,
                              hotkey,
                              QStringLiteral("RequestInternal"),
                              appId,
                              *duplicate});
            continue;
        }
        requestHotkeys.insert(hotkey, record.shortcutId);

        const PortalRuntimeConflict runtimeConflict = m_runtime->lookupPortalConflict(
                hotkey, QStringList(affectedIds.cbegin(), affectedIds.cend()));
        if (runtimeConflict.isValid()) {
            conflicts.append({record.shortcutId,
                              hotkey,
                              conflictType(runtimeConflict),
                              runtimeConflict.appId,
                              runtimeConflict.shortcutId});
        }
    }
    return conflicts;
}

bool PortalShortcutManager::hasInternalConflict(
        const PortalShortcutConflictList &conflicts) const
{
    return std::any_of(conflicts.cbegin(), conflicts.cend(), [](const auto &conflict) {
        return conflict.conflictType == QLatin1String("RequestInternal");
    });
}

QSet<QString> PortalShortcutManager::blockedShortcutIds(
        const PortalShortcutConflictList &conflicts) const
{
    QSet<QString> ids;
    for (const PortalShortcutConflict &conflict : conflicts)
        ids.insert(conflict.shortcutId);
    return ids;
}

PortalShortcutManager::RuntimeSnapshot PortalShortcutManager::snapshotRuntime(
        const QSet<QString> &affectedIds) const
{
    RuntimeSnapshot snapshot;
    snapshot.affectedIds = affectedIds;
    for (const QString &id : affectedIds) {
        if (m_activeConfigs.contains(id))
            snapshot.activeConfigs.insert(id, m_activeConfigs.value(id));
        snapshot.references.insert(id, m_references.value(id));
    }
    return snapshot;
}

bool PortalShortcutManager::transitionRuntime(
        const QSet<QString> &affectedIds,
        const QHash<QString, PortalShortcutInfo> &desiredRecords,
        const QHash<QString, QSet<QString>> &desiredReferences,
        const RuntimeSnapshot &snapshot,
        PortalOperationError *error)
{
    if (affectedIds.isEmpty())
        return true;

    m_transitionInProgress = true;
    for (const QString &id : affectedIds) {
        releasePressedShortcut(id);
        if (m_activeConfigs.contains(id))
            m_runtime->unregisterPortalShortcut(id);
        m_activeConfigs.remove(id);
    }

    bool registered = true;
    for (const QString &id : affectedIds) {
        if (desiredReferences.value(id).isEmpty())
            continue;
        const auto desired = desiredRecords.constFind(id);
        if (desired == desiredRecords.cend()) {
            registered = false;
            break;
        }
        const KeyConfig config = toRuntimeConfig(*desired);
        m_activeConfigs.insert(id, config);
        if (!m_runtime->registerPortalShortcut(config)) {
            registered = false;
            break;
        }
    }

    if (!registered || !m_runtime->commitPortalShortcuts()) {
        if (!restoreRuntime(snapshot)) {
            reconcileRuntime(snapshot, desiredRecords.isEmpty()
                    ? QString() : desiredRecords.cbegin()->appId);
        }
        m_transitionInProgress = false;
        setError(error,
                 FailedError,
                 QStringLiteral("The input backend rejected the Portal shortcut batch"));
        return false;
    }
    return true;
}

bool PortalShortcutManager::restoreRuntime(const RuntimeSnapshot &snapshot)
{
    if (snapshot.affectedIds.isEmpty())
        return true;

    m_transitionInProgress = true;
    for (const QString &id : snapshot.affectedIds) {
        releasePressedShortcut(id);
        if (m_activeConfigs.contains(id))
            m_runtime->unregisterPortalShortcut(id);
        m_activeConfigs.remove(id);
    }

    bool restored = true;
    for (auto it = snapshot.activeConfigs.cbegin(); it != snapshot.activeConfigs.cend(); ++it) {
        m_activeConfigs.insert(it.key(), it.value());
        restored = m_runtime->registerPortalShortcut(it.value()) && restored;
    }
    restored = m_runtime->commitPortalShortcuts() && restored;
    for (const QString &id : snapshot.affectedIds)
        m_references.insert(id, snapshot.references.value(id));
    m_transitionInProgress = !restored;
    if (!restored)
        qCritical() << "PortalShortcutManager: runtime compensation failed";
    return restored;
}

void PortalShortcutManager::reconcileRuntime(const RuntimeSnapshot &snapshot,
                                              const QString &affectedAppId)
{
    QSet<QString> appIds;
    if (!affectedAppId.isEmpty())
        appIds.insert(affectedAppId);
    QSet<QString> cleanupIds = snapshot.affectedIds;
    for (auto it = m_activeConfigs.cbegin(); it != m_activeConfigs.cend(); ++it) {
        cleanupIds.insert(it.key());
        appIds.insert(it->appId);
    }
    for (auto it = m_references.cbegin(); it != m_references.cend(); ++it)
        cleanupIds.insert(it.key());
    for (const SessionState &session : std::as_const(m_sessions))
        appIds.insert(session.appId);

    m_transitionInProgress = true;
    for (const QString &id : std::as_const(cleanupIds)) {
        releasePressedShortcut(id);
        m_runtime->unregisterPortalShortcut(id);
    }
    m_activeConfigs.clear();
    const bool cleaned = m_runtime->commitPortalShortcuts();
    QString loadError;
    const bool loaded = m_store->load(&loadError);
    QHash<QString, QSet<QString>> rebuiltReferences;
    QHash<QString, PortalShortcutInfo> records;
    if (cleaned && loaded) {
        // Only previously granted live-session subsets may be reactivated.
        for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
            if (m_closedTokens.contains(it.key()))
                continue;
            for (const QString &shortcutId : it->boundIds) {
                const auto record = m_store->shortcut(it->appId, shortcutId);
                if (!record || !record->enabled || record->hotkeys.isEmpty())
                    continue;
                records.insert(record->id, *record);
                rebuiltReferences[record->id].insert(it.key());
            }
        }
        QStringList ids = records.keys();
        std::sort(ids.begin(), ids.end());
        for (const QString &id : std::as_const(ids)) {
            const KeyConfig config = toRuntimeConfig(records.value(id));
            m_activeConfigs.insert(id, config);
            if (!m_runtime->registerPortalShortcut(config)) {
                m_runtime->unregisterPortalShortcut(id);
                m_activeConfigs.remove(id);
                rebuiltReferences.remove(id);
                qWarning() << "PortalShortcutManager: binding unavailable after reconciliation" << id;
            }
        }
    }

    const bool rebuilt = cleaned && loaded && m_runtime->commitPortalShortcuts();
    if (!rebuilt) {
        for (const QString &id : m_activeConfigs.keys())
            m_runtime->unregisterPortalShortcut(id);
        m_activeConfigs.clear();
        rebuiltReferences.clear();
        if (!m_runtime->commitPortalShortcuts())
            qCritical() << "PortalShortcutManager: reconciliation cleanup failed";
        qCritical() << "PortalShortcutManager: reconciliation failed" << loadError;
    }
    m_references = rebuiltReferences;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        QSet<QString> boundIds;
        for (const QString &shortcutId : it->boundIds) {
            const QString id = PortalShortcutStore::internalId(it->appId, shortcutId);
            if (m_references.value(id).contains(it.key()))
                boundIds.insert(shortcutId);
        }
        it->boundIds = boundIds;
    }
    m_transitionInProgress = false;
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
        if (!m_closedTokens.contains(it.key()))
            Q_EMIT sessionShortcutsChanged(it->appId, it.key(), boundShortcuts(*it));
    }
    for (const QString &appId : std::as_const(appIds))
        Q_EMIT shortcutsChanged(appId, decoratedShortcuts(appId));
}

void PortalShortcutManager::publishReferences(
        const QSet<QString> &affectedIds,
        const QHash<QString, QSet<QString>> &desiredReferences,
        const QHash<QString, PortalShortcutInfo> &desiredRecords)
{
    for (const QString &id : affectedIds) {
        m_references.insert(id, desiredReferences.value(id));
        if (desiredReferences.value(id).isEmpty()) {
            m_activeConfigs.remove(id);
        } else if (desiredRecords.contains(id)) {
            m_activeConfigs.insert(id, toRuntimeConfig(desiredRecords.value(id)));
        }
    }
}

void PortalShortcutManager::updateSessionBoundIds(
        const QSet<QString> &affectedIds,
        const QHash<QString, PortalShortcutInfo> &desiredRecords)
{
    for (auto sessionIt = m_sessions.begin(); sessionIt != m_sessions.end(); ++sessionIt) {
        for (const QString &id : affectedIds) {
            const auto desired = desiredRecords.constFind(id);
            if (desired == desiredRecords.cend() || desired->appId != sessionIt->appId)
                continue;
            const QString shortcutId = desired->shortcutId;
            if (!sessionIt->declaredIds.contains(shortcutId))
                continue;
            if (m_references.value(id).contains(sessionIt.key()))
                sessionIt->boundIds.insert(shortcutId);
            else
                sessionIt->boundIds.remove(shortcutId);
        }
    }
}
