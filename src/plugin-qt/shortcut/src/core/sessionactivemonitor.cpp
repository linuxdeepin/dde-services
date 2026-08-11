// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "shortcutlogging.h"

#include "sessionactivemonitor.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QVariant>

#include <functional>
#include <utility>

namespace {

constexpr auto PropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto SessionManagerService = "org.deepin.dde.SessionManager1";
constexpr auto SessionManagerPath = "/org/deepin/dde/SessionManager1";
constexpr auto SessionManagerInterface = "org.deepin.dde.SessionManager1";
constexpr auto LoginService = "org.freedesktop.login1";
constexpr auto LoginSessionInterface = "org.freedesktop.login1.Session";

QVariant unwrapDbusValue(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return qvariant_cast<QDBusVariant>(value).variant();
    return value;
}

QString objectPathString(const QVariant &value)
{
    const QVariant unwrapped = unwrapDbusValue(value);
    if (unwrapped.canConvert<QDBusObjectPath>())
        return qvariant_cast<QDBusObjectPath>(unwrapped).path();
    return unwrapped.toString();
}

class LoginSessionSignalRelay : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QString &, quint64, const QString &,
                                        const QVariantMap &, const QStringList &)>;

    LoginSessionSignalRelay(QString path, quint64 generation, Callback callback, QObject *parent)
        : QObject(parent)
        , m_path(std::move(path))
        , m_generation(generation)
        , m_callback(std::move(callback))
    {
    }

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                             const QStringList &invalidated)
    {
        m_callback(m_path, m_generation, interface, changed, invalidated);
    }

private:
    QString m_path;
    quint64 m_generation = 0;
    Callback m_callback;
};

}

SessionActiveMonitor::SessionActiveMonitor(QObject *parent)
    : QObject(parent)
{
    QDBusConnection::sessionBus().connect(QLatin1String(SessionManagerService),
                                          QLatin1String(SessionManagerPath),
                                          QLatin1String(PropertiesInterface),
                                          QStringLiteral("PropertiesChanged"),
                                          this,
                                          SLOT(onSessionManagerPropertiesChanged(QString,QVariantMap,QStringList)));

    auto *sessionManagerWatcher = new QDBusServiceWatcher(
            QLatin1String(SessionManagerService), QDBusConnection::sessionBus(),
            QDBusServiceWatcher::WatchForRegistration
                | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(sessionManagerWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [this]() { refreshSessionManagerState(); });
    connect(sessionManagerWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this]() {
        setLocked(true);
        updateCurrentSession(QString());
    });

    auto *loginWatcher = new QDBusServiceWatcher(
            QLatin1String(LoginService), QDBusConnection::systemBus(),
            QDBusServiceWatcher::WatchForRegistration
                | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(loginWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [this]() { refreshLoginSessionState(); });
    connect(loginWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this]() { setActive(false); });

    refreshSessionManagerState();
}

SessionActiveMonitor::~SessionActiveMonitor()
{
    disconnectLoginSessionSignal();
}

bool SessionActiveMonitor::isActive() const
{
    return m_active;
}

bool SessionActiveMonitor::isLocked() const
{
    return m_locked;
}

void SessionActiveMonitor::refreshSessionManagerState()
{
    QDBusInterface sessionManager(QLatin1String(SessionManagerService),
                                  QLatin1String(SessionManagerPath),
                                  QLatin1String(SessionManagerInterface),
                                  QDBusConnection::sessionBus());
    if (!sessionManager.isValid()) {
        setLocked(true);
        updateCurrentSession(QString());
        return;
    }

    setLocked(sessionManager.property("Locked").toBool());
    updateCurrentSession(objectPathString(sessionManager.property("CurrentSessionPath")));
}

void SessionActiveMonitor::refreshLoginSessionState()
{
    if (m_currentSessionPath.isEmpty()) {
        setActive(false);
        return;
    }

    QDBusInterface loginSession(QLatin1String(LoginService), m_currentSessionPath,
                                QLatin1String(LoginSessionInterface),
                                QDBusConnection::systemBus());
    setActive(loginSession.isValid() && loginSession.property("Active").toBool());
}

void SessionActiveMonitor::updateCurrentSession(const QString &path)
{
    if (path == m_currentSessionPath && !path.isEmpty()) {
        refreshLoginSessionState();
        return;
    }

    disconnectLoginSessionSignal();
    delete m_loginSessionSignalRelay;
    m_loginSessionSignalRelay = nullptr;

    m_currentSessionPath = path;
    ++m_sessionGeneration;
    setActive(false);
    if (path.isEmpty())
        return;

    const quint64 generation = m_sessionGeneration;
    m_loginSessionSignalRelay = new LoginSessionSignalRelay(
            path, generation,
            [this](const QString &signalPath, quint64 signalGeneration,
                   const QString &interface, const QVariantMap &changed,
                   const QStringList &invalidated) {
        handleLoginSessionPropertiesChanged(signalPath, signalGeneration,
                                            interface, changed, invalidated);
    }, this);
    QDBusConnection::systemBus().connect(QLatin1String(LoginService),
                                         path,
                                         QLatin1String(PropertiesInterface),
                                         QStringLiteral("PropertiesChanged"),
                                         m_loginSessionSignalRelay,
                                         SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    refreshLoginSessionState();
}

void SessionActiveMonitor::onSessionManagerPropertiesChanged(const QString &interface,
                                                              const QVariantMap &changed,
                                                              const QStringList &invalidated)
{
    if (interface != QLatin1String(SessionManagerInterface))
        return;
    if (changed.contains(QStringLiteral("Locked")))
        setLocked(unwrapDbusValue(changed.value(QStringLiteral("Locked"))).toBool());
    if (changed.contains(QStringLiteral("CurrentSessionPath")))
        updateCurrentSession(objectPathString(changed.value(QStringLiteral("CurrentSessionPath"))));
    if (invalidated.contains(QStringLiteral("Locked"))
            || invalidated.contains(QStringLiteral("CurrentSessionPath"))) {
        refreshSessionManagerState();
    }
}

void SessionActiveMonitor::handleLoginSessionPropertiesChanged(const QString &path,
                                                                quint64 generation,
                                                                const QString &interface,
                                                                const QVariantMap &changed,
                                                                const QStringList &invalidated)
{
    if (path != m_currentSessionPath || generation != m_sessionGeneration
            || interface != QLatin1String(LoginSessionInterface)) {
        return;
    }
    if (changed.contains(QStringLiteral("Active")))
        setActive(unwrapDbusValue(changed.value(QStringLiteral("Active"))).toBool());
    if (invalidated.contains(QStringLiteral("Active")))
        refreshLoginSessionState();
}

void SessionActiveMonitor::setActive(bool active)
{
    if (m_active == active)
        return;

    m_active = active;
    qCInfo(logShortcut) << "Special key session active state changed:" << active;
    emit activeChanged(active);
}

void SessionActiveMonitor::setLocked(bool locked)
{
    if (m_locked == locked)
        return;

    m_locked = locked;
    qCInfo(logShortcut) << "Shortcut session locked state changed:" << locked;
}

void SessionActiveMonitor::disconnectLoginSessionSignal()
{
    if (m_currentSessionPath.isEmpty() || !m_loginSessionSignalRelay)
        return;

    QDBusConnection::systemBus().disconnect(QLatin1String(LoginService),
                                            m_currentSessionPath,
                                            QLatin1String(PropertiesInterface),
                                            QStringLiteral("PropertiesChanged"),
                                            m_loginSessionSignalRelay,
                                            SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
}

#include "sessionactivemonitor.moc"
