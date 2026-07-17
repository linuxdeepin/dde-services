// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "x11keyhandler.h"
#include "modifierkeymonitor.h"
#include "core/triggeractioncatalog.h"
#include "core/qkeysequenceconverter.h"
#include "core/physicalkeyalias.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include <xcb/xtest.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <algorithm>

// Need to define XK_MISCELLANY before including keysymdef.h to get Caps_Lock, Num_Lock etc.
#define XK_MISCELLANY
#include <X11/keysymdef.h>

namespace {

enum class LogicalModifier {
    Unknown,
    Shift,
    Control,
    Alt,
    Meta
};

LogicalModifier logicalModifier(xcb_keysym_t keysym)
{
    if (keysym == XK_Shift_L || keysym == XK_Shift_R)
        return LogicalModifier::Shift;
    if (keysym == XK_Control_L || keysym == XK_Control_R)
        return LogicalModifier::Control;
    if (keysym == XK_Alt_L || keysym == XK_Alt_R)
        return LogicalModifier::Alt;
    if (keysym == XK_Super_L || keysym == XK_Super_R
            || keysym == XK_Meta_L || keysym == XK_Meta_R) {
        return LogicalModifier::Meta;
    }
    return LogicalModifier::Unknown;
}

QList<xcb_keysym_t> modifierKeysyms(LogicalModifier modifier)
{
    switch (modifier) {
    case LogicalModifier::Shift:
        return {XK_Shift_L, XK_Shift_R};
    case LogicalModifier::Control:
        return {XK_Control_L, XK_Control_R};
    case LogicalModifier::Alt:
        return {XK_Alt_L, XK_Alt_R};
    case LogicalModifier::Meta:
        return {XK_Super_L, XK_Super_R, XK_Meta_L, XK_Meta_R};
    case LogicalModifier::Unknown:
        return {};
    }
    return {};
}

void expandModifierCombinations(QList<uint16_t> &combinations,
                                const QList<uint16_t> &alternatives)
{
    QList<uint16_t> expanded;
    for (uint16_t combination : std::as_const(combinations)) {
        for (uint16_t alternative : alternatives) {
            const uint16_t value = combination | alternative;
            if (!expanded.contains(value))
                expanded.append(value);
        }
    }
    combinations = expanded;
}

// Merge expanded X11 grab candidates; Required beats IfAvailable on duplicates.
void appendHotkeyCandidate(QList<PhysicalKeyAlias::X11Candidate> &candidates,
                           const PhysicalKeyAlias::X11Candidate &candidate)
{
    auto existing = std::find_if(candidates.begin(), candidates.end(),
                                 [&candidate](const PhysicalKeyAlias::X11Candidate &item) {
        return item.hotkey == candidate.hotkey;
    });
    if (existing == candidates.end()) {
        candidates.append(candidate);
    } else if (candidate.requirement == PhysicalKeyAlias::X11CandidateRequirement::Required) {
        existing->requirement = PhysicalKeyAlias::X11CandidateRequirement::Required;
    }
}

}

X11KeyHandler::X11KeyHandler(QObject *parent)
    : AbstractKeyHandler(parent)
    , m_releaseTimer(new QTimer(this))
{
    m_capture.timer = new QTimer(this);
    m_capture.ownerWatcher = new QDBusServiceWatcher(this);
    m_capture.timer->setSingleShot(true);
    connect(m_capture.timer, &QTimer::timeout, this, [this] { finishCapture(); });
    m_capture.ownerWatcher->setConnection(QDBusConnection::sessionBus());
    m_capture.ownerWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_capture.ownerWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this](const QString &service) {
                if (service == m_capture.owner)
                    finishCapture();
            });

    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        qCritical() << "X11KeyHandler: Failed to connect to X11 server";
        return;
    }
    XSetEventQueueOwner(m_display, XCBOwnsEventQueue);
    m_connection = XGetXCBConnection(m_display);
    if (!m_connection || xcb_connection_has_error(m_connection)) {
        qCritical() << "X11KeyHandler: Failed to obtain XCB connection";
        return;
    }

    const xcb_setup_t *setup = xcb_get_setup(m_connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    while (iter.rem) {
        m_rootWindows.append(iter.data->root);
        xcb_screen_next(&iter);
    }
    if (m_rootWindows.isEmpty()) {
        qCritical() << "X11KeyHandler: X11 server has no screens";
        return;
    }

    m_keySymbols = xcb_key_symbols_alloc(m_connection);
    if (!m_keySymbols) {
        qCritical() << "X11KeyHandler: Failed to allocate keyboard symbols";
        return;
    }
    refreshModifierMasks();
    enableDetectableAutoRepeat();

    m_releaseTimer->setSingleShot(true);
    m_releaseTimer->setInterval(0);
    connect(m_releaseTimer, &QTimer::timeout, this, &X11KeyHandler::flushPendingReleases);
    
    // Create modifier key monitor
    m_modifierMonitor = new ModifierKeyMonitor(this);
    connect(m_modifierMonitor, &ModifierKeyMonitor::modifierKeyReleased,
            this, &X11KeyHandler::onModifierKeyReleased);
    m_modifierMonitor->start();

    // Setup XCB event monitoring
    int fd = xcb_get_file_descriptor(m_connection);
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    m_notifier->setEnabled(true);
    connect(m_notifier, &QSocketNotifier::activated, this, &X11KeyHandler::handleXcbEvents);
}

