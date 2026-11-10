// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "globalshortcutsprovider.h"

#include "globalshortcutsdialog.h"
#include "shortcutcapturecontroller.h"
#include "waylandforeignparent.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QTimer>

namespace {

constexpr auto BackendService = "org.freedesktop.impl.portal.desktop.dde";
constexpr auto BackendPath = "/org/freedesktop/portal/desktop";
constexpr auto ManagementInterface = "org.deepin.dde.impl.portal.GlobalShortcutsManagement1";
constexpr auto InvalidArgsError = "org.freedesktop.DBus.Error.InvalidArgs";
constexpr auto LimitsError = "org.freedesktop.portal.Error.LimitsExceeded";

} // namespace

GlobalShortcutsProvider::GlobalShortcutsProvider(QObject *parent)
    : QObject(parent)
    , m_captureController(new ShortcutCaptureController(this))
    , m_idleTimer(new QTimer(this))
{
    registerPortalShortcutMetaTypes();
    if (QGuiApplication::platformName() == QLatin1String("wayland"))
        m_foreignParent = new WaylandForeignParent(this);

    m_backendWatcher = new QDBusServiceWatcher(QString::fromLatin1(BackendService),
                                               QDBusConnection::sessionBus(),
                                               QDBusServiceWatcher::WatchForUnregistration,
                                               this);
    connect(m_backendWatcher,
            &QDBusServiceWatcher::serviceUnregistered,
            this,
            [this] { cancelAll(); });

    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(60000);
    connect(m_idleTimer, &QTimer::timeout, qApp, &QCoreApplication::quit);
    updateIdleTimer();
}

void GlobalShortcutsProvider::BeginBind(
        const QString &requestId,
        const QDBusObjectPath &sessionHandle,
        const QString &appId,
        const QString &parentWindow,
        const PortalShortcutUpdateList &shortcuts)
{
    if (!verifyBackendCaller())
        return;
    if (requestId.isEmpty() || sessionHandle.path().isEmpty() || appId.isEmpty()
            || m_bindRequests.contains(requestId)) {
        sendErrorReply(QString::fromLatin1(InvalidArgsError),
                       QStringLiteral("Invalid or duplicate GlobalShortcuts bind request"));
        return;
    }
    if (shortcuts.size() > 100) {
        sendErrorReply(QString::fromLatin1(LimitsError),
                       QStringLiteral("Too many GlobalShortcuts actions"));
        return;
    }

    auto *dialog = new GlobalShortcutsDialog(GlobalShortcutsDialog::Mode::Bind,
                                             appId,
                                             parentWindow,
                                             shortcuts,
                                             m_captureController,
                                             m_foreignParent);
    m_bindRequests.insert(requestId, {dialog, false});
    connect(dialog,
            &GlobalShortcutsDialog::bindAccepted,
            this,
            [this, requestId](const PortalShortcutUpdateList &updates) {
                completeBind(requestId, 0, updates);
            });
    connect(dialog, &GlobalShortcutsDialog::bindCancelled, this, [this, requestId] {
        completeBind(requestId, 1, {});
    });
    connect(dialog, &QObject::destroyed, this, [this, requestId] {
        m_bindRequests.remove(requestId);
        updateIdleTimer();
    });
    dialog->present();
    updateIdleTimer();
}

