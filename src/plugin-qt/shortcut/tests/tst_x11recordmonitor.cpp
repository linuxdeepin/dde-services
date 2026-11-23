// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "backend/x11/modifierkeymonitor.h"

#include <QSignalSpy>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QTest>

#include <utility>

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

class TestX11RecordMonitor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void recordsKeysDuringActiveGrab();
    void standaloneModifierTransportIsAvailable();
    void keymapRefreshDoesNotUseRecordDataConnection();
    void captureBoundaryDiscardsStoppedEvents();
    void xi2FallbackHandlesStandaloneModifier();
    void pointerMappingChangesDoNotStallRecord();

private:
    Display *m_display = nullptr;
};

namespace {

void sendKey(Display *display, KeyCode keycode, bool pressed)
{
    XTestFakeKeyEvent(display, keycode, pressed ? True : False, CurrentTime);
}

}

void TestX11RecordMonitor::initTestCase()
{
    // Keep one client connected so Xvfb does not reset between test cases.
    m_display = XOpenDisplay(nullptr);
    if (!m_display)
        QSKIP("No X server is available");
}

void TestX11RecordMonitor::cleanupTestCase()
{
    if (m_display)
        XCloseDisplay(m_display);
}

void TestX11RecordMonitor::recordsKeysDuringActiveGrab()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("No X server is available");

    ModifierKeyMonitor monitor;
    if (!monitor.isAvailable()) {
        XCloseDisplay(display);
        QSKIP("X RECORD extension is unavailable");
    }
    if (!monitor.supportsGrabResilientEvents()) {
        XCloseDisplay(display);
        QSKIP("X RECORD extension is unavailable");
    }
    monitor.start();
    QTRY_VERIFY_WITH_TIMEOUT(monitor.isRunning(), 2000);

    QSignalSpy recordedSpy(&monitor, &ModifierKeyMonitor::keyEventRecorded);
    QSignalSpy modifierSpy(&monitor, &ModifierKeyMonitor::modifierKeyReleased);
    QVERIFY(recordedSpy.isValid());
    QVERIFY(modifierSpy.isValid());

    const Window root = DefaultRootWindow(display);
    QCOMPARE(XGrabKeyboard(display, root, False, GrabModeAsync, GrabModeAsync, CurrentTime),
             GrabSuccess);

    const KeyCode super = XKeysymToKeycode(display, XK_Super_L);
    const KeyCode control = XKeysymToKeycode(display, XK_Control_L);
    const KeyCode alt = XKeysymToKeycode(display, XK_Alt_L);
    const KeyCode a = XKeysymToKeycode(display, XK_A);
    QVERIFY(super != 0);
    QVERIFY(control != 0);
    QVERIFY(alt != 0);
    QVERIFY(a != 0);

    XTestFakeKeyEvent(display, super, True, CurrentTime);
    XTestFakeKeyEvent(display, super, False, CurrentTime);
    XTestFakeKeyEvent(display, control, True, CurrentTime);
    XTestFakeKeyEvent(display, alt, True, CurrentTime);
    XTestFakeKeyEvent(display, a, True, CurrentTime);
    XTestFakeKeyEvent(display, a, False, CurrentTime);
    XTestFakeKeyEvent(display, alt, False, CurrentTime);
    XTestFakeKeyEvent(display, control, False, CurrentTime);
    XFlush(display);

    QTRY_VERIFY2_WITH_TIMEOUT(recordedSpy.size() >= 8,
                             qPrintable(QStringLiteral("recorded events: %1").arg(recordedSpy.size())),
                             2000);
    QTRY_COMPARE_WITH_TIMEOUT(modifierSpy.size(), 1, 2000);

    XUngrabKeyboard(display, CurrentTime);
    XCloseDisplay(display);
}

void TestX11RecordMonitor::standaloneModifierTransportIsAvailable()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("No X server is available");

    ModifierKeyMonitor monitor;
    QVERIFY2(monitor.isAvailable(), "Neither X RECORD nor XInput2 is available");
    monitor.start();
    QTRY_VERIFY_WITH_TIMEOUT(monitor.isRunning(), 2000);

    QSignalSpy modifierSpy(&monitor, &ModifierKeyMonitor::modifierKeyReleased);
    QVERIFY(modifierSpy.isValid());
    const KeyCode super = XKeysymToKeycode(display, XK_Super_L);
    QVERIFY(super != 0);
    sendKey(display, super, true);
    sendKey(display, super, false);
    XSync(display, False);
    QTRY_COMPARE_WITH_TIMEOUT(modifierSpy.size(), 1, 2000);

    XCloseDisplay(display);
}

void TestX11RecordMonitor::xi2FallbackHandlesStandaloneModifier()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("No X server is available");

    ModifierKeyMonitor monitor(nullptr, false);
    QVERIFY2(monitor.isAvailable(), "XInput2 fallback is unavailable");
    QVERIFY(!monitor.supportsGrabResilientEvents());
    monitor.start();
    QVERIFY(monitor.isRunning());

    QSignalSpy modifierSpy(&monitor, &ModifierKeyMonitor::modifierKeyReleased);
    QVERIFY(modifierSpy.isValid());
    const KeyCode super = XKeysymToKeycode(display, XK_Super_L);
    QVERIFY(super != 0);
    sendKey(display, super, true);
    sendKey(display, super, false);
    XSync(display, False);
    QTRY_COMPARE_WITH_TIMEOUT(modifierSpy.size(), 1, 2000);

    XCloseDisplay(display);
}