X11KeyHandler::~X11KeyHandler()
{
    finishCapture(false);
    if (m_notifier)
        m_notifier->setEnabled(false);
    if (m_keySymbols) xcb_key_symbols_free(m_keySymbols);
    if (m_display) XCloseDisplay(m_display);
}

bool X11KeyHandler::isAvailable() const
{
    return m_connection && !m_rootWindows.isEmpty() && m_keySymbols
            && !xcb_connection_has_error(m_connection);
}

bool X11KeyHandler::beginCapture(uint timeoutMs, const QString &owner)
{
    if (m_capture.active) {
        if (!owner.isEmpty() && owner != m_capture.owner)
            return false;
        m_capture.timer->start(qBound(1000u, timeoutMs, 60000u));
        return true;
    }
    if (!isAvailable())
        return false;

    // Capture shares this connection with normal shortcut handling. Drain
    // events queued before the grab so an old release cannot finish the new
    // capture immediately.
    handleXcbEvents();
    flushPendingReleases();

    const xcb_window_t rootWindow = m_rootWindows.constFirst();
    const xcb_grab_keyboard_cookie_t keyboardCookie = xcb_grab_keyboard(
            m_connection, false, rootWindow, XCB_CURRENT_TIME,
            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    xcb_grab_keyboard_reply_t *keyboardReply =
            xcb_grab_keyboard_reply(m_connection, keyboardCookie, nullptr);
    if (!keyboardReply || keyboardReply->status != XCB_GRAB_STATUS_SUCCESS) {
        qWarning() << "X11KeyHandler: failed to grab keyboard for shortcut capture";
        free(keyboardReply);
        return false;
    }
    free(keyboardReply);

    const xcb_grab_pointer_cookie_t pointerCookie = xcb_grab_pointer(
            m_connection, false, rootWindow,
            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
            XCB_WINDOW_NONE, XCB_CURSOR_NONE, XCB_CURRENT_TIME);
    xcb_grab_pointer_reply_t *pointerReply =
            xcb_grab_pointer_reply(m_connection, pointerCookie, nullptr);
    if (!pointerReply || pointerReply->status != XCB_GRAB_STATUS_SUCCESS) {
        qWarning() << "X11KeyHandler: failed to grab pointer for shortcut capture";
        free(pointerReply);
        xcb_ungrab_keyboard(m_connection, XCB_CURRENT_TIME);
        xcb_flush(m_connection);
        return false;
    }
    free(pointerReply);

    m_pendingReleases.clear();
    m_pressedBindings.clear();
    m_capture.keystroke.clear();
    m_capture.owner = owner;
    m_capture.active = true;
    if (!m_capture.owner.isEmpty())
        m_capture.ownerWatcher->addWatchedService(m_capture.owner);
    m_capture.timer->start(qBound(1000u, timeoutMs, 60000u));
    if (m_modifierMonitor)
        m_modifierMonitor->stop();
    xcb_flush(m_connection);
    emit captureStarted();
    return true;
}

bool X11KeyHandler::endCapture(const QString &owner)
{
    if (!m_capture.active)
        return false;
    if (!owner.isEmpty() && !m_capture.owner.isEmpty() && owner != m_capture.owner)
        return false;

    finishCapture();
    return true;
}

void X11KeyHandler::finishCapture(bool notify)
{
    if (!m_capture.active)
        return;

    xcb_ungrab_keyboard(m_connection, XCB_CURRENT_TIME);
    xcb_ungrab_pointer(m_connection, XCB_CURRENT_TIME);
    xcb_flush(m_connection);

    m_capture.timer->stop();
    if (!m_capture.owner.isEmpty())
        m_capture.ownerWatcher->removeWatchedService(m_capture.owner);
    m_capture.owner.clear();
    m_capture.keystroke.clear();
    m_capture.active = false;
    if (m_modifierMonitor)
        m_modifierMonitor->start();

    if (!notify) {
        m_keymapReloadAfterCapture = false;
        return;
    }

    emit captureFinished();

    if (m_keymapReloadAfterCapture) {
        m_keymapReloadAfterCapture = false;
        emit keymapAboutToChange();
        scheduleKeymapChanged();
    }
}

bool X11KeyHandler::registerKey(const KeyConfig &config)
{
    if (!isAvailable()) {
        qCritical() << "X11KeyHandler: XCB connection not available, cannot register key:" << config.getId();
        return false;
    }

    TriggerActionId actionId = TriggerActionId::Invalid;
    if (config.triggerType == static_cast<int>(TriggerType::Action)
            && !config.triggerValue.isEmpty()) {
        actionId = TriggerActionCatalog::resolve(config.triggerValue.first());
    }
    const QString delegatedWmId = TriggerActionCatalog::x11WmShortcutId(actionId);
    if (!delegatedWmId.isEmpty()) {
        if (!setWmShortcut(delegatedWmId, config.hotkeys))
            return false;
        m_wmShortcutIds.insert(config.getId(), delegatedWmId);
        qInfo() << "X11KeyHandler: delegated shortcut to KWin:"
                << config.getId() << delegatedWmId << config.hotkeys;
        return true;
    }

    // If already registered, unregister first
    if (m_shortcutKeys.contains(config.getId())) {
        unregisterKey(config.getId());
    }

    QList<uint32_t> grabbed;
    bool allSuccess = true;

    QList<PhysicalKeyAlias::X11Candidate> candidates;
    for (const QString &hotkey : config.hotkeys) {
        // Convert Qt Standard Name (e.g., "Meta+Volume Mute") to XKB string (e.g., "<Super>XF86AudioMute")
        const QString xkbHotkey = QKeySequenceConverter::qKeySequenceToXkb(hotkey);
        for (const PhysicalKeyAlias::X11Candidate &candidate : PhysicalKeyAlias::expandX11Candidates(xkbHotkey))
            appendHotkeyCandidate(candidates, candidate);
    }

    for (const PhysicalKeyAlias::X11Candidate &candidate : std::as_const(candidates)) {
        const QString &xkbHotkey = candidate.hotkey;
        const HotkeyResolutionResult resolved = resolveHotkey(xkbHotkey);
        const HotkeyResolution resolution = resolved.resolution;
        if (resolution != HotkeyResolution::Resolved) {
            const bool unavailableAlias =
                    resolution == HotkeyResolution::UnavailableInKeymap
                    && candidate.requirement == PhysicalKeyAlias::X11CandidateRequirement::IfAvailable;
            if (unavailableAlias) {
                qInfo() << "X11KeyHandler: Physical alias is unavailable in the current keymap:"
                        << xkbHotkey;
                continue;
            }

            qWarning() << "X11KeyHandler: Failed to resolve hotkey:" << xkbHotkey
                       << (resolution == HotkeyResolution::InvalidSpecification
                                   ? "invalid specification"
                                   : "unavailable in current keymap");
            allSuccess = false;
            break;
        }

        const ResolvedHotkey &hotkey = resolved.hotkey;
        const bool isStandaloneModifier = hotkey.modifierCombinations.size() == 1
                && isStandaloneModifierKey(hotkey.keysym, hotkey.modifierCombinations.constFirst());
        if (isStandaloneModifier
                && (!m_modifierMonitor || !m_modifierMonitor->isAvailable())) {
            qWarning() << "X11KeyHandler: Standalone modifier monitor is unavailable:" << xkbHotkey;
            allSuccess = false;
            break;
        }

        bool candidateFailed = false;
        for (uint16_t modifiers : hotkey.modifierCombinations) {
            for (xcb_keycode_t keycode : hotkey.keycodes) {
                const uint32_t key = keycode | (uint32_t(modifiers) << 16);
                if (grabbed.contains(key))
                    continue;

                if (!isStandaloneModifier && !grabKey(keycode, modifiers)) {
                    candidateFailed = true;
                    qWarning() << "X11KeyHandler: Failed to grab hotkey:" << xkbHotkey
                               << "keycode:" << keycode << "modifiers:" << Qt::hex << modifiers;
                    break;
                }

                m_grabbedKeys.insert(key, config.getId());
                grabbed.append(key);
                if (isStandaloneModifier)
                    m_standaloneModifierKeys.insert(key);
            }
            if (candidateFailed)
                break;
        }
        if (candidateFailed) {
            allSuccess = false;
            break;
        }
    }

    if (!allSuccess && !grabbed.isEmpty()) {
        qWarning() << "X11KeyHandler: Partial registration failure for" << config.getId()
                   << "- rolling back" << grabbed.size() << "successful grabs";
        for (uint32_t key : std::as_const(grabbed)) {
            const xcb_keycode_t keycode = key & 0xFFFF;
            const uint16_t mods = key >> 16;
            if (!m_standaloneModifierKeys.remove(key))
                ungrabKey(keycode, mods);
            m_grabbedKeys.remove(key);
        }
        return false;
    }

    if (!grabbed.isEmpty()) {
        m_shortcutKeys.insert(config.getId(), grabbed);
        m_shortcutFlags.insert(config.getId(), config.keyEventFlags);
    }

    return allSuccess;
}

bool X11KeyHandler::unregisterKey(const QString &shortcutId)
{
    const auto wmIt = m_wmShortcutIds.find(shortcutId);
    if (wmIt != m_wmShortcutIds.end()) {
        const QString delegatedWmId = wmIt.value();
        if (!setWmShortcut(delegatedWmId, {}))
            return false;
        m_wmShortcutIds.erase(wmIt);
        return true;
    }

    if (!m_shortcutKeys.contains(shortcutId)) return false;

    QList<uint32_t> keys = m_shortcutKeys.take(shortcutId);
    m_shortcutFlags.remove(shortcutId);

    for (uint32_t key : keys) {
        const xcb_keycode_t keycode = key & 0xFFFF;
        const uint16_t mods = key >> 16;
        if (!m_standaloneModifierKeys.remove(key))
            ungrabKey(keycode, mods);
        m_grabbedKeys.remove(key);
    }
    clearPressedState(shortcutId);

    return true;
}

bool X11KeyHandler::setWmShortcut(const QString &wmShortcutId, const QStringList &hotkeys)
{
    QJsonArray accels;
    for (const QString &hotkey : hotkeys)
        accels.append(QKeySequenceConverter::qKeySequenceToXkb(hotkey));

    const QString data = QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("Id"), wmShortcutId},
        {QStringLiteral("Accels"), accels},
    }).toJson(QJsonDocument::Compact));

    const auto callSetAccel = [&data](const QVariantList &arguments) {
        QDBusMessage message = QDBusMessage::createMethodCall(
                QStringLiteral("com.deepin.wm"), QStringLiteral("/com/deepin/wm"),
                QStringLiteral("com.deepin.wm"), QStringLiteral("SetAccel"));
        message.setArguments(arguments);
        return QDBusReply<bool>(QDBusConnection::sessionBus().call(message));
    };
    const auto hasSignatureMismatch = [](const QDBusReply<bool> &reply) {
        return !reply.isValid()
                && (reply.error().type() == QDBusError::UnknownMethod
                    || reply.error().type() == QDBusError::InvalidArgs);
    };

    // X11 window managers expose SetAccel(string) while newer implementations
    // add a uint context argument. Support both protocol versions at this
    // compatibility boundary without making the shortcut catalog platform-specific.
    const bool preferContext = m_wmSetAccelSignature == WmSetAccelSignature::ContextAndData;
    QDBusReply<bool> reply = preferContext
            ? callSetAccel({QVariant::fromValue(uint(0)), data})
            : callSetAccel({data});
    if (hasSignatureMismatch(reply)) {
        reply = preferContext
                ? callSetAccel({data})
                : callSetAccel({QVariant::fromValue(uint(0)), data});
        if (!hasSignatureMismatch(reply)) {
            m_wmSetAccelSignature = preferContext
                    ? WmSetAccelSignature::DataOnly
                    : WmSetAccelSignature::ContextAndData;
        }
    } else if (m_wmSetAccelSignature == WmSetAccelSignature::Unknown) {
        m_wmSetAccelSignature = WmSetAccelSignature::DataOnly;
    }
    if (!reply.isValid() || !reply.value()) {
        qWarning() << "X11KeyHandler: failed to set KWin shortcut:"
                   << wmShortcutId << hotkeys << reply.error().message();
        return false;
    }
    return true;
}