void GlobalShortcutsProvider::ConfigureShortcuts(
        const QDBusObjectPath &sessionHandle,
        const QString &appId,
        const QString &parentWindow,
        const PortalShortcutInfoList &shortcuts,
        const QVariantMap &options)
{
    if (!verifyBackendCaller())
        return;
    const QString sessionPath = sessionHandle.path();
    if (sessionPath.isEmpty() || appId.isEmpty() || shortcuts.size() > 100) {
        sendErrorReply(QString::fromLatin1(InvalidArgsError),
                       QStringLiteral("Invalid GlobalShortcuts configure request"));
        return;
    }

    const QString activationToken = options.value(QStringLiteral("activation_token")).toString();
    if (GlobalShortcutsDialog *existing = m_configureDialogs.value(sessionPath)) {
        existing->present(activationToken);
        return;
    }

    auto *dialog = new GlobalShortcutsDialog(GlobalShortcutsDialog::Mode::Configure,
                                             appId,
                                             parentWindow,
                                             toUpdates(shortcuts),
                                             m_captureController,
                                             m_foreignParent);
    m_configureDialogs.insert(sessionPath, dialog);
    const QPointer<GlobalShortcutsDialog> guardedDialog(dialog);
    connect(dialog,
            &GlobalShortcutsDialog::applyRequested,
            this,
            [this, sessionPath, guardedDialog](const PortalShortcutUpdateList &updates) {
                if (guardedDialog && m_configureDialogs.value(sessionPath) == guardedDialog)
                    applyConfigure(sessionPath, updates);
            });
    connect(dialog, &QDialog::finished, this, [this, sessionPath, guardedDialog] {
        if (m_configureDialogs.value(sessionPath) == guardedDialog)
            removeConfigureDialog(sessionPath);
    });
    connect(dialog, &QObject::destroyed, this, [this, sessionPath, guardedDialog] {
        if (m_configureDialogs.value(sessionPath) == guardedDialog)
            m_configureDialogs.remove(sessionPath);
        updateIdleTimer();
    });
    dialog->present(activationToken);
    updateIdleTimer();
}

void GlobalShortcutsProvider::CancelRequest(const QString &requestId)
{
    if (!verifyBackendCaller())
        return;
    if (requestId.isEmpty()) {
        sendErrorReply(QString::fromLatin1(InvalidArgsError),
                       QStringLiteral("The request id is empty"));
        return;
    }
    if (m_configureDialogs.contains(requestId)) {
        if (GlobalShortcutsDialog *dialog = m_configureDialogs.value(requestId))
            dialog->cancelSilently();
        removeConfigureDialog(requestId);
        return;
    }
    removeBindRequest(requestId, true);
}

bool GlobalShortcutsProvider::verifyBackendCaller()
{
    if (!calledFromDBus())
        return true;

    QDBusConnectionInterface *interface = QDBusConnection::sessionBus().interface();
    const QDBusReply<QString> owner = interface
            ? interface->serviceOwner(QString::fromLatin1(BackendService))
            : QDBusReply<QString>();
    if (!owner.isValid() || owner.value().isEmpty() || owner.value() != message().service()) {
        sendErrorReply(QDBusError::AccessDenied,
                       QStringLiteral("Only the current DDE portal backend may use this provider"));
        return false;
    }
    return true;
}

void GlobalShortcutsProvider::completeBind(
        const QString &requestId,
        uint response,
        const PortalShortcutUpdateList &selected)
{
    auto request = m_bindRequests.find(requestId);
    if (request == m_bindRequests.end() || request->completing)
        return;
    request->completing = true;
    if (request->dialog)
        request->dialog->setBusy(true);

    QDBusInterface management(QString::fromLatin1(BackendService),
                              QString::fromLatin1(BackendPath),
                              QString::fromLatin1(ManagementInterface),
                              QDBusConnection::sessionBus());
    QDBusPendingCall call = management.asyncCall(QStringLiteral("CompleteBind"),
                                                 requestId,
                                                 response,
                                                 QVariant::fromValue(selected));
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, requestId] {
        QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        auto request = m_bindRequests.find(requestId);
        if (request == m_bindRequests.end())
            return;
        if (reply.isError()) {
            request->completing = false;
            if (request->dialog) {
                request->dialog->setBusy(false);
                request->dialog->showError(reply.error().message());
                request->dialog->present();
            }
            return;
        }
        removeBindRequest(requestId, true);
    });
}

