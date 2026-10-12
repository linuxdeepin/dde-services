// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "backend/x11/x11keyhandler.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

#include <X11/XF86keysym.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

void sendKey(Display *display, KeySym keysym, bool pressed)
{
    const KeyCode keycode = XKeysymToKeycode(display, keysym);
    QVERIFY(keycode != 0);
    XTestFakeKeyEvent(display, keycode, pressed ? True : False, CurrentTime);
}

void sendTestChord(Display *display)
{
    sendKey(display, XK_Control_L, true);
    sendKey(display, XK_Alt_L, true);
    sendKey(display, XK_B, true);
    sendKey(display, XK_B, false);
    sendKey(display, XK_Alt_L, false);
    sendKey(display, XK_Control_L, false);
    XFlush(display);
}

void sendKeyStroke(Display *display, KeySym keysym)
{
    sendKey(display, keysym, true);
    sendKey(display, keysym, false);
    XFlush(display);
}

void clickPointer(Display *display)
{
    XTestFakeButtonEvent(display, 1, True, CurrentTime);
    XTestFakeButtonEvent(display, 1, False, CurrentTime);
    XFlush(display);
}

KeyConfig shortcut(const QString &id, const QString &hotkey)
{
    KeyConfig config;
    config.subPath = id;
    config.appId = QStringLiteral("org.deepin.dde.keybinding");
    config.displayName = id;
    config.enabled = true;
    config.modifiable = false;
    config.triggerType = static_cast<int>(TriggerType::Command);
    config.hotkeys = {hotkey};
    config.keyEventFlags = KeyEventFlag::Release;
    return config;
}

}

class TestX11GrabResilientShortcuts : public QObject
{
    Q_OBJECT

private slots:
    void legacyShortcutsActivateExactlyOnce();
    void xcbFallbackDuringRecordRestart();
    void captureReportsExplicitResults();
    void activeCaptureRejectsReplacement();
    void captureReportsTimeout();
    void explicitEndCaptureDoesNotReportResult();
    void sleepVariantsRegisterIndependently_data();
    void sleepVariantsRegisterIndependently();
};

void TestX11GrabResilientShortcuts::legacyShortcutsActivateExactlyOnce()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("No X server is available");

    X11KeyHandler handler;
    if (!handler.isAvailable()) {
        XCloseDisplay(display);
        QSKIP("X11 shortcut backend is unavailable");
    }

    const QString prefix = QStringLiteral("org.deepin.dde.keybinding.shortcut.app.");
    const QString screenshotId = prefix + QStringLiteral("screenshot");
    const QString launcherId = prefix + QStringLiteral("launcher");
    QVERIFY(handler.registerKey(shortcut(screenshotId, QStringLiteral("Ctrl+Alt+B"))));
    QVERIFY(handler.registerKey(shortcut(launcherId, QStringLiteral("Meta"))));

    QSignalSpy activationSpy(&handler, &X11KeyHandler::keyActivated);
    QVERIFY(activationSpy.isValid());

    const Window root = DefaultRootWindow(display);
    QCOMPARE(XGrabKeyboard(display, root, False, GrabModeAsync, GrabModeAsync, CurrentTime),
             GrabSuccess);

    sendTestChord(display);
    QTRY_COMPARE_WITH_TIMEOUT(activationSpy.size(), 1, 2000);
    QCOMPARE(activationSpy.at(0).at(0).toString(), screenshotId);

    sendKey(display, XK_Super_L, true);
    sendKey(display, XK_Super_L, false);
    XFlush(display);
    QTRY_COMPARE_WITH_TIMEOUT(activationSpy.size(), 2, 2000);
    QCOMPARE(activationSpy.at(1).at(0).toString(), launcherId);

    XUngrabKeyboard(display, CurrentTime);
    XSync(display, False);

    sendTestChord(display);
    QTRY_COMPARE_WITH_TIMEOUT(activationSpy.size(), 3, 2000);
    QCOMPARE(activationSpy.at(2).at(0).toString(), screenshotId);

    handler.unregisterKey(screenshotId);
    handler.unregisterKey(launcherId);
    XCloseDisplay(display);
}