bool X11KeyHandler::grabKey(xcb_keycode_t keycode, uint16_t modifiers)
{
    bool hasError = false;
    for (xcb_window_t rootWindow : std::as_const(m_rootWindows)) {
        for (uint16_t ignored : ignoredModifierCombinations()) {
            xcb_void_cookie_t cookie = xcb_grab_key_checked(
                m_connection,
                1,
                rootWindow,
                modifiers | ignored,
                keycode,
                XCB_GRAB_MODE_ASYNC,
                XCB_GRAB_MODE_ASYNC
            );

            xcb_generic_error_t *error = xcb_request_check(m_connection, cookie);
            if (error) {
                qWarning() << "Failed to grab key" << keycode << "with modifiers" << Qt::hex << modifiers
                          << "on root" << rootWindow << "Error code:" << error->error_code;
                free(error);
                hasError = true;
            }
        }
    }

    if (hasError) {
        ungrabKey(keycode, modifiers);
    }

    xcb_flush(m_connection);
    return !hasError;
}

bool X11KeyHandler::ungrabKey(xcb_keycode_t keycode, uint16_t modifiers)
{
    if (!isAvailable())
        return false;

    for (xcb_window_t rootWindow : std::as_const(m_rootWindows)) {
        for (uint16_t ignored : ignoredModifierCombinations())
            xcb_ungrab_key(m_connection, keycode, rootWindow, modifiers | ignored);
    }
    xcb_flush(m_connection);
    return true;
}

