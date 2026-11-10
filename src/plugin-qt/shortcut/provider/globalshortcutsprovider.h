// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/portalshortcuttypes.h"

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QHash>
#include <QObject>
#include <QPointer>

class GlobalShortcutsDialog;
class QDBusPendingCallWatcher;
class QDBusServiceWatcher;
class QTimer;
class ShortcutCaptureController;
class WaylandForeignParent;

class GlobalShortcutsProvider : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.GlobalShortcutsProvider1")
    Q_PROPERTY(uint version READ version CONSTANT)

public:
    explicit GlobalShortcutsProvider(QObject *parent = nullptr);

    uint version() const { return 1; }

public Q_SLOTS:
    Q_SCRIPTABLE void BeginBind(const QString &requestId,
                                const QDBusObjectPath &sessionHandle,
                                const QString &appId,
                                const QString &parentWindow,
                                const PortalShortcutUpdateList &shortcuts);
    Q_SCRIPTABLE void ConfigureShortcuts(const QDBusObjectPath &sessionHandle,
                                         const QString &appId,
                                         const QString &parentWindow,
                                         const PortalShortcutInfoList &shortcuts,
                                         const QVariantMap &options);
    Q_SCRIPTABLE void CancelRequest(const QString &requestId);

private:
    struct BindRequest {
        QPointer<GlobalShortcutsDialog> dialog;
        bool completing = false;
    };

    bool verifyBackendCaller();
    void completeBind(const QString &requestId,
                      uint response,
                      const PortalShortcutUpdateList &selected);
    void applyConfigure(const QString &sessionPath,
                        const PortalShortcutUpdateList &updates);
    void callApplyConfigure(const QString &sessionPath,
                            const PortalShortcutUpdateList &updates,
                            QPointer<GlobalShortcutsDialog> dialog);
    void removeBindRequest(const QString &requestId, bool closeDialog);
    void removeConfigureDialog(const QString &sessionPath);
    void cancelAll();
    void updateIdleTimer();
    static PortalShortcutUpdateList toUpdates(const PortalShortcutInfoList &shortcuts);
    static QString conflictMessage(const PortalShortcutConflictList &conflicts);

    QHash<QString, BindRequest> m_bindRequests;
    QHash<QString, QPointer<GlobalShortcutsDialog>> m_configureDialogs;
    ShortcutCaptureController *const m_captureController;
    WaylandForeignParent *m_foreignParent = nullptr;
    QDBusServiceWatcher *m_backendWatcher = nullptr;
    QTimer *m_idleTimer = nullptr;
};
