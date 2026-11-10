// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "../provider/x11shortcutcapture.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QSocketNotifier>
#include <QTest>
#include <QTimer>

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <cstdlib>

class TestX11ShortcutCapture : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void longHoldCompletesOnPhysicalRelease();
    void splitRepeatPairDoesNotComplete();
    void cancelClearsPendingRelease();

private:
    static xcb_window_t eventWindow(X11ShortcutCapture &capture);
    static void sendEvent(X11ShortcutCapture &capture, xcb_window_t window,
                          uint8_t type, xcb_timestamp_t timestamp);
};

xcb_window_t TestX11ShortcutCapture::eventWindow(X11ShortcutCapture &capture)
{
    const xcb_window_t window = xcb_generate_id(capture.m_connection);
    xcb_create_window(capture.m_connection, XCB_COPY_FROM_PARENT, window,
                      capture.m_screen->root, 0, 0, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, capture.m_screen->root_visual, 0, nullptr);
    return window;
}

void TestX11ShortcutCapture::sendEvent(X11ShortcutCapture &capture, xcb_window_t window,
                                      uint8_t type, xcb_timestamp_t timestamp)
{
    xcb_key_press_event_t event{};
    event.response_type = type;
    event.detail = XKeysymToKeycode(capture.m_display, XK_A);
    event.time = timestamp;
    event.root = capture.m_screen->root;
    event.event = window;
    event.state = XCB_MOD_MASK_CONTROL;
    event.same_screen = 1;
    xcb_send_event(capture.m_connection, false, window, XCB_EVENT_MASK_NO_EVENT,
                   reinterpret_cast<const char *>(&event));
    free(xcb_get_input_focus_reply(capture.m_connection,
                                   xcb_get_input_focus(capture.m_connection), nullptr));
    capture.handleEvents();
}

void TestX11ShortcutCapture::longHoldCompletesOnPhysicalRelease()
{
    Display *display = XOpenDisplay(nullptr);
    QVERIFY(display);
    const auto closeDisplay = qScopeGuard([display] { XCloseDisplay(display); });
    const KeyCode key = XKeysymToKeycode(display, XK_F8);
    QVERIFY(key);
    XAutoRepeatOn(display);
    QVERIFY(XkbSetAutoRepeatRate(display, XkbUseCoreKbd, 100, 30));
    XSync(display, False);

    X11ShortcutCapture capture;
    QVERIFY(capture.isAvailable());
    QVERIFY(capture.start());
    QSignalSpy captured(&capture, &X11ShortcutCapture::captured);
    QSignalSpy failed(&capture, &X11ShortcutCapture::failed);
    const auto releaseKey = qScopeGuard([display, key] {
        XTestFakeKeyEvent(display, key, False, CurrentTime);
        XSync(display, False);
    });
    XTestFakeKeyEvent(display, key, True, CurrentTime);
    XFlush(display);
    QTest::qWait(500);
    QVERIFY(capture.m_pendingKeycode != 0);
    QVERIFY(capture.isActive());
    QCOMPARE(captured.size(), 0);
    QCOMPARE(failed.size(), 0);
    XTestFakeKeyEvent(display, key, False, CurrentTime);
    XFlush(display);
    QTRY_COMPARE_WITH_TIMEOUT(captured.size(), 1, 2000);
    QCOMPARE(captured.first().first().toString(), QStringLiteral("F8"));
    QVERIFY(!capture.isActive());
    QCOMPARE(failed.size(), 0);
}

void TestX11ShortcutCapture::splitRepeatPairDoesNotComplete()
{
    X11ShortcutCapture capture;
    QVERIFY(capture.isAvailable());
    capture.m_detectableAutoRepeat = false;
    QVERIFY(capture.start());
    capture.m_notifier->setEnabled(false);
    const xcb_window_t window = eventWindow(capture);
    const auto destroyWindow = qScopeGuard([&] {
        xcb_destroy_window(capture.m_connection, window);
    });
    QSignalSpy captured(&capture, &X11ShortcutCapture::captured);
    QSignalSpy failed(&capture, &X11ShortcutCapture::failed);

    sendEvent(capture, window, XCB_KEY_PRESS, 10);
    sendEvent(capture, window, XCB_KEY_RELEASE, 20);
    QVERIFY(capture.m_releasePending);
    QVERIFY(capture.isActive());
    QCOMPARE(captured.size(), 0);

    // Deliver the matching Press in a later socket read, before the release
    // round trip. The next queued event must still be processed normally.
    sendEvent(capture, window, XCB_KEY_PRESS, 20);
    QVERIFY(!capture.m_releasePending);
    capture.flushPendingRelease();
    QVERIFY(capture.isActive());
    QCOMPARE(captured.size(), 0);

    sendEvent(capture, window, XCB_KEY_RELEASE, 30);
    capture.flushPendingRelease();
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured.first().first().toString(), QStringLiteral("<Control>A"));
    QCOMPARE(failed.size(), 0);
    QVERIFY(!capture.isActive());
}

void TestX11ShortcutCapture::cancelClearsPendingRelease()
{
    X11ShortcutCapture capture;
    QVERIFY(capture.isAvailable());
    capture.m_detectableAutoRepeat = false;
    QVERIFY(capture.start());
    capture.m_notifier->setEnabled(false);
    const xcb_window_t window = eventWindow(capture);
    const auto destroyWindow = qScopeGuard([&] {
        xcb_destroy_window(capture.m_connection, window);
    });
    QSignalSpy captured(&capture, &X11ShortcutCapture::captured);
    sendEvent(capture, window, XCB_KEY_PRESS, 10);
    sendEvent(capture, window, XCB_KEY_RELEASE, 20);
    QVERIFY(capture.m_releasePending);
    capture.cancel();
    QVERIFY(!capture.m_releasePending);
    QVERIFY(!capture.m_releaseTimer->isActive());
    QVERIFY(capture.start());
    capture.flushPendingRelease();
    QTest::qWait(20);
    QVERIFY(capture.isActive());
    QCOMPARE(captured.size(), 0);
    capture.cancel();
}

QTEST_GUILESS_MAIN(TestX11ShortcutCapture)

#include "tst_x11shortcutcapture.moc"
