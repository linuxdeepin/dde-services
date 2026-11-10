// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "portalshortcuttypes.h"
#include "shortcutconfig.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>

#include <optional>

class PortalShortcutStore;

struct PortalRuntimeConflict
{
    QString id;
    ShortcutOrigin origin = ShortcutOrigin::Builtin;
    bool modifiable = false;
    QString appId;
    QString shortcutId;

    bool isValid() const { return !id.isEmpty(); }
};

class PortalShortcutRuntime
{
public:
    virtual ~PortalShortcutRuntime() = default;

    virtual bool beginPortalWrite() = 0;
    virtual void endPortalWrite() = 0;
    virtual PortalRuntimeConflict lookupPortalConflict(
            const QString &hotkey,
            const QStringList &excludeIds) const = 0;
    virtual bool registerPortalShortcut(const KeyConfig &config) = 0;
    virtual void unregisterPortalShortcut(const QString &id) = 0;
    virtual bool commitPortalShortcuts() = 0;
};

struct PortalOperationError
{
    QString name;
    QString message;

    bool isValid() const { return !name.isEmpty(); }
};

class PortalShortcutManager : public QObject
{
    Q_OBJECT

public:
    explicit PortalShortcutManager(PortalShortcutRuntime *runtime, QObject *parent = nullptr);
    PortalShortcutManager(PortalShortcutRuntime *runtime,
                          PortalShortcutStore *store,
                          QObject *parent = nullptr);

    bool load();
    PortalShortcutInfoList listShortcuts(const QString &appId) const;
    std::optional<PortalTriggerList> normalizeTriggers(const PortalTriggerList &requested,
                                                       PortalOperationError *error) const;
    std::optional<PortalShortcutInfoList> commitSessionBindings(
            const QString &appId,
            const QString &sessionToken,
            const PortalShortcutUpdateList &requested,
            PortalOperationError *error);
    std::optional<PortalShortcutInfoList> restoreSessionBindings(
            const QString &appId,
            const QString &sessionToken,
            const PortalShortcutUpdateList &declared,
            const QStringList &activeShortcutIds,
            PortalOperationError *error);
    std::optional<PortalShortcutInfoList> applySessionUpdates(
            const QString &appId,
            const QString &sessionToken,
            const PortalShortcutUpdateList &updates,
            PortalOperationError *error);
    void deactivateSession(const QString &appId, const QString &sessionToken);
    void deactivateAllSessions();
    void processQueuedDeactivations();
    std::optional<PortalShortcutConflictList> checkConflicts(
            const QString &appId,
            const PortalShortcutUpdateList &shortcuts,
            PortalOperationError *error) const;

    std::optional<KeyConfig> activeConfig(const QString &id) const;
    QList<KeyConfig> activeConfigs() const { return m_activeConfigs.values(); }
    bool handleKeyEvent(const QString &id, int eventFlag);
    void restoreRuntimeBindings();
    // The runtime adapter calls this while holding its write guard.
    void handleRegistrationFailure(const QString &id);

Q_SIGNALS:
    void shortcutPressed(const QString &appId,
                         const QString &shortcutId,
                         qulonglong timestamp);
    void shortcutReleased(const QString &appId,
                          const QString &shortcutId,
                          qulonglong timestamp);
    void shortcutsChanged(const QString &appId,
                          const PortalShortcutInfoList &shortcuts);
    void sessionShortcutsChanged(const QString &appId,
                                 const QString &sessionToken,
                                 const PortalShortcutInfoList &shortcuts);

private:
    struct SessionState {
        QString appId;
        PortalShortcutUpdateList request;
        QStringList declaredOrder;
        QSet<QString> declaredIds;
        QSet<QString> boundIds;
    };

    struct RuntimeSnapshot {
        QSet<QString> affectedIds;
        QHash<QString, KeyConfig> activeConfigs;
        QHash<QString, QSet<QString>> references;
    };

    class WriteGuard;

    static constexpr qsizetype MaximumAppIdLength = 255;
    static constexpr qsizetype MaximumShortcutIdLength = 128;
    static constexpr qsizetype MaximumDescriptionLength = 256;
    static constexpr qsizetype MaximumHotkeyLength = 256;
    static constexpr qsizetype MaximumRememberedClosedSessions = 1024;

    static bool containsControlCharacter(const QString &text);
    static QString normalizeInternalHotkey(const QString &hotkey);
    static bool normalizeUpdates(const QString &appId,
                                 const PortalShortcutUpdateList &input,
                                 PortalShortcutUpdateList &normalized,
                                 PortalOperationError *error);
    static KeyConfig toRuntimeConfig(const PortalShortcutInfo &shortcut);
    static QString conflictType(const PortalRuntimeConflict &conflict);
    static void setError(PortalOperationError *error,
                         const QString &name,
                         const QString &message);

    PortalShortcutInfoList decoratedShortcuts(const QString &appId) const;
    PortalShortcutInfoList boundShortcuts(const SessionState &session) const;
    PortalShortcutConflictList conflictsForRecords(
            const QString &appId,
            const PortalShortcutInfoList &records,
            const QSet<QString> &affectedIds) const;
    bool hasInternalConflict(const PortalShortcutConflictList &conflicts) const;
    QSet<QString> blockedShortcutIds(const PortalShortcutConflictList &conflicts) const;
    RuntimeSnapshot snapshotRuntime(const QSet<QString> &affectedIds) const;
    bool transitionRuntime(const QSet<QString> &affectedIds,
                           const QHash<QString, PortalShortcutInfo> &desiredRecords,
                           const QHash<QString, QSet<QString>> &desiredReferences,
                           const RuntimeSnapshot &snapshot,
                           PortalOperationError *error);
    bool restoreRuntime(const RuntimeSnapshot &snapshot);
    void reconcileRuntime(const RuntimeSnapshot &snapshot, const QString &affectedAppId);
    void publishReferences(const QSet<QString> &affectedIds,
                           const QHash<QString, QSet<QString>> &desiredReferences,
                           const QHash<QString, PortalShortcutInfo> &desiredRecords);
    void updateSessionBoundIds(const QSet<QString> &affectedIds,
                               const QHash<QString, PortalShortcutInfo> &desiredRecords);
    void deactivateSessionInternal(const QString &appId,
                                   const QString &sessionToken,
                                   bool allowQueuedRetry);
    void rememberClosedToken(const QString &sessionToken);
    bool releasePressedShortcut(const QString &id);

    PortalShortcutRuntime *const m_runtime;
    PortalShortcutStore *const m_store;
    QHash<QString, SessionState> m_sessions;
    QSet<QString> m_closedTokens;
    QQueue<QString> m_closedTokenOrder;
    QHash<QString, QSet<QString>> m_references;
    QHash<QString, KeyConfig> m_activeConfigs;
    QHash<QString, KeyConfig> m_pressedConfigs;
    QHash<QString, QString> m_queuedDeactivations;
    QElapsedTimer m_eventClock;
    quint64 m_lifecycleGeneration = 0;
    bool m_deactivateAllPending = false;
    bool m_processingQueuedDeactivations = false;
    bool m_transitionInProgress = false;
};
