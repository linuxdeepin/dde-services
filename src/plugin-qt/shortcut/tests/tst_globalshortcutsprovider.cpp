// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "../provider/globalshortcutsdialog.h"
#include "../provider/globalshortcutsprovider.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QQueue>
#include <QScopeGuard>
#include <QTest>
#include <QThread>

namespace {

constexpr auto BackendService = "org.freedesktop.impl.portal.desktop.dde";
constexpr auto BackendPath = "/org/freedesktop/portal/desktop";
constexpr auto SessionPath = "/org/freedesktop/portal/desktop/session/test/configure";

PortalShortcutInfoList shortcuts()
{
    PortalShortcutInfo info;
    info.id = QStringLiteral("portal:test:talk");
    info.appId = QStringLiteral("org.example.Configure");
    info.shortcutId = QStringLiteral("talk");
    info.description = QStringLiteral("Talk");
    info.hotkeys = {QStringLiteral("Ctrl+Alt+T")};
    info.enabled = true;
    info.active = true;
    return {info};
}

GlobalShortcutsDialog *visibleDialog()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        auto *dialog = qobject_cast<GlobalShortcutsDialog *>(widget);
        if (dialog && dialog->isVisible())
            return dialog;
    }
    return nullptr;
}

QPushButton *applyButton(GlobalShortcutsDialog *dialog)
{
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    return buttons ? buttons->button(QDialogButtonBox::Apply) : nullptr;
}

} // namespace

class FakeManagement : public QDBusVirtualObject
{
    Q_OBJECT

public:
    bool start()
    {
        m_bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                              QStringLiteral("provider-lifecycle-management"));
        return m_bus.registerVirtualObject(QString::fromLatin1(BackendPath), this)
                && m_bus.registerService(QString::fromLatin1(BackendService));
    }

    void stop()
    {
        m_bus.unregisterService(QString::fromLatin1(BackendService));
        m_bus.unregisterObject(QString::fromLatin1(BackendPath));
        QDBusConnection::disconnectFromBus(QStringLiteral("provider-lifecycle-management"));
    }

    QString introspect(const QString &) const override
    {
        return QStringLiteral(
                "<interface name=\"org.deepin.dde.impl.portal.GlobalShortcutsManagement1\">"
                "<method name=\"CheckSessionShortcutConflicts\">"
                "<arg type=\"o\" direction=\"in\"/>"
                "<arg type=\"a(ssasasb)\" direction=\"in\"/>"
                "<arg type=\"a(sssss)\" direction=\"out\"/></method>"
                "<method name=\"ApplySessionShortcuts\">"
                "<arg type=\"o\" direction=\"in\"/>"
                "<arg type=\"a(ssasasb)\" direction=\"in\"/>"
                "<arg type=\"a(ssssasasbb)\" direction=\"out\"/></method>"
                "</interface>");
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override
    {
        if (message.member() == QLatin1String("CheckSessionShortcutConflicts")) {
            m_checks.enqueue(message);
            Q_EMIT checkReceived();
            return true;
        }
        if (message.member() == QLatin1String("ApplySessionShortcuts")) {
            m_applies.enqueue(message);
            Q_EMIT applyReceived();
            return true;
        }
        return false;
    }

    bool replyCheck(bool error)
    {
        if (m_checks.isEmpty())
            return false;
        const QDBusMessage call = m_checks.dequeue();
        return m_bus.send(error
                ? call.createErrorReply(QStringLiteral("org.freedesktop.portal.Error.Failed"),
                                         QStringLiteral("stale-check-error"))
                : call.createReply({QVariant::fromValue(PortalShortcutConflictList{})}));
    }

    bool replyApply(bool error)
    {
        if (m_applies.isEmpty())
            return false;
        const QDBusMessage call = m_applies.dequeue();
        return m_bus.send(error
                ? call.createErrorReply(QStringLiteral("org.freedesktop.portal.Error.Failed"),
                                         QStringLiteral("stale-apply-error"))
                : call.createReply({QVariant::fromValue(shortcuts())}));
    }

Q_SIGNALS:
    void checkReceived();
    void applyReceived();

private:
    QDBusConnection m_bus{QStringLiteral("provider-lifecycle-management")};
    QQueue<QDBusMessage> m_checks;
    QQueue<QDBusMessage> m_applies;
};

class TestGlobalShortcutsProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void staleCheckCannotApplyToReopenedDialog_data();
    void staleCheckCannotApplyToReopenedDialog();
    void staleApplyCannotChangeReopenedDialog_data();
    void staleApplyCannotChangeReopenedDialog();

private:
    static void configure(GlobalShortcutsProvider &provider);
    void replyCheck(bool error = false);
    void replyApply(bool error = false);
    QThread m_thread;
    FakeManagement *m_management = nullptr;
    int m_checks = 0;
    int m_applies = 0;
};