void TestX11GrabResilientShortcuts::xcbFallbackDuringRecordRestart()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("No X server is available");

    X11KeyHandler handler;
    if (!handler.isAvailable()) {
        XCloseDisplay(display);
        QSKIP("X11 shortcut backend is unavailable");
    }

    const QString screenshotId = QStringLiteral(
            "org.deepin.dde.keybinding.shortcut.app.screenshot");
    QVERIFY(handler.registerKey(shortcut(screenshotId, QStringLiteral("Ctrl+Alt+B"))));
    QSignalSpy activationSpy(&handler, &X11KeyHandler::keyActivated);
    QVERIFY(activationSpy.isValid());

    // Ending capture restarts RECORD asynchronously. A shortcut pressed in
    // that interval must fall back to XCB and still activate exactly once.
    QVERIFY(handler.beginCapture(1, 30000, QStringLiteral("test-owner")));
    QVERIFY(handler.endCapture(QStringLiteral("test-owner")));
    sendTestChord(display);
    QTRY_COMPARE_WITH_TIMEOUT(activationSpy.size(), 1, 2000);
    QCOMPARE(activationSpy.constFirst().constFirst().toString(), screenshotId);
    QTest::qWait(100);
    QCOMPARE(activationSpy.size(), 1);

    handler.unregisterKey(screenshotId);
    XCloseDisplay(display);
}

void TestX11GrabResilientShortcuts::captureReportsExplicitResults()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("No X server is available");

    X11KeyHandler handler;
    if (!handler.isAvailable()) {
        XCloseDisplay(display);
        QSKIP("X11 shortcut backend is unavailable");
    }

    QSignalSpy resultSpy(&handler, &X11KeyHandler::captureResult);
    QVERIFY(resultSpy.isValid());

    QVERIFY(handler.beginCapture(10, 30000, QStringLiteral("test-owner")));
    sendKeyStroke(display, XK_Shift_L);
    QTRY_COMPARE_WITH_TIMEOUT(resultSpy.size(), 1, 2000);
    QList<QVariant> result = resultSpy.takeFirst();
    QCOMPARE(result.at(0).toULongLong(), 10ULL);
    QCOMPARE(result.at(1).toUInt(),
             static_cast<uint>(AbstractKeyHandler::CaptureInvalid));

    QVERIFY(handler.beginCapture(11, 30000, QStringLiteral("test-owner")));
    sendKeyStroke(display, XK_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(resultSpy.size(), 1, 2000);
    result = resultSpy.takeFirst();
    QCOMPARE(result.at(0).toULongLong(), 11ULL);
    QCOMPARE(result.at(1).toUInt(),
             static_cast<uint>(AbstractKeyHandler::CaptureCanceled));

    QVERIFY(handler.beginCapture(12, 30000, QStringLiteral("test-owner")));
    sendTestChord(display);
    QTRY_COMPARE_WITH_TIMEOUT(resultSpy.size(), 1, 2000);
    result = resultSpy.takeFirst();
    QCOMPARE(result.at(0).toULongLong(), 12ULL);
    QCOMPARE(result.at(1).toUInt(), static_cast<uint>(AbstractKeyHandler::CaptureSuccess));
    QCOMPARE(result.at(2).toString(), QStringLiteral("<Control><Alt>B"));

    QVERIFY(handler.beginCapture(13, 30000, QStringLiteral("test-owner")));
    clickPointer(display);
    QTRY_COMPARE_WITH_TIMEOUT(resultSpy.size(), 1, 2000);
    result = resultSpy.takeFirst();
    QCOMPARE(result.at(0).toULongLong(), 13ULL);
    QCOMPARE(result.at(1).toUInt(),
             static_cast<uint>(AbstractKeyHandler::CaptureCanceled));

    XCloseDisplay(display);
}

