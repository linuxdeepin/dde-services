// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "backend/wayland/treelandshortcutwrapper.h"
#include "backend/wayland/waylandgesturehandler.h"
#include "backend/wayland/waylandkeyhandler.h"
#include "config/configloader.h"
#include "core/gesturemanager.h"
#include "core/keybindingmanager.h"
#include "core/sessionactivemonitor.h"
#include "core/translationmanager.h"
#include "wayland-treeland-shortcut-manager-v3-server-protocol.h"
#include "wayland-shortcut-test-xdg-shell-server-protocol.h"

#include <QGuiApplication>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-server.h>

namespace {

struct Binding {
    QString name;
    QString key;
    uint32_t flags = 0;
    uint32_t action = 0;
    uint32_t finger = 0;
    uint32_t direction = 0;
};

// A private, windowless Wayland server. It exercises the real Qt platform and
// generated client bindings without acquiring the desktop's shortcut manager.
class ShortcutServer
{
public:
    ShortcutServer()
    {
        m_display = wl_display_create();
        int sockets[2];
        if (!m_display || socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
            qFatal("Cannot create the private Wayland connection");
        if (!wl_client_create(m_display, sockets[0]))
            qFatal("Cannot create the test Wayland client");
        qputenv("WAYLAND_SOCKET", QByteArray::number(sockets[1]));

        wl_display_init_shm(m_display);
        wl_global_create(m_display, &wl_compositor_interface, 4, nullptr,
                         [](wl_client *client, void *, uint32_t version, uint32_t id) {
            // No surfaces are created by these windowless tests.
            wl_resource_create(client, &wl_compositor_interface, version, id);
        });
        wl_global_create(m_display, &xdg_wm_base_interface, 1, nullptr,
                         [](wl_client *client, void *, uint32_t version, uint32_t id) {
            static const struct xdg_wm_base_interface shellRequests = {
                [](wl_client *, wl_resource *resource) { wl_resource_destroy(resource); },
                nullptr,
                nullptr,
                [](wl_client *, wl_resource *, uint32_t) {},
            };
            auto *resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
            wl_resource_set_implementation(resource, &shellRequests, nullptr, nullptr);
        });
        wl_global_create(m_display, &treeland_shortcut_manager_v3_interface, 1, this,
                         [](wl_client *client, void *data, uint32_t version, uint32_t id) {
            auto *resource = wl_resource_create(client, &treeland_shortcut_manager_v3_interface,
                                                version, id);
            wl_resource_set_implementation(resource, &requests, data, destroyResource);
        });
        m_thread = std::thread([this] {
            while (!m_stopping) {
                wl_event_loop_dispatch(wl_display_get_event_loop(m_display), 5);
                {
                    std::lock_guard lock(m_mutex);
                    if (m_releaseFailures) {
                        for (const QString &name : std::as_const(m_deferredFailures))
                            sendFailure(name);
                        m_deferredFailures.clear();
                        m_releaseFailures = false;
                    }
                    for (const QString &name : std::as_const(m_activations)) {
                        if (m_owner && m_bindings.contains(name)) {
                            treeland_shortcut_manager_v3_send_activated(
                                    m_owner, name.toUtf8().constData(), 2);
                        }
                    }
                    m_activations.clear();
                }
                wl_display_flush_clients(m_display);
            }
        });
    }

    ~ShortcutServer()
    {
        m_stopping = true;
        m_thread.join();
        wl_display_destroy_clients(m_display);
        wl_display_destroy(m_display);
    }

    QMap<QString, Binding> bindings()
    {
        std::lock_guard lock(m_mutex);
        return m_bindings;
    }

    void rejectNext(bool defer = false)
    {
        std::lock_guard lock(m_mutex);
        m_rejectNext = true;
        m_deferNextFailure = defer;
    }

    void releaseFailures()
    {
        std::lock_guard lock(m_mutex);
        m_releaseFailures = true;
    }

    void activate(const QString &name)
    {
        std::lock_guard lock(m_mutex);
        m_activations.append(name);
    }

private:
    static ShortcutServer *server(wl_resource *resource)
    {
        return static_cast<ShortcutServer *>(wl_resource_get_user_data(resource));
    }

    static void destroyResource(wl_resource *resource)
    {
        auto *self = server(resource);
        std::lock_guard lock(self->m_mutex);
        if (self->m_owner == resource) {
            self->m_owner = nullptr;
            self->m_deferredFailures.clear();
        }
    }

    void sendFailure(const QString &name)
    {
        if (m_owner) {
            treeland_shortcut_manager_v3_send_bind_failure(
                    m_owner, name.toUtf8().constData(),
                    TREELAND_SHORTCUT_MANAGER_V3_BIND_ERROR_INVALID_ARGUMENT);
        }
    }

    void bind(wl_resource *resource, const Binding &binding)
    {
        std::lock_guard lock(m_mutex);
        if (m_owner != resource) {
            wl_resource_post_error(resource, TREELAND_SHORTCUT_MANAGER_V3_ERROR_NOT_ACQUIRED,
                                   "bind before acquire");
            return;
        }
        if (m_rejectNext) {
            m_rejectNext = false;
            if (m_deferNextFailure)
                m_deferredFailures.append(binding.name);
            else
                sendFailure(binding.name);
            return;
        }
        m_bindings.insert(binding.name, binding);
    }

    static const struct treeland_shortcut_manager_v3_interface requests;
    wl_display *m_display = nullptr;
    wl_resource *m_owner = nullptr;
    std::thread m_thread;
    std::atomic_bool m_stopping = false;
    std::mutex m_mutex;
    QMap<QString, Binding> m_bindings;
    QStringList m_deferredFailures;
    QStringList m_activations;
    bool m_rejectNext = false;
    bool m_deferNextFailure = false;
    bool m_releaseFailures = false;
};

const struct treeland_shortcut_manager_v3_interface ShortcutServer::requests = {
    [](wl_client *, wl_resource *resource) { wl_resource_destroy(resource); },
    [](wl_client *, wl_resource *resource) {
        auto *self = server(resource);
        std::lock_guard lock(self->m_mutex);
        if (self->m_owner) {
            wl_resource_post_error(resource, TREELAND_SHORTCUT_MANAGER_V3_ERROR_OCCUPIED,
                                   "manager already acquired");
            return;
        }
        self->m_owner = resource;
        self->m_bindings.clear();
    },
    [](wl_client *, wl_resource *resource, const char *name, const char *key,
       uint32_t flags, uint32_t action) {
        server(resource)->bind(resource, {QString::fromUtf8(name), QString::fromUtf8(key),
                                          flags, action, 0, 0});
    },
    [](wl_client *, wl_resource *resource, const char *name, uint32_t finger,
       uint32_t direction, uint32_t action) {
        server(resource)->bind(resource, {QString::fromUtf8(name), {}, 0, action, finger, direction});
    },
    [](wl_client *, wl_resource *resource, const char *name, uint32_t finger, uint32_t action) {
        server(resource)->bind(resource, {QString::fromUtf8(name), {}, 0, action, finger, 0});
    },
    [](wl_client *, wl_resource *resource, const char *name) {
        auto *self = server(resource);
        std::lock_guard lock(self->m_mutex);
        self->m_bindings.remove(QString::fromUtf8(name));
    },
    nullptr, // capture_next_shortcut is owned by the GUI client, not this service.
};

KeyConfig keyConfig(const QString &id, const QStringList &hotkeys)
{
    KeyConfig config;
    config.appId = QStringLiteral("org.deepin.dde.keybinding");
    config.subPath = id;
    config.displayName = id;
    config.enabled = true;
    config.modifiable = true;
    config.triggerType = int(TriggerType::Command);
    config.hotkeys = hotkeys;
    return config;
}

GestureConfig gestureConfig(const QString &id, int action)
{
    GestureConfig config;
    config.appId = QStringLiteral("org.deepin.dde.keybinding");
    config.subPath = id;
    config.displayName = id;
    config.enabled = true;
    config.modifiable = true;
    config.triggerType = int(TriggerType::Action);
    config.triggerValue = {QString::number(action)};
    config.gestureType = int(GestureType::Swipe);
    config.fingerCount = 3;
    config.direction = 3;
    return config;
}

} // namespace

class TestWaylandShortcuts : public QObject
{
    Q_OBJECT
public:
    explicit TestWaylandShortcuts(ShortcutServer &server) : m_server(server) {}

private slots:
    void init()
    {
        m_wrapper = std::make_unique<TreelandShortcutWrapper>();
        m_wrapper->initProtocol();
        QTRY_VERIFY(m_wrapper->isActive());
        QVERIFY(m_wrapper->syncAndWait());
    }