void GlobalShortcutsProvider::applyConfigure(
        const QString &sessionPath,
        const PortalShortcutUpdateList &updates)
{
    const QPointer<GlobalShortcutsDialog> dialog = m_configureDialogs.value(sessionPath);
    if (!dialog)
        return;
    dialog->setBusy(true);
    dialog->showError({});

    QDBusInterface management(QString::fromLatin1(BackendService),
                              QString::fromLatin1(BackendPath),
                              QString::fromLatin1(ManagementInterface),
                              QDBusConnection::sessionBus());
    QDBusPendingCall call = management.asyncCall(
            QStringLiteral("CheckSessionShortcutConflicts"),
            QVariant::fromValue(QDBusObjectPath(sessionPath)),
            QVariant::fromValue(updates));
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher,
            &QDBusPendingCallWatcher::finished,
            this,
            [this, watcher, sessionPath, updates, dialog] {
                QDBusPendingReply<PortalShortcutConflictList> reply = *watcher;
                watcher->deleteLater();
                if (!dialog || m_configureDialogs.value(sessionPath) != dialog)
                    return;
                if (reply.isError()) {
                    dialog->setBusy(false);
                    dialog->showError(reply.error().message());
                    return;
                }
                if (!reply.value().isEmpty()) {
                    dialog->setBusy(false);
                    dialog->showError(conflictMessage(reply.value()));
                    return;
                }
                callApplyConfigure(sessionPath, updates, dialog);
            });
}

void GlobalShortcutsProvider::callApplyConfigure(
        const QString &sessionPath,
        const PortalShortcutUpdateList &updates,
        QPointer<GlobalShortcutsDialog> dialog)
{
    QDBusInterface management(QString::fromLatin1(BackendService),
                              QString::fromLatin1(BackendPath),
                              QString::fromLatin1(ManagementInterface),
                              QDBusConnection::sessionBus());
    QDBusPendingCall call = management.asyncCall(QStringLiteral("ApplySessionShortcuts"),
                                                 QVariant::fromValue(QDBusObjectPath(sessionPath)),
                                                 QVariant::fromValue(updates));
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, sessionPath, dialog] {
        QDBusPendingReply<PortalShortcutInfoList> reply = *watcher;
        watcher->deleteLater();
        if (!dialog || m_configureDialogs.value(sessionPath) != dialog)
            return;
        dialog->setBusy(false);
        dialog->showError(reply.isError() ? reply.error().message() : QString());
    });
}

void GlobalShortcutsProvider::removeBindRequest(const QString &requestId, bool closeDialog)
{
    const BindRequest request = m_bindRequests.take(requestId);
    if (closeDialog && request.dialog) {
        request.dialog->cancelSilently();
        request.dialog->deleteLater();
    }
    updateIdleTimer();
}

void GlobalShortcutsProvider::removeConfigureDialog(const QString &sessionPath)
{
    QPointer<GlobalShortcutsDialog> dialog = m_configureDialogs.take(sessionPath);
    if (dialog)
        dialog->deleteLater();
    updateIdleTimer();
}

void GlobalShortcutsProvider::cancelAll()
{
    const QStringList bindIds = m_bindRequests.keys();
    for (const QString &requestId : bindIds)
        removeBindRequest(requestId, true);
    const QStringList sessionPaths = m_configureDialogs.keys();
    for (const QString &sessionPath : sessionPaths) {
        if (GlobalShortcutsDialog *dialog = m_configureDialogs.value(sessionPath))
            dialog->cancelSilently();
        removeConfigureDialog(sessionPath);
    }
    m_captureController->cancel();
    updateIdleTimer();
}

void GlobalShortcutsProvider::updateIdleTimer()
{
    if (m_bindRequests.isEmpty() && m_configureDialogs.isEmpty())
        m_idleTimer->start();
    else
        m_idleTimer->stop();
}

PortalShortcutUpdateList GlobalShortcutsProvider::toUpdates(
        const PortalShortcutInfoList &shortcuts)
{
    PortalShortcutUpdateList updates;
    updates.reserve(shortcuts.size());
    for (const PortalShortcutInfo &shortcut : shortcuts) {
        updates.append({shortcut.shortcutId,
                        shortcut.description,
                        shortcut.hotkeys,
                        shortcut.preferredKeys,
                        shortcut.enabled});
    }
    return updates;
}

QString GlobalShortcutsProvider::conflictMessage(
        const PortalShortcutConflictList &conflicts)
{
    if (conflicts.isEmpty())
        return {};
    const PortalShortcutConflict &first = conflicts.constFirst();
    return tr("%1 conflicts with another shortcut (%2).")
            .arg(first.shortcutId, first.conflictType);
}