void TestX11GrabResilientShortcuts::activeCaptureRejectsReplacement()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("No X server is available");

    X11KeyHandler handler;
    if (!handler.isAvailable()) {
        XCloseDisplay(display);
        QSKIP("X11 shortcut backend is unavailable");
    }

    QSignalSpy resultSpy(&handler, &X11KeyHandler::captureResult);
    QVERIFY(resultSpy.isValid());

    const QString owner = QStringLiteral("test-owner");
    QVERIFY(handler.beginCapture(14, 30000, owner));
    QVERIFY(!handler.beginCapture(15, 30000, owner));
    sendTestChord(display);
    QTRY_COMPARE_WITH_TIMEOUT(resultSpy.size(), 1, 2000);
    QCOMPARE(resultSpy.constFirst().at(0).toULongLong(), 14ULL);
    QCOMPARE(resultSpy.constFirst().at(1).toUInt(),
             static_cast<uint>(AbstractKeyHandler::CaptureSuccess));

    XCloseDisplay(display);
}

void TestX11GrabResilientShortcuts::captureReportsTimeout()
{
    X11KeyHandler handler;
    if (!handler.isAvailable())
        QSKIP("X11 shortcut backend is unavailable");

    QSignalSpy resultSpy(&handler, &X11KeyHandler::captureResult);
    QVERIFY(handler.beginCapture(20, 1, QStringLiteral("test-owner")));
    QTRY_COMPARE_WITH_TIMEOUT(resultSpy.size(), 1, 2000);
    const QList<QVariant> result = resultSpy.takeFirst();
    QCOMPARE(result.at(0).toULongLong(), 20ULL);
    QCOMPARE(result.at(1).toUInt(),
             static_cast<uint>(AbstractKeyHandler::CaptureTimedOut));
}

void TestX11GrabResilientShortcuts::explicitEndCaptureDoesNotReportResult()
{
    X11KeyHandler handler;
    if (!handler.isAvailable())
        QSKIP("X11 shortcut backend is unavailable");

    QSignalSpy resultSpy(&handler, &X11KeyHandler::captureResult);
    QVERIFY(handler.beginCapture(30, 30000, QStringLiteral("test-owner")));
    QVERIFY(handler.endCapture(QStringLiteral("test-owner")));
    QTest::qWait(50);
    QCOMPARE(resultSpy.size(), 0);
}

void TestX11GrabResilientShortcuts::sleepVariantsRegisterIndependently_data()
{
    QTest::addColumn<bool>("hasSleep");
    QTest::addColumn<bool>("hasSuspend");

    QTest::newRow("sleep-only") << true << false;
    QTest::newRow("suspend-only") << false << true;
    QTest::newRow("both") << true << true;
    QTest::newRow("neither") << false << false;
}