    void cleanup()
    {
        m_wrapper.reset();
    }

    void bindsAndActivatesWithoutCommit()
    {
        WaylandKeyHandler handler(m_wrapper.get());
        QSignalSpy activated(&handler, &AbstractKeyHandler::keyActivated);
        QVERIFY(handler.registerKey(keyConfig("command", {"Ctrl+Alt+A"})));
        QVERIFY(handler.commitSync());
        const auto bindings = m_server.bindings();
        QCOMPARE(bindings.size(), 1);
        QCOMPARE(bindings.first().action, 0U);
        QCOMPARE(bindings.first().flags, 2U);
        m_server.activate(bindings.firstKey());
        QTRY_COMPARE(activated.size(), 1);
        QCOMPARE(activated.first().first().toString(), QStringLiteral("command"));
        QVERIFY(handler.unregisterKey("command"));
        QVERIFY(handler.commitSync());
        QVERIFY(m_server.bindings().isEmpty());
    }

    void mapsGestureBusinessIdsToWireValues()
    {
        WaylandGestureHandler handler(m_wrapper.get());
        QVERIFY(handler.registerGesture(gestureConfig("maximize", 11)));
        QVERIFY(handler.registerGesture(gestureConfig("tile-left", 100)));
        auto notify = gestureConfig("service", 1);
        notify.gestureType = int(GestureType::Hold);
        QVERIFY(handler.registerGesture(notify));
        QVERIFY(handler.commitSync());
        QList<uint32_t> actions;
        for (const Binding &binding : m_server.bindings())
            actions.append(binding.action);
        std::sort(actions.begin(), actions.end());
        QCOMPARE(actions, QList<uint32_t>({0, 15, 32}));
        QVERIFY(!handler.registerGesture(gestureConfig("disabled", 0)));
    }