extern "C" unsigned long x11StringToKeysym(const char* str);

X11KeyHandler::HotkeyResolutionResult X11KeyHandler::resolveHotkey(const QString &hotkey)
{
    HotkeyResolutionResult result;
    result.hotkey.modifierCombinations = {0};
    QString keyStr = hotkey;

    // Parse modifiers
    if (keyStr.contains("<Shift>")) {
        for (uint16_t &modifiers : result.hotkey.modifierCombinations)
            modifiers |= XCB_MOD_MASK_SHIFT;
        keyStr.remove("<Shift>");
    }
    if (keyStr.contains("<Control>")) {
        for (uint16_t &modifiers : result.hotkey.modifierCombinations)
            modifiers |= XCB_MOD_MASK_CONTROL;
        keyStr.remove("<Control>");
    }
    if (keyStr.contains("<Alt>")) {
        expandModifierCombinations(result.hotkey.modifierCombinations, m_altMasks);
        keyStr.remove("<Alt>");
    }
    if (keyStr.contains("<Super>") || keyStr.contains("<Meta>")) {
        expandModifierCombinations(result.hotkey.modifierCombinations, m_superMasks);
        keyStr.remove("<Super>");
        keyStr.remove("<Meta>");
    }

    if (keyStr.isEmpty())
        return result;
    if (result.hotkey.modifierCombinations.isEmpty()) {
        result.resolution = HotkeyResolution::UnavailableInKeymap;
        return result;
    }

    // Get keysym and keycode
    result.hotkey.keysym = x11StringToKeysym(keyStr.toUtf8().constData());
    if (result.hotkey.keysym == XCB_NO_SYMBOL)
        return result;

    const bool isStandaloneModifier = result.hotkey.modifierCombinations.size() == 1
            && isStandaloneModifierKey(result.hotkey.keysym, result.hotkey.modifierCombinations.constFirst());
    const QList<xcb_keysym_t> keysyms = isStandaloneModifier
            ? modifierKeysyms(logicalModifier(result.hotkey.keysym))
            : QList<xcb_keysym_t>{result.hotkey.keysym};
    const bool basicLevelOnly = keyStr.startsWith(QLatin1String("XF86"));

    for (xcb_keysym_t candidateKeysym : keysyms) {
        xcb_keycode_t *candidateKeycodes = xcb_key_symbols_get_keycode(m_keySymbols, candidateKeysym);
        if (!candidateKeycodes)
            continue;

        for (xcb_keycode_t *candidate = candidateKeycodes;
             *candidate != XCB_NO_SYMBOL; ++candidate) {
            const xcb_keysym_t baseKeysym = xcb_key_symbols_get_keysym(m_keySymbols, *candidate, 0);
            if (isStandaloneModifier && logicalModifier(baseKeysym) != logicalModifier(result.hotkey.keysym)) {
                continue;
            }
            if (basicLevelOnly && baseKeysym != result.hotkey.keysym)
                continue;
            if (!result.hotkey.keycodes.contains(*candidate))
                result.hotkey.keycodes.append(*candidate);
        }
        free(candidateKeycodes);
    }

    if (result.hotkey.keycodes.isEmpty()) {
        result.resolution = HotkeyResolution::UnavailableInKeymap;
        return result;
    }
    result.resolution = HotkeyResolution::Resolved;
    return result;
}