void TestX11RecordMonitor::keymapRefreshDoesNotUseRecordDataConnection()
{
    ModifierKeyMonitor monitor;
    if (!monitor.isAvailable())
        QSKIP("X RECORD extension is unavailable");
    if (!monitor.supportsGrabResilientEvents())
        QSKIP("X RECORD extension is unavailable");
    monitor.start();
    QTRY_VERIFY_WITH_TIMEOUT(monitor.isRunning(), 2000);

    QElapsedTimer timer;
    timer.start();
    monitor.refreshKeyboardMapping();
    QVERIFY2(timer.elapsed() < 1000,
             "Keyboard mapping refresh blocked on the RECORD data connection");
}

void TestX11RecordMonitor::captureBoundaryDiscardsStoppedEvents()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("No X server is available");

    ModifierKeyMonitor monitor;
    if (!monitor.isAvailable()) {
        XCloseDisplay(display);
        QSKIP("X RECORD extension is unavailable");
    }
    if (!monitor.supportsGrabResilientEvents()) {
        XCloseDisplay(display);
        QSKIP("X RECORD extension is unavailable");
    }
    monitor.start();
    QTRY_VERIFY_WITH_TIMEOUT(monitor.isRunning(), 2000);

    QSignalSpy recordedSpy(&monitor, &ModifierKeyMonitor::keyEventRecorded);
    QVERIFY(recordedSpy.isValid());
    const KeyCode a = XKeysymToKeycode(display, XK_A);
    QVERIFY(a != 0);

    monitor.stop();
    sendKey(display, a, true);
    sendKey(display, a, false);
    XSync(display, False);

    // start() may be called before EndOfData arrives. It must wait for that
    // server-side boundary and start a new RECORD stream.
    monitor.start();
    QTRY_VERIFY_WITH_TIMEOUT(monitor.isRunning(), 2000);
    QTest::qWait(50);
    QCOMPARE(recordedSpy.size(), 0);

    sendKey(display, a, true);
    sendKey(display, a, false);
    XSync(display, False);
    QTRY_COMPARE_WITH_TIMEOUT(recordedSpy.size(), 2, 2000);

    XCloseDisplay(display);
}

void TestX11RecordMonitor::pointerMappingChangesDoNotStallRecord()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        QSKIP("No X server is available");
    const auto closeDisplay = qScopeGuard([display] { XCloseDisplay(display); });

    QList<unsigned char> originalMapping(256);
    const int buttonCount = XGetPointerMapping(display, originalMapping.data(), originalMapping.size());
    if (buttonCount < 3)
        QSKIP("At least three pointer buttons are required");
    originalMapping.resize(buttonCount);
    const auto restoreMapping = qScopeGuard([&] {
        XSetPointerMapping(display, originalMapping.data(), originalMapping.size());
        XSync(display, False);
    });

    ModifierKeyMonitor monitor;
    if (!monitor.supportsGrabResilientEvents())
        QSKIP("X RECORD extension is unavailable");
    monitor.start();
    QTRY_VERIFY_WITH_TIMEOUT(monitor.isRunning(), 2000);

    QSignalSpy recordedSpy(&monitor, &ModifierKeyMonitor::keyEventRecorded);
    QSignalSpy modifierSpy(&monitor, &ModifierKeyMonitor::modifierKeyReleased);
    QVERIFY(recordedSpy.isValid());
    QVERIFY(modifierSpy.isValid());
    const KeyCode super = XKeysymToKeycode(display, XK_Super_L);
    QVERIFY(super != 0);

    sendKey(display, super, true);
    sendKey(display, super, false);
    XSync(display, False);
    QTRY_COMPARE_WITH_TIMEOUT(recordedSpy.size(), 2, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(modifierSpy.size(), 1, 2000);

    QList<unsigned char> changedMapping = originalMapping;
    std::swap(changedMapping[0], changedMapping[2]);
    QCOMPARE(XSetPointerMapping(display, changedMapping.data(), changedMapping.size()), MappingSuccess);
    XSync(display, False);
    // Deliver MappingNotify before new RECORD replies arrive. Leaving this
    // ordinary event queued makes XPending() stop reading the data socket.
    QTest::qWait(50);

    sendKey(display, super, true);
    sendKey(display, super, false);
    XSync(display, False);
    QTRY_COMPARE_WITH_TIMEOUT(recordedSpy.size(), 4, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(modifierSpy.size(), 2, 2000);

    // EndOfData must also remain readable so a new RECORD stream can start.
    monitor.stop();
    monitor.start();
    QTRY_VERIFY_WITH_TIMEOUT(monitor.isRunning(), 2000);
    sendKey(display, super, true);
    sendKey(display, super, false);
    XSync(display, False);
    QTRY_COMPARE_WITH_TIMEOUT(recordedSpy.size(), 6, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(modifierSpy.size(), 3, 2000);
}

QTEST_MAIN(TestX11RecordMonitor)

#include "tst_x11recordmonitor.moc"