    void remembersFailureReceivedBeforeSync()
    {
        QSignalSpy failed(m_wrapper.get(), &TreelandShortcutWrapper::bindFailed);
        m_server.rejectNext();
        QVERIFY(m_wrapper->bindKey("rejected", "Ctrl+Alt+B", 2, 0));
        m_wrapper->flush();
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!m_wrapper->syncAndWait());
        QVERIFY(m_wrapper->bindKey("accepted", "Ctrl+Alt+C", 2, 0));
        QVERIFY(m_wrapper->syncAndWait());
        QCOMPARE(m_server.bindings().size(), 1);
    }

    void failedConfigRemovesItsPartialBindings()
    {
        WaylandKeyHandler handler(m_wrapper.get());
        QSignalSpy failed(&handler, &AbstractKeyHandler::registrationFailed);
        QVERIFY(handler.registerKey(keyConfig("unrelated", {"Ctrl+Alt+D"})));
        QVERIFY(handler.commitSync());
        m_server.rejectNext();
        QVERIFY(handler.registerKey(keyConfig("partial", {"Ctrl+Alt+E", "Ctrl+Alt+F"})));
        QVERIFY(!handler.commitSync());
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("partial"));
        QVERIFY(handler.commitSync()); // Also wait for the compensating unbinds.
        const auto bindings = m_server.bindings();
        QCOMPARE(bindings.size(), 1);
        QCOMPARE(bindings.first().key, QStringLiteral("Ctrl+Alt+D"));
    }

    void lateFailureInvalidatesOnlyItsConfig()
    {
        WaylandKeyHandler handler(m_wrapper.get());
        QSignalSpy failed(&handler, &AbstractKeyHandler::registrationFailed);
        m_server.rejectNext(true);
        QVERIFY(handler.registerKey(keyConfig("deferred", {"Ctrl+Alt+G"})));
        QVERIFY(handler.commitSync());
        m_server.releaseFailures();
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(handler.registerKey(keyConfig("unrelated", {"Ctrl+Alt+H"})));
        QVERIFY(handler.commitSync());
        QCOMPARE(m_server.bindings().size(), 1);
    }

    void lateFailureDoesNotInvalidateReplacement()
    {
        WaylandGestureHandler handler(m_wrapper.get());
        QSignalSpy failed(&handler, &AbstractGestureHandler::registrationFailed);
        QSignalSpy activated(&handler, &AbstractGestureHandler::activated);
        m_server.rejectNext(true);
        QVERIFY(handler.registerGesture(gestureConfig("replaced", 11)));
        QVERIFY(handler.commitSync());
        QVERIFY(handler.registerGesture(gestureConfig("replaced", 12)));
        QVERIFY(handler.commitSync());
        QSignalSpy wireFailure(m_wrapper.get(), &TreelandShortcutWrapper::bindFailed);
        m_server.releaseFailures();
        QTRY_COMPARE(wireFailure.size(), 1);
        QCOMPARE(failed.size(), 0);
        QVERIFY(handler.commitSync());
        const auto bindings = m_server.bindings();
        QCOMPARE(bindings.size(), 1);
        QCOMPARE(bindings.first().action, 16U);
        m_server.activate(bindings.firstKey());
        QTRY_COMPARE(activated.size(), 1);
        QCOMPARE(activated.first().first().toString(), QStringLiteral("replaced"));
    }

    void gestureModificationRestoresOldBindingAfterRejection()
    {
        ConfigLoader loader;
        TranslationManager translations;
        WaylandGestureHandler handler(m_wrapper.get());
        GestureManager manager(&loader, nullptr, &translations, &handler, nullptr, nullptr);
        const GestureConfig original = gestureConfig("editable", 11);
        emit loader.gestureConfigAdded(original);
        QCOMPARE(m_server.bindings().size(), 1);
        m_server.rejectNext();
        QVERIFY(!manager.ModifyGesture("editable", {"12"}));
        const auto bindings = m_server.bindings();
        QCOMPARE(bindings.size(), 1);
        QCOMPARE(bindings.first().action, 15U);
        QCOMPARE(manager.ListAllGestures().first().triggerValue, original.triggerValue);
    }

    void keyModificationRestoresOldBindingAfterRejection()
    {
        ConfigLoader loader;
        TranslationManager translations;
        SessionActiveMonitor session;
        WaylandKeyHandler handler(m_wrapper.get());
        KeybindingManager manager(&loader, nullptr, &translations, &handler, &session);
        const KeyConfig original = keyConfig("editable-key", {"Ctrl+Alt+I"});
        emit loader.keyConfigAdded(original);
        QCOMPARE(m_server.bindings().size(), 1);
        QSignalSpy failed(&handler, &AbstractKeyHandler::registrationFailed);
        m_server.rejectNext();
        QVERIFY(!manager.ModifyHotkeys(original.getId(), {"Ctrl+Alt+J"}));
        QCOMPARE(failed.size(), 1);
        const auto bindings = m_server.bindings();
        QCOMPARE(bindings.size(), 1);
        QCOMPARE(bindings.first().key, original.hotkeys.first());
        QCOMPARE(manager.GetShortcut(original.getId()).hotkeys,
                 QStringList({"<Control><Alt>I"}));
    }

private:
    ShortcutServer &m_server;
    std::unique_ptr<TreelandShortcutWrapper> m_wrapper;
};

int main(int argc, char **argv)
{
    QTemporaryDir runtime;
    if (!runtime.isValid())
        return 1;
    qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", runtime.filePath("config").toUtf8());
    qputenv("QT_QPA_PLATFORM", "wayland");
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    qputenv("QT_NO_XDG_DESKTOP_PORTAL", "1");
    qputenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "shm");
    ShortcutServer server;
    QGuiApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    TestWaylandShortcuts test(server);
    return QTest::qExec(&test, argc, argv);
}

#include "tst_waylandshortcuts.moc"