void X11KeyHandler::handleXcbEvents()
{
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(m_connection))) {
        uint8_t responseType = event->response_type & ~0x80;

        if (m_capture.active && responseType == XCB_KEY_PRESS) {
            const CapturedKey captured = captureKey(
                    reinterpret_cast<xcb_key_press_event_t *>(event));
            m_capture.keystroke = isCapturedKeyValid(captured)
                    ? captured.keystroke : QString();
            emit captureKeyEvent(true, captured.keystroke);
        } else if (m_capture.active && responseType == XCB_KEY_RELEASE) {
            emit captureKeyEvent(false, m_capture.keystroke);
            finishCapture();
        } else if (m_capture.active && responseType == XCB_BUTTON_PRESS) {
            m_capture.keystroke.clear();
            emit captureKeyEvent(true, QString());
        } else if (m_capture.active && responseType == XCB_BUTTON_RELEASE) {
            emit captureKeyEvent(false, QString());
            finishCapture();
        } else if (responseType == XCB_KEY_PRESS) {
            handleKeyPress(reinterpret_cast<xcb_key_press_event_t *>(event));
        } else if (responseType == XCB_KEY_RELEASE) {
            handleKeyRelease(reinterpret_cast<xcb_key_release_event_t *>(event));
        } else if (responseType == XCB_MAPPING_NOTIFY) {
            flushPendingReleases();
            auto *mappingEvent = reinterpret_cast<xcb_mapping_notify_event_t *>(event);
            if (mappingEvent->request == XCB_MAPPING_KEYBOARD
                    || mappingEvent->request == XCB_MAPPING_MODIFIER) {
                if (m_capture.active)
                    m_keymapReloadAfterCapture = true;
                else
                    emit keymapAboutToChange();
                if (mappingEvent->request == XCB_MAPPING_KEYBOARD)
                    xcb_refresh_keyboard_mapping(m_keySymbols, mappingEvent);
                refreshModifierMasks();
                if (!m_capture.active)
                    scheduleKeymapChanged();
            }
        } else {
            flushPendingReleases();
        }

        free(event);
    }
}