void TestX11GrabResilientShortcuts::sleepVariantsRegisterIndependently()
{
    QFETCH(bool, hasSleep);
    QFETCH(bool, hasSuspend);

    const std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr),
                                                                  &XCloseDisplay);
    if (!display)
        QSKIP("No X server is available");

    int firstKeycode = 0;
    int lastKeycode = 0;
    XDisplayKeycodes(display.get(), &firstKeycode, &lastKeycode);
    const int keycodeCount = lastKeycode - firstKeycode + 1;
    int keysymsPerKeycode = 0;
    const std::unique_ptr<KeySym, decltype(&XFree)> original(
            XGetKeyboardMapping(display.get(), firstKeycode, keycodeCount, &keysymsPerKeycode),
            &XFree);
    QVERIFY(original);
    QVERIFY(keycodeCount >= 2);
    QVERIFY(keysymsPerKeycode > 0);

    // CTest runs this executable under Xvfb. Restore its map even if an assertion fails.
    const auto restoreKeymap = qScopeGuard([&] {
        XChangeKeyboardMapping(display.get(), firstKeycode, keysymsPerKeycode, original.get(),
                               keycodeCount);
        XSync(display.get(), False);
    });
    std::vector<KeySym> keymap(original.get(),
                              original.get() + keycodeCount * keysymsPerKeycode);
    std::replace(keymap.begin(), keymap.end(), KeySym(XF86XK_Sleep), KeySym(NoSymbol));
    std::replace(keymap.begin(), keymap.end(), KeySym(XF86XK_Suspend), KeySym(NoSymbol));
    const KeyCode sleepKeycode = lastKeycode - 1;
    const KeyCode suspendKeycode = lastKeycode;
    const int sleepOffset = (sleepKeycode - firstKeycode) * keysymsPerKeycode;
    const int suspendOffset = (suspendKeycode - firstKeycode) * keysymsPerKeycode;
    std::fill(keymap.begin() + sleepOffset, keymap.end(), NoSymbol);
    keymap[sleepOffset] = hasSleep ? XF86XK_Sleep : NoSymbol;
    keymap[suspendOffset] = hasSuspend ? XF86XK_Suspend : NoSymbol;
    XChangeKeyboardMapping(display.get(), firstKeycode, keysymsPerKeycode, keymap.data(),
                           keycodeCount);
    XSync(display.get(), False);

    X11KeyHandler handler;
    QVERIFY(handler.isAvailable());
    QSignalSpy activationSpy(&handler, &X11KeyHandler::keyActivated);
    QVERIFY(activationSpy.isValid());
    const QString unrelatedId = QStringLiteral("test.unrelated");
    QVERIFY(handler.registerKey(shortcut(unrelatedId, QStringLiteral("Ctrl+Alt+B"))));

    // Read the shipped configs so combining the variants again reproduces the failure.
    const QString iniPath = QFINDTESTDATA("../configs/org.deepin.dde.keybinding.ini");
    QVERIFY(!iniPath.isEmpty());
    const QString configDir = QFileInfo(iniPath).absolutePath();
    const QString prefix = QStringLiteral("org.deepin.dde.keybinding.shortcut.");
    const QStringList names{QStringLiteral("sleep"), QStringLiteral("suspend")};
    for (const QString &name : names) {
        QFile file(configDir + QLatin1Char('/') + prefix + name
                   + QStringLiteral("/org.deepin.shortcut.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        const QJsonObject contents = document.object().value("contents").toObject();
        const auto value = [&contents](const QString &key) {
            return contents.value(key).toObject().value("value").toVariant();
        };
        KeyConfig config = shortcut(prefix + name, QString());
        config.hotkeys = value("hotkeys").toStringList();
        config.triggerType = value("triggerType").toInt();
        config.triggerValue = value("triggerValue").toStringList();
        QCOMPARE(config.triggerType, static_cast<int>(TriggerType::Command));
        QCOMPARE(config.triggerValue, (QStringList{QStringLiteral("/usr/bin/dde-shortcut-tool"),
                                                  QStringLiteral("power"),
                                                  QStringLiteral("suspend")}));
        QCOMPARE(handler.registerKey(config),
                 name == QLatin1String("sleep") ? hasSleep : hasSuspend);
    }

    // Observe backend activation only; no executor is connected, so the test never suspends.
    if (hasSleep) {
        XTestFakeKeyEvent(display.get(), sleepKeycode, True, CurrentTime);
        XTestFakeKeyEvent(display.get(), sleepKeycode, False, CurrentTime);
        XFlush(display.get());
        QTRY_COMPARE_WITH_TIMEOUT(activationSpy.size(), 1, 2000);
        QCOMPARE(activationSpy.takeFirst().constFirst().toString(),
                 prefix + QStringLiteral("sleep"));
    }
    if (hasSuspend) {
        XTestFakeKeyEvent(display.get(), suspendKeycode, True, CurrentTime);
        XTestFakeKeyEvent(display.get(), suspendKeycode, False, CurrentTime);
        XFlush(display.get());
        QTRY_COMPARE_WITH_TIMEOUT(activationSpy.size(), 1, 2000);
        QCOMPARE(activationSpy.takeFirst().constFirst().toString(),
                 prefix + QStringLiteral("suspend"));
    }
    sendTestChord(display.get());
    QTRY_COMPARE_WITH_TIMEOUT(activationSpy.size(), 1, 2000);
    QCOMPARE(activationSpy.constFirst().constFirst().toString(), unrelatedId);
}

QTEST_MAIN(TestX11GrabResilientShortcuts)

#include "tst_x11grabresilientshortcuts.moc"