void TestGlobalShortcutsProvider::initTestCase()
{
    registerPortalShortcutMetaTypes();
    m_management = new FakeManagement;
    m_management->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_management, &QObject::deleteLater);
    connect(m_management, &FakeManagement::checkReceived, this, [this] { ++m_checks; });
    connect(m_management, &FakeManagement::applyReceived, this, [this] { ++m_applies; });
    m_thread.start();
    bool started = false;
    QVERIFY(QMetaObject::invokeMethod(m_management, [&] { started = m_management->start(); },
                                      Qt::BlockingQueuedConnection));
    QVERIFY(started);
}

void TestGlobalShortcutsProvider::cleanupTestCase()
{
    QVERIFY(QMetaObject::invokeMethod(m_management, [this] { m_management->stop(); },
                                      Qt::BlockingQueuedConnection));
    m_thread.quit();
    QVERIFY(m_thread.wait(3000));
}

void TestGlobalShortcutsProvider::init()
{
    m_checks = 0;
    m_applies = 0;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!visibleDialog());
}

void TestGlobalShortcutsProvider::configure(GlobalShortcutsProvider &provider)
{
    provider.ConfigureShortcuts(QDBusObjectPath(QString::fromLatin1(SessionPath)),
                                 shortcuts().constFirst().appId, {}, shortcuts(), {});
}

void TestGlobalShortcutsProvider::replyCheck(bool error)
{
    bool sent = false;
    QVERIFY(QMetaObject::invokeMethod(m_management, [&] { sent = m_management->replyCheck(error); },
                                      Qt::BlockingQueuedConnection));
    QVERIFY(sent);
}

void TestGlobalShortcutsProvider::replyApply(bool error)
{
    bool sent = false;
    QVERIFY(QMetaObject::invokeMethod(m_management, [&] { sent = m_management->replyApply(error); },
                                      Qt::BlockingQueuedConnection));
    QVERIFY(sent);
}

void TestGlobalShortcutsProvider::staleCheckCannotApplyToReopenedDialog_data()
{
    QTest::addColumn<bool>("error");
    QTest::newRow("success") << false;
    QTest::newRow("error") << true;
}

void TestGlobalShortcutsProvider::staleCheckCannotApplyToReopenedDialog()
{
    QFETCH(bool, error);
    GlobalShortcutsProvider provider;
    const auto closeDialog = qScopeGuard([&] {
        provider.CancelRequest(QString::fromLatin1(SessionPath));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    configure(provider);
    QPointer<GlobalShortcutsDialog> old = visibleDialog();
    QVERIFY(old);
    applyButton(old)->click();
    QTRY_COMPARE(m_checks, 1);
    provider.CancelRequest(QString::fromLatin1(SessionPath));
    configure(provider);
    QPointer<GlobalShortcutsDialog> current = visibleDialog();
    QVERIFY(current);
    QVERIFY(current != old);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!old);
    applyButton(current)->click();
    QTRY_COMPARE(m_checks, 2);

    replyCheck(error);
    QTest::qWait(100);
    QCOMPARE(m_applies, 0);
    QVERIFY(!applyButton(current)->isEnabled());
    replyCheck();
    QTRY_COMPARE(m_applies, 1);
    replyApply();
    QTRY_VERIFY(applyButton(current)->isEnabled());
}

void TestGlobalShortcutsProvider::staleApplyCannotChangeReopenedDialog_data()
{
    staleCheckCannotApplyToReopenedDialog_data();
}

void TestGlobalShortcutsProvider::staleApplyCannotChangeReopenedDialog()
{
    QFETCH(bool, error);
    GlobalShortcutsProvider provider;
    const auto closeDialog = qScopeGuard([&] {
        provider.CancelRequest(QString::fromLatin1(SessionPath));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    configure(provider);
    QPointer<GlobalShortcutsDialog> old = visibleDialog();
    QVERIFY(old);
    applyButton(old)->click();
    QTRY_COMPARE(m_checks, 1);
    replyCheck();
    QTRY_COMPARE(m_applies, 1);
    provider.CancelRequest(QString::fromLatin1(SessionPath));
    configure(provider);
    QPointer<GlobalShortcutsDialog> current = visibleDialog();
    QVERIFY(current);
    QVERIFY(current != old);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    applyButton(current)->click();
    QTRY_COMPARE(m_checks, 2);
    replyCheck();
    QTRY_COMPARE(m_applies, 2);

    replyApply(error);
    QTest::qWait(100);
    QVERIFY(!applyButton(current)->isEnabled());
    for (QLabel *label : current->findChildren<QLabel *>())
        QVERIFY(!label->text().contains(QStringLiteral("stale-apply-error")));
    replyApply();
    QTRY_VERIFY(applyButton(current)->isEnabled());
}

QTEST_MAIN(TestGlobalShortcutsProvider)

#include "tst_globalshortcutsprovider.moc"