X11KeyHandler::CapturedKey X11KeyHandler::captureKey(const xcb_key_press_event_t *event) const
{
    CapturedKey captured;
    if (!event || !m_keySymbols)
        return captured;

    unsigned int group = 0;
    XkbStateRec keyboardState{};
    if (m_display && XkbGetState(m_display, XkbUseCoreKbd, &keyboardState) == Success)
        group = keyboardState.group;

    xcb_keysym_t keysym = xcb_keysym_t(XkbKeycodeToKeysym(
            m_display, KeyCode(event->detail), int(group), 0));
    if (keysym == XCB_NO_SYMBOL)
        return captured;

    KeySym lower = NoSymbol;
    KeySym upper = NoSymbol;
    XConvertCase(KeySym(keysym), &lower, &upper);
    keysym = upper == NoSymbol ? keysym : xcb_keysym_t(upper);

    const char *keyName = XKeysymToString(KeySym(keysym));
    if (!keyName)
        return captured;

    QString name = QString::fromLatin1(keyName);
    if (name == QLatin1String("KP_Prior"))
        name = QStringLiteral("KP_Page_Up");
    else if (name == QLatin1String("KP_Next"))
        name = QStringLiteral("KP_Page_Down");
    else if (name == QLatin1String("Prior"))
        name = QStringLiteral("Page_Up");
    else if (name == QLatin1String("Next"))
        name = QStringLiteral("Page_Down");
    else if (name == QLatin1String("ISO_Left_Tab"))
        name = QStringLiteral("Tab");

    uint16_t modifiers = 0;
    if (event->state & XCB_MOD_MASK_SHIFT)
        modifiers |= XCB_MOD_MASK_SHIFT;
    if (event->state & XCB_MOD_MASK_CONTROL)
        modifiers |= XCB_MOD_MASK_CONTROL;
    if (hasAnyMask(event->state, m_altMasks))
        modifiers |= XCB_MOD_MASK_1;
    if (hasAnyMask(event->state, m_superMasks))
        modifiers |= XCB_MOD_MASK_4;

    if ((keysym == XK_Super_L || keysym == XK_Super_R)
            && modifiers != 0
            && (modifiers & ~XCB_MOD_MASK_4) == 0) {
        modifiers = 0;
    }

    QString keystroke;
    if (modifiers & XCB_MOD_MASK_SHIFT)
        keystroke += QStringLiteral("<Shift>");
    if (modifiers & XCB_MOD_MASK_CONTROL)
        keystroke += QStringLiteral("<Control>");
    if (modifiers & XCB_MOD_MASK_1)
        keystroke += QStringLiteral("<Alt>");
    if (modifiers & XCB_MOD_MASK_4)
        keystroke += QStringLiteral("<Super>");
    keystroke += name;

    captured.keystroke = keystroke;
    captured.keyName = name;
    captured.keysym = keysym;
    captured.modifiers = modifiers;
    return captured;
}

bool X11KeyHandler::isCapturedKeyValid(const CapturedKey &key) const
{
    if (key.keysym == XCB_NO_SYMBOL || key.keyName.isEmpty())
        return false;

    if (key.modifiers == 0) {
        if (IsFunctionKey(KeySym(key.keysym)) || IsMiscFunctionKey(KeySym(key.keysym)))
            return true;
        if (key.keysym == XK_BackSpace || key.keysym == XK_Delete
                || key.keysym == XK_Super_L || key.keysym == XK_Super_R
                || key.keysym == XK_Pause) {
            return true;
        }
        return key.keyName.startsWith(QLatin1String("XF86"));
    }

    if (IsModifierKey(KeySym(key.keysym)))
        return false;

    if (key.modifiers == XCB_MOD_MASK_SHIFT) {
        if (IsFunctionKey(KeySym(key.keysym)) || IsMiscFunctionKey(KeySym(key.keysym))
                || IsCursorKey(KeySym(key.keysym))) {
            return true;
        }
        if (key.keysym == XK_BackSpace || key.keysym == XK_space
                || key.keysym == XK_Delete || key.keysym == XK_Sys_Req
                || key.keysym == XK_Escape || key.keysym == XK_Tab) {
            return true;
        }
        return key.keyName.startsWith(QLatin1String("XF86"));
    }

    return true;
}

bool X11KeyHandler::hasAnyMask(uint16_t state, const QList<uint16_t> &masks) const
{
    return std::any_of(masks.cbegin(), masks.cend(), [state](uint16_t mask) {
        return mask && (state & mask);
    });
}

uint16_t X11KeyHandler::getConcernedMods(uint16_t state)
{
    uint16_t concernedMasks = XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL;
    for (uint16_t mask : std::as_const(m_altMasks))
        concernedMasks |= mask;
    for (uint16_t mask : std::as_const(m_superMasks))
        concernedMasks |= mask;
    return state & concernedMasks;
}

QList<uint16_t> X11KeyHandler::ignoredModifierCombinations() const
{
    QList<uint16_t> combinations{0};
    const uint16_t lockMasks[] = {m_capsLockMask, m_numLockMask, m_scrollLockMask};
    for (uint16_t mask : lockMasks) {
        if (!mask)
            continue;
        const QList<uint16_t> existing = combinations;
        for (uint16_t value : existing) {
            const uint16_t combined = value | mask;
            if (!combinations.contains(combined))
                combinations.append(combined);
        }
    }
    return combinations;
}

