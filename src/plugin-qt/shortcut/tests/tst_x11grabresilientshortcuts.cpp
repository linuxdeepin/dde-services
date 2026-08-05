// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "backend/x11/x11keyhandler.h"

#include <QSignalSpy>
#include <QTest>

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

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
    QVERIFY(handler.beginCapture(30000, QStringLiteral("test-owner")));
    QVERIFY(handler.endCapture(QStringLiteral("test-owner")));
    sendTestChord(display);
    QTRY_COMPARE_WITH_TIMEOUT(activationSpy.size(), 1, 2000);
    QCOMPARE(activationSpy.constFirst().constFirst().toString(), screenshotId);
    QTest::qWait(100);
    QCOMPARE(activationSpy.size(), 1);

    handler.unregisterKey(screenshotId);
    XCloseDisplay(display);
}

QTEST_MAIN(TestX11GrabResilientShortcuts)

#include "tst_x11grabresilientshortcuts.moc"