void X11KeyHandler::refreshModifierMasks()
{
    if (!isAvailable() || !m_keySymbols)
        return;

    m_altMasks.clear();
    m_superMasks.clear();
    m_capsLockMask = 0;
    m_numLockMask = 0;
    m_scrollLockMask = 0;

    const xcb_get_modifier_mapping_cookie_t cookie = xcb_get_modifier_mapping(m_connection);
    xcb_get_modifier_mapping_reply_t *reply = xcb_get_modifier_mapping_reply(m_connection, cookie, nullptr);
    if (!reply) {
        m_altMasks = {XCB_MOD_MASK_1};
        m_superMasks = {XCB_MOD_MASK_4};
        m_capsLockMask = XCB_MOD_MASK_LOCK;
        m_numLockMask = XCB_MOD_MASK_2;
        return;
    }

    const xcb_keycode_t *keycodes = xcb_get_modifier_mapping_keycodes(reply);
    for (int modifierIndex = 0; modifierIndex < 8; ++modifierIndex) {
        const uint16_t mask = uint16_t(1u << modifierIndex);
        for (int keyIndex = 0; keyIndex < reply->keycodes_per_modifier; ++keyIndex) {
            const xcb_keycode_t keycode = keycodes[modifierIndex * reply->keycodes_per_modifier + keyIndex];
            if (keycode == XCB_NO_SYMBOL)
                continue;
            const xcb_keysym_t keysym = xcb_key_symbols_get_keysym(m_keySymbols, keycode, 0);
            if (keysym == XK_Alt_L || keysym == XK_Alt_R) {
                if (!m_altMasks.contains(mask))
                    m_altMasks.append(mask);
            } else if (keysym == XK_Super_L || keysym == XK_Super_R
                       || keysym == XK_Meta_L || keysym == XK_Meta_R) {
                if (!m_superMasks.contains(mask))
                    m_superMasks.append(mask);
            } else if (keysym == XK_Caps_Lock) {
                m_capsLockMask |= mask;
            } else if (keysym == XK_Num_Lock) {
                m_numLockMask |= mask;
            } else if (keysym == XK_Scroll_Lock) {
                m_scrollLockMask |= mask;
            }
        }
    }
    free(reply);

    if (m_altMasks.isEmpty())
        m_altMasks = {XCB_MOD_MASK_1};
    if (m_superMasks.isEmpty())
        m_superMasks = {XCB_MOD_MASK_4};
    if (!m_capsLockMask)
        m_capsLockMask = XCB_MOD_MASK_LOCK;
}

void X11KeyHandler::enableDetectableAutoRepeat()
{
    Bool supported = False;
    m_detectableAutoRepeat = m_display
            && XkbSetDetectableAutoRepeat(m_display, True, &supported)
            && supported;
    if (!m_detectableAutoRepeat)
        qWarning() << "X11KeyHandler: falling back to Release/Press autorepeat detection";
}

void X11KeyHandler::handleKeyPress(const xcb_key_press_event_t *event)
{
    const auto pending = m_pendingReleases.constFind(event->detail);
    if (pending != m_pendingReleases.constEnd()
            && pending.value() == event->time
            && m_pressedBindings.contains(event->detail)) {
        m_pendingReleases.erase(pending);
        activate(m_pressedBindings.value(event->detail), KeyEventFlag::Repeat);
        return;
    }

    flushPendingReleases();
    m_modifierMonitor->notifyNonModifierKeyPressed();

    if (m_pressedBindings.contains(event->detail)) {
        activate(m_pressedBindings.value(event->detail), KeyEventFlag::Repeat);
        return;
    }

    const uint16_t modifiers = getConcernedMods(event->state);
    const uint32_t bindingKey = event->detail | (uint32_t(modifiers) << 16);
    const auto bindingIt = m_grabbedKeys.constFind(bindingKey);
    if (bindingIt == m_grabbedKeys.constEnd())
        return;

    const QString shortcutId = bindingIt.value();
    m_pressedBindings.insert(event->detail, shortcutId);
    activate(shortcutId, KeyEventFlag::Press);
}

void X11KeyHandler::handleKeyRelease(const xcb_key_release_event_t *event)
{
    if (!m_pressedBindings.contains(event->detail))
        return;

    if (m_detectableAutoRepeat) {
        const QString shortcutId = m_pressedBindings.take(event->detail);
        activate(shortcutId, KeyEventFlag::Release);
        return;
    }

    if (m_pendingReleases.contains(event->detail))
        flushPendingReleases();
    m_pendingReleases.insert(event->detail, event->time);
    m_releaseTimer->start();
}

void X11KeyHandler::flushPendingReleases()
{
    if (m_pendingReleases.isEmpty())
        return;

    const QList<xcb_keycode_t> keycodes = m_pendingReleases.keys();
    m_pendingReleases.clear();
    for (xcb_keycode_t keycode : keycodes) {
        const auto bindingIt = m_pressedBindings.find(keycode);
        if (bindingIt == m_pressedBindings.end())
            continue;
        const QString shortcutId = bindingIt.value();
        m_pressedBindings.erase(bindingIt);
        activate(shortcutId, KeyEventFlag::Release);
    }
}

void X11KeyHandler::activate(const QString &shortcutId, int eventFlag)
{
    if (m_shortcutFlags.value(shortcutId, KeyEventFlag::Release) & eventFlag)
        emit keyActivated(shortcutId);
}

void X11KeyHandler::clearPressedState(const QString &shortcutId)
{
    for (auto it = m_pressedBindings.begin(); it != m_pressedBindings.end();) {
        if (it.value() == shortcutId) {
            m_pendingReleases.remove(it.key());
            it = m_pressedBindings.erase(it);
        } else {
            ++it;
        }
    }
}

void X11KeyHandler::notifyKeymapChanged()
{
    m_keymapChangePending = false;
    if (m_capture.active) {
        m_keymapReloadAfterCapture = true;
        return;
    }
    emit keymapChanged();
}

void X11KeyHandler::scheduleKeymapChanged()
{
    if (m_keymapChangePending)
        return;
    m_keymapChangePending = true;
    QTimer::singleShot(0, this, &X11KeyHandler::notifyKeymapChanged);
}

bool X11KeyHandler::getCapsLockState() const
{
    if (!isAvailable()) return false;
    
    // Query pointer to get modifier state (CapsLock is XCB_MOD_MASK_LOCK)
    xcb_query_pointer_cookie_t cookie = xcb_query_pointer(m_connection, m_rootWindows.constFirst());
    xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(m_connection, cookie, nullptr);
    
    if (!reply) return false;
    
    bool state = (reply->mask & m_capsLockMask) != 0;
    free(reply);
    
    return state;
}

bool X11KeyHandler::getNumLockState() const
{
    if (!isAvailable()) return false;
    
    // Query pointer to get modifier state
    xcb_query_pointer_cookie_t cookie = xcb_query_pointer(m_connection, m_rootWindows.constFirst());
    xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(m_connection, cookie, nullptr);
    
    if (!reply) return false;
    
    bool state = (reply->mask & m_numLockMask) != 0;
    free(reply);
    
    return state;
}

void X11KeyHandler::setCapsLockState(bool on)
{
    if (!isAvailable()) return;
    
    bool currentState = getCapsLockState();
    if (currentState == on) return;
    
    // Get CapsLock keycode
    xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(m_keySymbols, XK_Caps_Lock);
    if (!keycodes || keycodes[0] == XCB_NO_SYMBOL) {
        if (keycodes) free(keycodes);
        qWarning() << "Failed to get CapsLock keycode";
        return;
    }
    
    xcb_keycode_t keycode = keycodes[0];
    free(keycodes);
    
    // Simulate key press and release
    const xcb_window_t rootWindow = m_rootWindows.constFirst();
    xcb_test_fake_input(m_connection, XCB_KEY_PRESS, keycode, XCB_CURRENT_TIME, rootWindow, 0, 0, 0);
    xcb_test_fake_input(m_connection, XCB_KEY_RELEASE, keycode, XCB_CURRENT_TIME, rootWindow, 0, 0, 0);
    xcb_flush(m_connection);
}

void X11KeyHandler::setNumLockState(bool on)
{
    if (!isAvailable()) return;
    
    bool currentState = getNumLockState();
    if (currentState == on) return;
    
    // Get NumLock keycode
    xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(m_keySymbols, XK_Num_Lock);
    if (!keycodes || keycodes[0] == XCB_NO_SYMBOL) {
        if (keycodes) free(keycodes);
        qWarning() << "Failed to get NumLock keycode";
        return;
    }
    
    xcb_keycode_t keycode = keycodes[0];
    free(keycodes);
    
    // Simulate key press and release
    const xcb_window_t rootWindow = m_rootWindows.constFirst();
    xcb_test_fake_input(m_connection, XCB_KEY_PRESS, keycode, XCB_CURRENT_TIME, rootWindow, 0, 0, 0);
    xcb_test_fake_input(m_connection, XCB_KEY_RELEASE, keycode, XCB_CURRENT_TIME, rootWindow, 0, 0, 0);
    xcb_flush(m_connection);
}

// ==================== Modifier Key Handler ====================
void X11KeyHandler::onModifierKeyReleased(unsigned long keysym)
{
    const LogicalModifier releasedModifier = logicalModifier(keysym);
    if (releasedModifier == LogicalModifier::Unknown)
        return;

    QSet<QString> shortcutIds;
    for (uint32_t key : std::as_const(m_standaloneModifierKeys)) {
        const xcb_keycode_t keycode = key & 0xFFFF;
        if (logicalModifier(xcb_key_symbols_get_keysym(m_keySymbols, keycode, 0))
                == releasedModifier) {
            shortcutIds.insert(m_grabbedKeys.value(key));
        }
    }
    for (const QString &shortcutId : std::as_const(shortcutIds))
        activate(shortcutId, KeyEventFlag::Release);
}

// ==================== Helper Functions ====================
bool X11KeyHandler::isStandaloneModifierKey(xcb_keysym_t keysym, uint16_t mods) const
{
    return mods == 0 && logicalModifier(keysym) != LogicalModifier::Unknown;
}
