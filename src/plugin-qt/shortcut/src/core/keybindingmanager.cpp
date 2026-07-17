// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "keybindingmanager.h"
#include "commandlineparser.h"
#include "customshortcuttransaction.h"
#include "backend/abstractkeyhandler.h"
#include "backend/specialkeyhandler.h"
#include "config/configloader.h"
#include "actionexecutor.h"
#include "translationmanager.h"
#include "core/shortcutconfig.h"
#include "qkeysequenceconverter.h"
#include "physicalkeyalias.h"
#include "backend/x11/x11gestureactionexecutor.h"

#include <DGuiApplicationHelper>

#include <QDebug>
#include <QDBusMessage>
#include <QHash>
#include <QKeySequence>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <utility>

DGUI_USE_NAMESPACE

namespace {
constexpr const char *kDefaultShortcutAppId = "org.deepin.dde.keybinding";
constexpr const char *kNoHotkeyText = "None";

const QHash<QString, int> &reservedCategoryOrder()
{
    static const QHash<QString, int> order{
        {QStringLiteral("System"), 0},
        {QStringLiteral("Window"), 1},
        {QStringLiteral("Workspace"), 2},
        {QStringLiteral("AssistiveTools"), 3},
        {QStringLiteral("Custom"), 99},
    };
    return order;
}

QHash<QString, int> categoryDisplayOrders(const QMap<QString, KeyConfig> &configs)
{
    QHash<QString, int> orders = reservedCategoryOrder();
    int appOrder = 10;
    for (const KeyConfig &config : configs) {
        if (!config.modifiable || config.category.isEmpty()
                || orders.contains(config.category)) {
            continue;
        }
        orders.insert(config.category, appOrder++);
    }
    return orders;
}

const KeyConfig *shortcutConfig(const ShortcutInfo &info,
                                const QMap<QString, KeyConfig> &configs)
{
    const auto it = configs.constFind(info.id);
    return it == configs.constEnd() ? nullptr : &it.value();
}

bool isLegacyCustomShortcut(const ShortcutInfo &info,
                            const QMap<QString, KeyConfig> &configs)
{
    const KeyConfig *config = shortcutConfig(info, configs);
    return config && config->category == QLatin1String(CategoryKey::Custom)
            && config->displayOrder < 0;
}

int shortcutDisplayOrder(const ShortcutInfo &info,
                         const QMap<QString, KeyConfig> &configs,
                         const QHash<QString, int> &legacyCustomOrders)
{
    const KeyConfig *config = shortcutConfig(info, configs);
    if (!config)
        return std::numeric_limits<int>::max();
    if (config->category == QLatin1String(CategoryKey::Custom)
            && config->displayOrder < 0) {
        return legacyCustomOrders.value(config->getId(), std::numeric_limits<int>::max());
    }
    return config->displayOrder < 0 ? std::numeric_limits<int>::max() : config->displayOrder;
}

void sortShortcutInfos(QList<ShortcutInfo> &infos,
                       const QMap<QString, KeyConfig> &configs,
                       const QStringList &customShortcutSubPaths)
{
    const QHash<QString, int> categoryOrders = categoryDisplayOrders(configs);
    QHash<QString, int> legacyCustomOrders;
    legacyCustomOrders.reserve(customShortcutSubPaths.size());
    for (int index = 0; index < customShortcutSubPaths.size(); ++index)
        legacyCustomOrders.insert(customShortcutSubPaths.at(index), index);

    std::sort(infos.begin(), infos.end(), [&configs, &categoryOrders, &legacyCustomOrders]
              (const ShortcutInfo &left, const ShortcutInfo &right) {
        const int leftCategoryOrder = categoryOrders.value(left.category, 10);
        const int rightCategoryOrder = categoryOrders.value(right.category, 10);
        if (leftCategoryOrder != rightCategoryOrder)
            return leftCategoryOrder < rightCategoryOrder;

        if (left.category != right.category)
            return left.category < right.category;

        const bool leftLegacyCustom = isLegacyCustomShortcut(left, configs);
        const bool rightLegacyCustom = isLegacyCustomShortcut(right, configs);
        if (leftLegacyCustom != rightLegacyCustom)
            return leftLegacyCustom;

        const int leftOrder = shortcutDisplayOrder(left, configs, legacyCustomOrders);
        const int rightOrder = shortcutDisplayOrder(right, configs, legacyCustomOrders);
        if (leftOrder != rightOrder)
            return leftOrder < rightOrder;

        return left.id < right.id;
    });
}
}

// Normalize a hotkey from XKB form ("<Control><Alt>T") to Qt PortableText
// ("Ctrl+Alt+T"). Inputs already in Qt form pass through unchanged.
// dde-services emits XKB on the wire for legacy control-center compatibility
// but stores Qt internally, so callers may send either form back to us.
static QString normalizeHotkey(const QString &hotkey)
{
    QString normalized = hotkey.trimmed();
    if (normalized.isEmpty())
        return normalized;

    // Collapse X11-style physical aliases (e.g. KP_Delete) to the logical key
    // name for storage and conflict checks. Treeland resolves both the main
    // Delete key and keypad Delete key from the same Qt Delete registration;
    // only the X11 backend needs to expand the physical keycodes.
    normalized = PhysicalKeyAlias::canonicalize(normalized);

    if (SpecialKeyHandler::isKeycode(normalized)) {
        const uint32_t keycode = SpecialKeyHandler::parseKeycode(normalized);
        return keycode == 0 ? QString() : QString::number(keycode);
    }

    if (normalized.contains(QLatin1Char('<')) && normalized.contains(QLatin1Char('>'))) {
        normalized = QKeySequenceConverter::xkbToQKeySequence(normalized);
    } else {
        const QString converted = QKeySequenceConverter::xkbToQKeySequence(normalized);
        if (converted != normalized
                && !QKeySequence::fromString(converted, QKeySequence::PortableText).isEmpty()) {
            normalized = converted;
        }
    }

    const QKeySequence sequence = QKeySequence::fromString(normalized, QKeySequence::PortableText);
    if (!sequence.isEmpty() && sequence.count() == 1)
        return sequence.toString(QKeySequence::PortableText);

    return normalized;
}

static QStringList normalizeHotkeys(const QStringList &hotkeys)
{
    QStringList out;
    out.reserve(hotkeys.size());
    for (const QString &h : hotkeys)
        out.append(normalizeHotkey(h));
    return out;
}

static bool containsEmptyHotkey(const QStringList &hotkeys)
{
    return std::any_of(hotkeys.begin(), hotkeys.end(),
                      [](const QString &h) { return h.trimmed().isEmpty(); });
}

constexpr int MaxCustomShortcutCount = 200;
constexpr int MaxCustomShortcutNameLength = 128;
constexpr int MaxCustomShortcutCommandLength = 4096;
constexpr int MaxCustomShortcutHotkeyLength = 256;
constexpr int MaxShortcutHotkeyCount = 16;

static bool containsControlCharacter(const QString &text)
{
    for (const QChar ch : text) {
        const QChar::Category category = ch.category();
        if (category == QChar::Other_Control
            || category == QChar::Other_Format
            || category == QChar::Other_Surrogate
            || category == QChar::Other_PrivateUse
            || category == QChar::Other_NotAssigned) {
            return true;
        }
    }
    return false;
}

static bool isValidCustomShortcutName(const QString &name)
{
    return !name.isEmpty()
            && name.size() <= MaxCustomShortcutNameLength
            && !containsControlCharacter(name);
}

static bool isValidCustomShortcutCommand(const QString &command)
{
    return !command.isEmpty()
            && command.size() <= MaxCustomShortcutCommandLength
            && !containsControlCharacter(command);
}

static std::optional<QStringList> parseCustomShortcutCommand(const QString &command)
{
    if (!isValidCustomShortcutCommand(command))
        return std::nullopt;
    return CommandLineParser::split(command);
}

static bool isValidCustomShortcutHotkey(const QString &hotkey, bool allowEmpty)
{
    if (hotkey.isEmpty())
        return allowEmpty;
    if (hotkey.size() > MaxCustomShortcutHotkeyLength || containsControlCharacter(hotkey))
        return false;

    if (SpecialKeyHandler::isKeycode(hotkey))
        return SpecialKeyHandler::parseKeycode(hotkey) != 0;

    const QKeySequence sequence = QKeySequence::fromString(hotkey, QKeySequence::PortableText);
    if (sequence.isEmpty())
        return QKeySequenceConverter::qKeySequenceToXkb(hotkey).startsWith(QLatin1String("XF86"));
    if (sequence.count() != 1)
        return false;

    const QKeyCombination combination = sequence[0];
    if (combination.keyboardModifiers() != Qt::NoModifier)
        return true;

    const Qt::Key key = combination.key();
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return true;
    if (QKeySequenceConverter::isMultimediaKey(key))
        return true;

    switch (key) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_Print:
    case Qt::Key_SysReq:
    case Qt::Key_Pause:
        return true;
    default:
        return false;
    }
}

static bool areValidShortcutHotkeys(const QStringList &hotkeys)
{
    if (hotkeys.isEmpty() || hotkeys.size() > MaxShortcutHotkeyCount)
        return false;
    if (QSet<QString>(hotkeys.begin(), hotkeys.end()).size() != hotkeys.size())
        return false;
    return std::all_of(hotkeys.begin(), hotkeys.end(), [](const QString &hotkey) {
        return isValidCustomShortcutHotkey(hotkey, false);
    });
}

KeybindingManager::KeybindingManager(ConfigLoader *loader, ActionExecutor *executor,
                                     TranslationManager *translationManager,
                                     AbstractKeyHandler *keyHandler,
                                     X11GestureActionExecutor *x11ActionExecutor,
                                     QObject *parent)
    : QObject(parent)
    , m_loader(loader)
    , m_keyHandler(keyHandler)
    , m_specialKeyHandler(new SpecialKeyHandler(this))
    , m_executor(executor)
    , m_translationManager(translationManager)
    , m_x11ActionExecutor(x11ActionExecutor)
    , m_isWayland(DGuiApplicationHelper::testAttribute(DGuiApplicationHelper::IsWaylandPlatform))
{
    qRegisterMetaType<ShortcutInfo>("ShortcutInfo");
    qRegisterMetaType<QList<ShortcutInfo>>("QList<ShortcutInfo>");
    qRegisterMetaType<KeyConfig>("KeyConfig");

    qDBusRegisterMetaType<ShortcutInfo>();
    qDBusRegisterMetaType<QList<ShortcutInfo>>();

    qRegisterMetaType<CategoryInfo>("CategoryInfo");
    qRegisterMetaType<QList<CategoryInfo>>("QList<CategoryInfo>");
    qDBusRegisterMetaType<CategoryInfo>();
    qDBusRegisterMetaType<QList<CategoryInfo>>();

    // Connect signals from key handler
    connect(m_keyHandler, &AbstractKeyHandler::keyActivated, this, &KeybindingManager::onKeyActivated);
    if (!m_isWayland) {
        connect(m_keyHandler, &AbstractKeyHandler::captureStarted,
                this, [this] { m_specialKeyHandler->setEnabled(false); });
        connect(m_keyHandler, &AbstractKeyHandler::captureKeyEvent,
                this, &KeybindingManager::onCaptureKeyEvent);
        connect(m_keyHandler, &AbstractKeyHandler::captureFinished,
                this, [this] { m_specialKeyHandler->setEnabled(true); });
        connect(m_keyHandler, &AbstractKeyHandler::keymapAboutToChange,
                this, &KeybindingManager::onBackendKeymapAboutToChange);
        connect(m_keyHandler, &AbstractKeyHandler::keymapChanged,
                this, &KeybindingManager::onBackendKeymapChanged);
    }
    
    // Connect signals from special key handler
    connect(m_specialKeyHandler, &SpecialKeyHandler::keyActivated, this, &KeybindingManager::onKeyActivated);
    
    connect(m_loader, &ConfigLoader::keyConfigChanged, this, &KeybindingManager::onKeyConfigChanged);
    connect(m_loader, &ConfigLoader::keyConfigAdded,
            this, &KeybindingManager::onKeyConfigAdded);
    connect(m_loader, &ConfigLoader::configRemoved, this, &KeybindingManager::onConfigRemoved);
}

bool KeybindingManager::BeginCapture(uint timeoutMs)
{
    if (m_isWayland)
        return true;

    const QString owner = calledFromDBus() ? message().service() : QString();
    return m_keyHandler->beginCapture(timeoutMs, owner);
}

void KeybindingManager::EndCapture()
{
    if (m_isWayland)
        return;
    const QString owner = calledFromDBus() ? message().service() : QString();
    m_keyHandler->endCapture(owner);
}

KeybindingManager::~KeybindingManager() = default;

void KeybindingManager::registerAllShortcuts()
{
    qDebug() << "KeybindingManager: Registering all shortcuts...";
    for (const QString &id : m_activeShortcutIds.values())
        unregisterShortcut(id);

    m_keyConfigsMap.clear();
    m_activeShortcutIds.clear();

    QList<KeyConfig> configs = m_loader->keys();
    std::sort(configs.begin(), configs.end(), [](const KeyConfig &left, const KeyConfig &right) {
        return left.getId() < right.getId();
    });
    for (const KeyConfig &loadedConfig : std::as_const(configs)) {
        KeyConfig config = loadedConfig;
        config.hotkeys = normalizeHotkeys(config.hotkeys);
        m_keyConfigsMap[config.getId()] = config;
        if (config.canRegister() && !registerShortcut(config))
            qWarning() << "KeybindingManager: configured shortcut is inactive:" << config.getId();
    }

    qInfo() << "KeybindingManager: configured" << m_keyConfigsMap.size()
            << "active" << m_activeShortcutIds.size();
}

void KeybindingManager::clearState()
{
    qWarning() << "KeybindingManager: Marking runtime bindings inactive due to protocol disconnection";
    m_specialKeyHandler->clear();
    m_activeShortcutIds.clear();
}

QList<ShortcutInfo> KeybindingManager::ListAllShortcuts()
{
    QList<ShortcutInfo> list;
    for (const auto &config : m_keyConfigsMap) {
        // Only expose modifiable shortcuts — control center filters out
        // non-modifiable entries to avoid showing read-only items.
        // Empty hotkeys are still exposed so the control center can show
        // the existing row as "None" after another shortcut takes its binding.
        if (!config.modifiable) {
            continue;
        }
        list.append(toShortcutInfo(config));
    }

    sortShortcutInfos(list, m_keyConfigsMap, m_loader->customShortcutSubPaths());
    return list;
}

QList<ShortcutInfo> KeybindingManager::ListShortcutsByApp(const QString &appId)
{
    QList<ShortcutInfo> list;
    QList<KeyConfig> configs = m_keyConfigsMap.values();
    for (const KeyConfig &config : configs) {
        if (config.appId == appId) {
            list.append(toShortcutInfo(config));
        }
    }
    sortShortcutInfos(list, m_keyConfigsMap, m_loader->customShortcutSubPaths());
    return list;
}

QList<ShortcutInfo> KeybindingManager::ListShortcutsByCategory(const QString &category)
{
    QList<ShortcutInfo> list;
    QList<KeyConfig> configs = m_keyConfigsMap.values();
    for (const KeyConfig &config : configs) {
        if (config.category == category) {
            list.append(toShortcutInfo(config));
        }
    }

    sortShortcutInfos(list, m_keyConfigsMap, m_loader->customShortcutSubPaths());
    return list;
}

QList<CategoryInfo> KeybindingManager::ListCategories()
{
    // Collect distinct categories from the user-visible (modifiable, with
    // hotkeys) configs — mirrors ListAllShortcuts' filter so the category
    // set matches what the control center actually renders.
    QHash<QString, CategoryInfo> seen;
    const QHash<QString, int> categoryOrders = categoryDisplayOrders(m_keyConfigsMap);
    for (const auto &config : m_keyConfigsMap) {
        if (!config.modifiable || config.category.isEmpty())
            continue;
        if (!seen.contains(config.category)) {
            CategoryInfo ci;
            ci.key = config.category;
            ci.displayName = m_translationManager->translate(config.appId, config.category);
            ci.isCustom = (config.category == CategoryKey::Custom);
            ci.order = categoryOrders.value(config.category, 10);
            seen.insert(config.category, ci);
        }
    }

    QList<CategoryInfo> result = seen.values();
    std::sort(result.begin(), result.end(),
              [](const CategoryInfo &a, const CategoryInfo &b) {
        return a.order == b.order ? a.key < b.key : a.order < b.order;
    });
    return result;
}

ShortcutInfo KeybindingManager::GetShortcut(const QString &id)
{
    if (m_keyConfigsMap.contains(id)) {
        const auto &config = m_keyConfigsMap[id];
        return toShortcutInfo(config);
    }

    return ShortcutInfo();
}

QString KeybindingManager::GetShortcutCommand(const QString &id)
{
    const KeyConfig config = m_keyConfigsMap.value(id);
    if (config.category == QLatin1String(CategoryKey::Custom)
        && config.triggerType == static_cast<int>(TriggerType::Command)
        && !config.triggerValue.isEmpty()) {
        return CommandLineParser::join(config.triggerValue);
    }
    return QString();
}

ShortcutInfo KeybindingManager::LookupConflictShortcut(const QString &hotkey)
{
    const QString needle = normalizeHotkey(hotkey);
    for (const KeyConfig &config : m_keyConfigsMap) {
        if (!config.enabled) continue;
        if (config.hotkeys.contains(needle)) {
            return toShortcutInfo(config);
        }
    }
    return ShortcutInfo(); // Empty struct if no conflict
}

QList<ShortcutInfo> KeybindingManager::SearchShortcuts(const QString &keyword)
{
    QList<ShortcutInfo> list;
    if (keyword.isEmpty()) return list;

    for (auto it = m_keyConfigsMap.constBegin(); it != m_keyConfigsMap.constEnd(); ++it) {
        const KeyConfig &config = it.value();
        if (!config.isValid()) continue;

        // Match against shortcutId
        if (config.getId().contains(keyword, Qt::CaseInsensitive)) {
            list.append(toShortcutInfo(config));
            continue;
        }

        // Match against display name
        if (config.displayName.contains(keyword, Qt::CaseInsensitive)) {
            list.append(toShortcutInfo(config));
            continue;
        }

        // Match against localized display name
        QString localName = m_translationManager->translate(config.appId, config.displayName);
        if (localName.contains(keyword, Qt::CaseInsensitive)) {
            list.append(toShortcutInfo(config));
            continue;
        }

        // Match against hotkey combinations
        for (const QString &hk : config.hotkeys) {
            if (hk.contains(keyword, Qt::CaseInsensitive)) {
                list.append(toShortcutInfo(config));
                break;
            }
        }
    }

    sortShortcutInfos(list, m_keyConfigsMap, m_loader->customShortcutSubPaths());
    return list;
}

bool KeybindingManager::ModifyHotkeys(const QString &id, const QStringList &newHotkeys)
{
    if (!m_keyConfigsMap.contains(id)) return false;

    KeyConfig config = m_keyConfigsMap[id];
    if (!config.enabled || !config.modifiable) {
        qWarning() << "Shortcut is not modifiable or enabled:" << id << "enabled:"
                    << config.enabled << "modifiable:" << config.modifiable;
        return false;
    }

    // Accept either Qt PortableText ("Ctrl+Alt+T") or XKB form
    // ("<Control><Alt>T") on the wire; canonicalize to Qt internally.
    const QStringList normalized = normalizeHotkeys(newHotkeys);
    if (normalized.isEmpty() || containsEmptyHotkey(normalized)
            || !areValidShortcutHotkeys(normalized)) {
        qWarning() << "ModifyHotkeys: new hotkeys can not be empty:" << id;
        return false;
    }

    // Check for conflicts (exclude self)
    for (const QString &hotkey : normalized) {
        ShortcutInfo conflictInfo = LookupConflictShortcut(hotkey);
        if (!conflictInfo.id.isEmpty() && conflictInfo.id != id) {
            qWarning() << "Conflict detected with:" << conflictInfo.id << conflictInfo.displayName;
            return false;
        }
    }

    // Save old state for X11 rollback or persistence recovery.
    const QStringList oldHotkeys = config.hotkeys;

    // Phase 1: queue unbind + rebind
    unregisterShortcut(id);
    config.hotkeys = normalized;

    if (!registerShortcut(config, QStringList{id})) {
        qWarning() << "Failed to register new hotkeys:" << id << normalized;
        config.hotkeys = oldHotkeys;
        registerShortcut(config, QStringList{id});
        m_keyHandler->commitSync();
        return false;
    }

    // Phase 2: commit to compositor (sync — must know whether to roll back).
    if (!m_keyHandler->commitSync()) {
        qWarning() << "Shortcut commit failed for ModifyHotkeys:" << id;
        unregisterShortcut(id);
        config.hotkeys = oldHotkeys;
        registerShortcut(config, QStringList{id});
        m_keyHandler->commitSync();
        return false;
    }

    // Phase 3: only after a successful commit, persist to dconfig and notify.
    config.hotkeys = normalized;
    m_keyConfigsMap[id] = config;
    if (!m_loader->updateValue(id, "hotkeys", normalized)) {
        qWarning() << "ModifyHotkeys: failed to persist hotkeys, rolling back:" << id;
        config.hotkeys = oldHotkeys;
        m_keyConfigsMap[id] = config;
        unregisterShortcut(id);
        registerShortcut(config, QStringList{id});
        m_keyHandler->commitSync();
        return false;
    }
    emit ShortcutChanged(id, toShortcutInfo(config));

    return true;
}

QString KeybindingManager::AddCustomShortcut(const QString &name, const QString &action, const QString &hotkey)
{
    return addCustomShortcut(name, action, hotkey, QString());
}

QString KeybindingManager::AddCustomShortcutWithConflict(const QString &name, const QString &action,
                                                          const QString &hotkey,
                                                          const QString &expectedConflictId)
{
    if (expectedConflictId.trimmed().isEmpty()) {
        qWarning() << "AddCustomShortcutWithConflict: expected conflict id is empty";
        return QString();
    }

    return addCustomShortcut(name, action, hotkey, expectedConflictId);
}

QString KeybindingManager::addCustomShortcut(const QString &name, const QString &action,
                                             const QString &hotkey,
                                             const QString &expectedConflictId)
{
    const QString displayName = name.trimmed();
    const QString actionText = action.trimmed();
    const QString normalizedHotkey = normalizeHotkey(hotkey);

    if (runtimeCustomShortcutCount() >= MaxCustomShortcutCount) {
        qWarning() << "AddCustomShortcut: custom shortcut count limit reached";
        return QString();
    }

    const auto commandArguments = parseCustomShortcutCommand(actionText);
    if (!isValidCustomShortcutName(displayName)
        || !commandArguments
        || !isValidCustomShortcutHotkey(normalizedHotkey, false)) {
        qWarning() << "AddCustomShortcut: invalid input";
        return QString();
    }
    KeyConfig config;
    config.appId = QStringLiteral("org.deepin.dde.keybinding");
    config.subPath = createCustomShortcutId();
    config.displayOrder = nextCustomShortcutDisplayOrder();
    config.keyEventFlags = KeyEventFlag::Release;
    updateCustomShortcutConfigFields(config, displayName, *commandArguments, normalizedHotkey);

    CustomShortcutChange change;
    change.newTarget = config;
    if (!prepareConflictShortcutChange(normalizedHotkey, change, QString(), expectedConflictId)) {
        return QString();
    }

    CustomShortcutTransaction transaction(this, change);
    if (!transaction.applyRuntime()) {
        qWarning() << "AddCustomShortcut: failed to apply runtime change" << config.getId();
        return QString();
    }

    if (!transaction.persistAdd())
        return QString();

    transaction.publish();
    return config.getId();
}

bool KeybindingManager::ModifyCustomShortcut(const QString &id, const QString &name,
                                             const QString &action, const QString &hotkey)
{
    return modifyCustomShortcut(id, name, action, hotkey, QString());
}

bool KeybindingManager::ModifyCustomShortcutWithConflict(const QString &id, const QString &name,
                                                         const QString &action,
                                                         const QString &hotkey,
                                                         const QString &expectedConflictId)
{
    if (expectedConflictId.trimmed().isEmpty()) {
        qWarning() << "ModifyCustomShortcutWithConflict: expected conflict id is empty";
        return false;
    }

    return modifyCustomShortcut(id, name, action, hotkey, expectedConflictId);
}

bool KeybindingManager::modifyCustomShortcut(const QString &id, const QString &name,
                                             const QString &action, const QString &hotkey,
                                             const QString &expectedConflictId)
{
    if (!m_keyConfigsMap.contains(id)) {
        return false;
    }

    KeyConfig oldConfig = m_keyConfigsMap[id];
    if (!isRuntimeCustomShortcut(oldConfig)) {
        qWarning() << "ModifyCustomShortcut: shortcut is not a runtime custom shortcut:" << id;
        return false;
    }

    const QString displayName = name.trimmed();
    const QString actionText = action.trimmed();
    const QString normalizedHotkey = normalizeHotkey(hotkey);
    const auto commandArguments = parseCustomShortcutCommand(actionText);
    if (!isValidCustomShortcutName(displayName)
        || !commandArguments
        || !isValidCustomShortcutHotkey(normalizedHotkey, true)) {
        qWarning() << "ModifyCustomShortcut: invalid input" << id;
        return false;
    }
    KeyConfig newConfig = oldConfig;
    updateCustomShortcutConfigFields(newConfig, displayName, *commandArguments, normalizedHotkey);

    const bool hotkeysChanged = oldConfig.hotkeys != newConfig.hotkeys;
    if (!hotkeysChanged) {
        if (!expectedConflictId.isEmpty()) {
            qWarning() << "ModifyCustomShortcutWithConflict: hotkey is unchanged:" << id;
            return false;
        }

        if (oldConfig == newConfig)
            return true;

        if (!m_loader->updateCustomShortcut(newConfig)) {
            qWarning() << "ModifyCustomShortcut: failed to persist custom shortcut:" << id;
            return false;
        }

        m_keyConfigsMap[id] = newConfig;
        emit ShortcutChanged(id, toShortcutInfo(newConfig));
        return true;
    }

    CustomShortcutChange change;
    change.hasOldTarget = true;
    change.oldTarget = oldConfig;
    change.newTarget = newConfig;
    if (!prepareConflictShortcutChange(normalizedHotkey, change, id, expectedConflictId)) {
        return false;
    }

    CustomShortcutTransaction transaction(this, change);
    if (!transaction.applyRuntime()) {
        qWarning() << "ModifyCustomShortcut: failed to apply runtime change" << id;
        return false;
    }

    if (!transaction.persistModify())
        return false;

    transaction.publish();
    return true;
}

bool KeybindingManager::DeleteCustomShortcut(const QString &id)
{
    if (!m_keyConfigsMap.contains(id)) {
        return false;
    }

    KeyConfig oldConfig = m_keyConfigsMap[id];
    if (!isRuntimeCustomShortcut(oldConfig)) {
        qWarning() << "DeleteCustomShortcut: shortcut is not a runtime custom shortcut:" << id;
        return false;
    }

    unregisterShortcut(id);
    if (!m_keyHandler->commitSync()) {
        qWarning() << "DeleteCustomShortcut: commit failed" << id;
        if (registerShortcut(oldConfig, QStringList{id}))
            m_keyHandler->commitSync();
        return false;
    }

    if (!m_loader->removeCustomShortcut(id)) {
        qWarning() << "DeleteCustomShortcut: failed to remove persisted custom shortcut, restoring" << id;
        if (registerShortcut(oldConfig, QStringList{id}))
            m_keyHandler->commitSync();
        return false;
    }

    m_keyConfigsMap.remove(id);
    emit ShortcutRemoved(id);
    return true;
}

bool KeybindingManager::SwapHotkeys(const QString &id1, const QString &id2)
{
    if (id1 == id2)
        return false;

    if (!m_keyConfigsMap.contains(id1) || !m_keyConfigsMap.contains(id2))
        return false;

    KeyConfig config1 = m_keyConfigsMap[id1];
    KeyConfig config2 = m_keyConfigsMap[id2];

    if (!config1.enabled || !config1.modifiable || !config2.enabled || !config2.modifiable) {
        qWarning() << "SwapHotkeys: both shortcuts must be enabled and modifiable:"
                    << id1 << id2;
        return false;
    }
    if (!canPersistShortcutHotkeys(config1) || !canPersistShortcutHotkeys(config2)) {
        qWarning() << "SwapHotkeys: both shortcuts must have writable configs:"
                    << id1 << m_loader->canUpdateValue(id1)
                    << id2 << m_loader->canUpdateValue(id2);
        return false;
    }

    const QStringList hotkeys1 = config1.hotkeys;
    const QStringList hotkeys2 = config2.hotkeys;

    // Phase 1: unbind both, then rebind with swapped hotkeys.
    unregisterShortcut(id1);
    unregisterShortcut(id2);

    config1.hotkeys = hotkeys2;
    config2.hotkeys = hotkeys1;

    bool reg1 = registerShortcut(config1, QStringList{id1, id2});
    bool reg2 = registerShortcut(config2, QStringList{id1, id2});

    if (!reg1 || !reg2) {
        qWarning() << "SwapHotkeys: registerKey failed" << id1 << reg1 << id2 << reg2;
        if (rollbackRegistration(id1, id2, config1, config2, hotkeys1, hotkeys2) == RollbackResult::RebuildRequired)
            rebuildPersistedShortcutPair(id1, id2);

        return false;
    }

    // Phase 2: commit to compositor.  Do NOT update m_keyConfigsMap yet
    // — if commit fails we want the map to still hold the originals.
    if (!m_keyHandler->commitSync()) {
        qWarning() << "SwapHotkeys: commit failed";
        unregisterShortcut(id1);
        unregisterShortcut(id2);
        if (rollbackRegistration(id1, id2, config1, config2, hotkeys1, hotkeys2) == RollbackResult::RebuildRequired)
            rebuildPersistedShortcutPair(id1, id2);

        return false;
    }

    // Phase 3: persist both values before publishing the new in-memory state.
    const bool persisted1 = m_loader->updateValue(id1, "hotkeys", config1.hotkeys);
    const bool persisted2 = persisted1 && m_loader->updateValue(id2, "hotkeys", config2.hotkeys);
    if (!persisted2) {
        qWarning() << "SwapHotkeys: persistence failed, rolling back" << id1 << id2;
        const bool compensated1 = m_loader->updateValue(id1, "hotkeys", hotkeys1);
        const bool compensated2 = !persisted1 || m_loader->updateValue(id2, "hotkeys", hotkeys2);
        if (!compensated1 || !compensated2) {
            qCritical() << "SwapHotkeys: persistence compensation failed, rebuilding from DConfig" << id1 << id2;
            rebuildPersistedShortcutPair(id1, id2);
            return false;
        }
        unregisterShortcut(id1);
        unregisterShortcut(id2);
        if (rollbackRegistration(id1, id2, config1, config2, hotkeys1, hotkeys2) == RollbackResult::RebuildRequired)
            rebuildPersistedShortcutPair(id1, id2);

        return false;
    }

    m_keyConfigsMap[id1] = config1;
    m_keyConfigsMap[id2] = config2;
    emit ShortcutChanged(id1, toShortcutInfo(config1));
    emit ShortcutChanged(id2, toShortcutInfo(config2));

    return true;
}

KeybindingManager::RollbackResult KeybindingManager::rollbackRegistration(const QString &id1, const QString &id2,
                                                                          KeyConfig &config1, KeyConfig &config2,
                                                                          const QStringList &hotkeys1, const QStringList &hotkeys2)
{
    config1.hotkeys = hotkeys1;
    config2.hotkeys = hotkeys2;

    unregisterShortcut(id1);
    unregisterShortcut(id2);
    const bool registered1 = !config1.canRegister() || registerShortcut(config1, QStringList{id1, id2});
    const bool registered2 = !config2.canRegister() || registerShortcut(config2, QStringList{id1, id2});
    if (!registered1 || !registered2) {
        qCritical() << "rollbackRegistration: failed to restore runtime bindings:"
                    << id1 << registered1 << id2 << registered2;
        unregisterShortcut(id1);
        unregisterShortcut(id2);
        return RollbackResult::RebuildRequired;
    }

    if (!m_keyHandler->commitSync()) {
        qCritical() << "rollbackRegistration: commitSync failed";
        unregisterShortcut(id1);
        unregisterShortcut(id2);
        return RollbackResult::RebuildRequired;
    }

    m_keyConfigsMap[id1] = config1;
    m_keyConfigsMap[id2] = config2;
    emit ShortcutChanged(id1, toShortcutInfo(config1));
    emit ShortcutChanged(id2, toShortcutInfo(config2));
    return RollbackResult::Success;
}

void KeybindingManager::rebuildPersistedShortcutPair(const QString &id1, const QString &id2)
{
    unregisterShortcut(id1);
    unregisterShortcut(id2);
    if (!m_keyHandler->commitSync()) {
        qCritical() << "Failed to commit shortcut cleanup before DConfig rebuild:" << id1 << id2;
    }

    KeyConfig config1;
    KeyConfig config2;
    const bool loaded1 = m_loader->reloadKeyConfig(id1, &config1);
    const bool loaded2 = m_loader->reloadKeyConfig(id2, &config2);

    if (loaded1) {
        config1.hotkeys = normalizeHotkeys(config1.hotkeys);
        if (config1.enabled)
            m_keyConfigsMap[id1] = config1;
        else
            m_keyConfigsMap.remove(id1);
    }

    if (loaded2) {
        config2.hotkeys = normalizeHotkeys(config2.hotkeys);
        if (config2.enabled)
            m_keyConfigsMap[id2] = config2;
        else
            m_keyConfigsMap.remove(id2);
    }

    bool registered1 = true;
    bool registered2 = true;
    if (loaded1 && config1.canRegister())
        registered1 = registerShortcut(config1, QStringList{id1, id2});
    if (loaded2 && config2.canRegister())
        registered2 = registerShortcut(config2, QStringList{id1, id2});

    if (!registered1 || !registered2) {
        qWarning() << "Failed to fully rebuild shortcuts from DConfig:"
                   << id1 << registered1 << id2 << registered2;
        unregisterShortcut(id1);
        unregisterShortcut(id2);
    }
    if ((registered1 && registered2 && !m_keyHandler->commitSync())
            || (!registered1 || !registered2)) {
        qCritical() << "Failed to commit shortcuts rebuilt from DConfig:" << id1 << id2;
        unregisterShortcut(id1);
        unregisterShortcut(id2);
        if (!m_keyHandler->commitSync())
            qCritical() << "Failed to commit cleanup after DConfig rebuild;"
                        << "local runtime mappings remain empty:" << id1 << id2;
    }

    if (loaded1)
        emit ShortcutChanged(id1, toShortcutInfo(config1));
    if (loaded2)
        emit ShortcutChanged(id2, toShortcutInfo(config2));

    if (!loaded1 || !loaded2)
        qCritical() << "Failed to reload shortcut configuration:" << id1 << loaded1 << id2 << loaded2;
}

bool KeybindingManager::ReplaceHotkey(const QString &targetId, const QString &newHotkey, const QString &conflictId)
{
    if (targetId == conflictId) return false;
    if (!m_keyConfigsMap.contains(targetId) || !m_keyConfigsMap.contains(conflictId))
        return false;

    KeyConfig targetConfig = m_keyConfigsMap[targetId];
    KeyConfig conflictConfig = m_keyConfigsMap[conflictId];

    if (!targetConfig.enabled || !targetConfig.modifiable) {
        qWarning() << "ReplaceHotkey: target not modifiable or enabled:" << targetId;
        return false;
    }
    if (!conflictConfig.enabled || !conflictConfig.modifiable) {
        qWarning() << "ReplaceHotkey: conflict shortcut not modifiable or enabled:" << conflictId;
        return false;
    }
    if (!canPersistShortcutHotkeys(targetConfig) || !canPersistShortcutHotkeys(conflictConfig)) {
        qWarning() << "ReplaceHotkey: target and conflict shortcuts must have writable configs:"
                    << targetId << m_loader->canUpdateValue(targetId)
                    << conflictId << m_loader->canUpdateValue(conflictId);
        return false;
    }

    const QString normalized = normalizeHotkey(newHotkey);
    if (normalized.trimmed().isEmpty() || !isValidCustomShortcutHotkey(normalized, false)) {
        qWarning() << "ReplaceHotkey: new hotkey can not be empty:" << targetId;
        return false;
    }

    // Remove the hotkey from the conflict shortcut
    if (!conflictConfig.hotkeys.removeOne(normalized)) {
        qWarning() << "ReplaceHotkey: hotkey" << normalized << "not found in conflict shortcut" << conflictId;
        return false;
    }

    // Replace the target's hotkeys with the new one (not append).
    targetConfig.hotkeys.clear();
    targetConfig.hotkeys.append(normalized);

    // Save old state for rollback
    const QStringList oldTargetHotkeys = m_keyConfigsMap[targetId].hotkeys;
    const QStringList oldConflictHotkeys = m_keyConfigsMap[conflictId].hotkeys;

    // Phase 1: unregister both, then register both with new state
    unregisterShortcut(targetId);
    unregisterShortcut(conflictId);

    bool regTarget = registerShortcut(targetConfig, QStringList{targetId, conflictId});
    bool regConflict = true;
    if (!conflictConfig.hotkeys.isEmpty()) {
        regConflict = registerShortcut(conflictConfig, QStringList{targetId, conflictId});
    }

    if (!regTarget || !regConflict) {
        qWarning() << "ReplaceHotkey: registerKey failed" << targetId << regTarget << conflictId << regConflict;
        unregisterShortcut(targetId);
        unregisterShortcut(conflictId);
        if (rollbackRegistration(targetId, conflictId, targetConfig, conflictConfig,
                                 oldTargetHotkeys, oldConflictHotkeys)
                == RollbackResult::RebuildRequired)
            rebuildPersistedShortcutPair(targetId, conflictId);
        return false;
    }

    // Phase 2: commit to compositor
    if (!m_keyHandler->commitSync()) {
        qWarning() << "ReplaceHotkey: commit failed";
        unregisterShortcut(targetId);
        unregisterShortcut(conflictId);
        if (rollbackRegistration(targetId, conflictId, targetConfig, conflictConfig,
                                 oldTargetHotkeys, oldConflictHotkeys)
                == RollbackResult::RebuildRequired)
            rebuildPersistedShortcutPair(targetId, conflictId);
        return false;
    }

    // Phase 3: persist both values before publishing the new in-memory state.
    const bool targetPersisted = m_loader->updateValue(targetId, "hotkeys", targetConfig.hotkeys);
    const bool conflictPersisted = targetPersisted
            && m_loader->updateValue(conflictId, "hotkeys", conflictConfig.hotkeys);
    if (!conflictPersisted) {
        qWarning() << "ReplaceHotkey: persistence failed, rolling back" << targetId << conflictId;
        const bool targetCompensated = m_loader->updateValue(targetId, "hotkeys", oldTargetHotkeys);
        const bool conflictCompensated = !targetPersisted
                || m_loader->updateValue(conflictId, "hotkeys", oldConflictHotkeys);
        if (!targetCompensated || !conflictCompensated) {
            qCritical() << "ReplaceHotkey: persistence compensation failed, rebuilding from DConfig" << targetId << conflictId;
            rebuildPersistedShortcutPair(targetId, conflictId);
            return false;
        }
        unregisterShortcut(targetId);
        unregisterShortcut(conflictId);
        if (rollbackRegistration(targetId, conflictId, targetConfig, conflictConfig,
                                 oldTargetHotkeys, oldConflictHotkeys)
                == RollbackResult::RebuildRequired)
            rebuildPersistedShortcutPair(targetId, conflictId);
        return false;
    }

    m_keyConfigsMap[targetId] = targetConfig;
    m_keyConfigsMap[conflictId] = conflictConfig;
    emit ShortcutChanged(targetId, toShortcutInfo(targetConfig));
    emit ShortcutChanged(conflictId, toShortcutInfo(conflictConfig));

    return true;
}

bool KeybindingManager::Disable(const QString &id)
{
    if (!m_keyConfigsMap.contains(id)) {
        return false;
    }

    KeyConfig oldConfig = m_keyConfigsMap[id];
    if (!canPersistShortcutHotkeys(oldConfig))
        return false;

    unregisterShortcut(id);
    if (!m_keyHandler->commitSync()) {
        registerShortcut(oldConfig, {id});
        m_keyHandler->commitSync();
        return false;
    }

    KeyConfig newConfig = oldConfig;
    newConfig.hotkeys.clear();
    m_keyConfigsMap.insert(id, newConfig);
    if (!m_loader->updateValue(id, QStringLiteral("hotkeys"), QStringList())) {
        m_keyConfigsMap.insert(id, oldConfig);
        registerShortcut(oldConfig, {id});
        m_keyHandler->commitSync();
        return false;
    }

    emit ShortcutChanged(id, toShortcutInfo(newConfig));
    return true;
}

void KeybindingManager::ReloadConfigs()
{
    qInfo() << "ReloadConfigs called via DBus (delegating to Smart ConfigLoader)";
    
    // ConfigLoader will calculate diff (Add/Remove) and emit:
    // - configRemoved(id) -> Triggers unregister
    // - keyConfigChanged(config) -> Triggers register/update
    // - keyConfigAdded(config) -> Triggers register
    m_loader->reload();
    m_translationManager->reload();
}

void KeybindingManager::Reset()
{
    // Reset shortcut hotkeys to defaults; other fields (enabled, etc.) are untouched.
    const QStringList resetIds = m_loader->resettableHotkeyIds();
    if (resetIds.isEmpty()) {
        return;
    }

    // DConfig resets are delivered later as per-key valueChanged signals.
    // Release all reset targets first so one restored default does not conflict
    // with another target's stale binding.
    QStringList toRestore;
    for (const QString &id : resetIds) {
        if (!m_keyConfigsMap.contains(id)) {
            continue;
        }

        unregisterShortcut(id);
        toRestore.append(id);
    }

    if (!m_keyHandler->commitSync()) {
        qWarning() << "Reset: failed to commit unregistering existing hotkeys";
        for (const QString &id : std::as_const(toRestore)) {
            const KeyConfig config = m_keyConfigsMap.value(id);
            if (config.canRegister())
                registerShortcut(config, QStringList{id});
        }
        m_keyHandler->commitSync();
        return;
    }

    m_loader->resetHotkeys(resetIds);
}

void KeybindingManager::onKeyConfigAdded(const KeyConfig &loadedConfig)
{
    KeyConfig config = loadedConfig;
    config.hotkeys = normalizeHotkeys(config.hotkeys);
    m_keyConfigsMap[config.getId()] = config;

    if (config.canRegister()) {
        const bool registered = registerShortcut(config, QStringList{config.getId()});
        if (!registered || !m_keyHandler->commitSync()) {
            qWarning() << "KeybindingManager: new shortcut is inactive:" << config.getId();
            unregisterShortcut(config.getId());
            m_keyHandler->commitSync();
        }
    }
    emit ShortcutChanged(config.getId(), toShortcutInfo(config));
}

void KeybindingManager::onKeyConfigChanged(const KeyConfig &loadedConfig)
{
    if (!m_keyConfigsMap.contains(loadedConfig.getId())) {
        onKeyConfigAdded(loadedConfig);
        return;
    }
    KeyConfig config = loadedConfig;
    config.hotkeys = normalizeHotkeys(config.hotkeys);
    const KeyConfig oldConfig = m_keyConfigsMap.value(config.getId());
    if (oldConfig == config) {
        if (config.canRegister() && !m_activeShortcutIds.contains(config.getId())) {
            const bool registered = registerShortcut(config, QStringList{config.getId()});
            if (!registered || !m_keyHandler->commitSync()) {
                qWarning() << "KeybindingManager: failed to restore inactive shortcut:"
                           << config.getId();
                unregisterShortcut(config.getId());
                m_keyHandler->commitSync();
            }
        }
        return;
    }

    const bool oldWasActive = m_activeShortcutIds.contains(config.getId());
    if (oldWasActive)
        unregisterShortcut(config.getId());

    if (!config.enabled) {
        m_keyConfigsMap.remove(config.getId());
    } else {
        m_keyConfigsMap[config.getId()] = config;
    }

    bool registered = false;
    if (config.canRegister())
        registered = registerShortcut(config, QStringList{config.getId()});

    if (oldWasActive || registered) {
        if (!registered && config.canRegister())
            qWarning() << "KeybindingManager: changed shortcut is inactive:" << config.getId();
        if (!m_keyHandler->commitSync()) {
            qWarning() << "KeybindingManager: config change commit failed, leaving shortcut inactive:"
                       << config.getId();
        }
    }

    emit ShortcutChanged(config.getId(), toShortcutInfo(config));
}

void KeybindingManager::onConfigRemoved(const QString &id)
{
    if (m_keyConfigsMap.contains(id)) {
        m_keyConfigsMap.remove(id);
        const bool wasActive = m_activeShortcutIds.contains(id);
        if (wasActive) {
            unregisterShortcut(id);
            if (!m_keyHandler->commitSync())
                clearState();
        }

        emit ShortcutRemoved(id);
    } else if (id.contains(".shortcut")) {
        // configRemoved is shared by key/gesture; suffix says it's ours but
        // it's not in the map — registerShortcut() must have failed at init.
        // Worth a warning.
        qWarning() << "KeybindingManager: shortcut id removed but was never registered:" << id;
    } else {
        qDebug() << "KeybindingManager: ignoring removal of non-key id:" << id;
    }
}

void KeybindingManager::onKeyActivated(const QString &shortcutId)
{
    qDebug() << "Key activated:" << shortcutId;
    
    if (m_keyConfigsMap.contains(shortcutId) && m_activeShortcutIds.contains(shortcutId)) {
        const auto &config = m_keyConfigsMap[shortcutId];
        if (!m_isWayland && config.triggerType == static_cast<int>(TriggerType::Action)
                && m_x11ActionExecutor) {
            m_x11ActionExecutor->execute(config);
        } else {
            m_executor->execute(config);
        }
        emit ShortcutActivated(shortcutId, config.triggerValue);
    }
}

void KeybindingManager::onCaptureKeyEvent(bool pressed, const QString &keystroke)
{
    emit KeyEvent(pressed, keystroke);
}

bool KeybindingManager::registerShortcut(const KeyConfig &config, const QStringList &excludeIds)
{
    if (!config.canRegister()) {
        qWarning() << "Shortcut can not be registered, skipping:"
                   << "Enabled:" << config.enabled
                   << "AppId:" << config.appId
                   << "DisplayName:" << config.displayName
                   << "hotkeys:" << config.hotkeys;
        return false;
    }
    if (config.hotkeys.size() > MaxShortcutHotkeyCount) {
        qWarning() << "Shortcut has too many hotkeys, keeping it configured but inactive:"
                   << config.getId() << config.hotkeys.size();
        return false;
    }

    // Separate hotkeys into normal keys and keycodes
    QStringList normalHotkeys;
    QStringList keycodeHotkeys;
    QSet<QString> seenHotkeys;

    for (const QString &hotkey : config.hotkeys) {
        if (seenHotkeys.contains(hotkey))
            continue;
        seenHotkeys.insert(hotkey);

        if (!isValidCustomShortcutHotkey(hotkey, false)) {
            qWarning() << "Shortcut contains an invalid hotkey, skipping:" << config.getId() << hotkey;
            continue;
        }

        if (SpecialKeyHandler::isKeycode(hotkey)) {
            keycodeHotkeys.append(hotkey);
        } else {
            normalHotkeys.append(hotkey);
        }
    }

    if (normalHotkeys.isEmpty() && keycodeHotkeys.isEmpty())
        return false;

    bool normalRegistered = normalHotkeys.isEmpty();
    bool specialRegistered = keycodeHotkeys.isEmpty();

    // Register normal hotkeys via AbstractKeyHandler (X11/Wayland)
    if (!normalHotkeys.isEmpty()) {
        // Check for conflicts before registering
        for (const QString &hotkey : normalHotkeys) {
            const QString conflictId = lookupRuntimeConflict(hotkey, excludeIds);
            if (!conflictId.isEmpty()) {
                qWarning() << "Shortcut conflict detected during init:"
                            << "Config appId:" << config.appId
                            << "Config displayName:" << config.displayName
                            << "Config hotkeys:" << config.hotkeys
                            << "Conflicts with:" << conflictId
                            << "- Skipping registration";
                return false;
            }
        }

        // Create a config with only normal hotkeys
        KeyConfig normalConfig = config;
        normalConfig.hotkeys = normalHotkeys;
        
        normalRegistered = m_keyHandler->registerKey(normalConfig);
        if (!normalRegistered)
            return false;
    }

    // Register keycode hotkeys via SpecialKeyHandler
    if (!keycodeHotkeys.isEmpty()) {
        KeyConfig keycodeConfig = config;
        keycodeConfig.hotkeys = keycodeHotkeys;
        
        specialRegistered = m_specialKeyHandler->registerKey(keycodeConfig);
        if (!specialRegistered) {
            if (normalRegistered && !normalHotkeys.isEmpty())
                m_keyHandler->unregisterKey(config.getId());
            return false;
        }
    }

    if (normalRegistered && specialRegistered) {
        m_activeShortcutIds.insert(config.getId());
        return true;
    }
    return false;
}

void KeybindingManager::unregisterShortcut(const QString &id)
{
    m_keyHandler->unregisterKey(id);
    m_specialKeyHandler->unregisterKey(id);
    m_activeShortcutIds.remove(id);
}

QString KeybindingManager::lookupRuntimeConflict(const QString &hotkey,
                                                 const QStringList &excludeIds) const
{
    const QString normalized = normalizeHotkey(hotkey);
    for (const QString &id : m_activeShortcutIds) {
        if (excludeIds.contains(id))
            continue;
        const auto configIt = m_keyConfigsMap.constFind(id);
        if (configIt != m_keyConfigsMap.constEnd() && configIt->hotkeys.contains(normalized))
            return id;
    }
    return QString();
}

void KeybindingManager::onBackendKeymapChanged()
{
    qInfo() << "KeybindingManager: keyboard mapping changed, rebuilding X11 grabs";
    for (const KeyConfig &config : std::as_const(m_keyConfigsMap)) {
        if (config.canRegister() && !registerShortcut(config, QStringList{config.getId()})) {
            qWarning() << "KeybindingManager: shortcut remains inactive after keymap change:" << config.getId();
        }
    }
    m_keyHandler->commitSync();
}

void KeybindingManager::onBackendKeymapAboutToChange()
{
    const QStringList activeIds = m_activeShortcutIds.values();
    for (const QString &id : activeIds)
        unregisterShortcut(id);
    m_keyHandler->commitSync();
}

bool KeybindingManager::isRuntimeCustomShortcut(const KeyConfig &config) const
{
    return config.category == QLatin1String(CategoryKey::Custom)
            && config.modifiable
            && config.subPath.startsWith(QStringLiteral("org.deepin.dde.keybinding.shortcut.custom."));
}

bool KeybindingManager::canPersistShortcutHotkeys(const KeyConfig &config) const
{
    return config.modifiable && m_loader->canUpdateValue(config.getId());
}

int KeybindingManager::runtimeCustomShortcutCount() const
{
    int count = 0;
    for (const KeyConfig &config : m_keyConfigsMap) {
        if (isRuntimeCustomShortcut(config))
            ++count;
    }
    return count;
}

int KeybindingManager::nextCustomShortcutDisplayOrder() const
{
    int maxOrder = 0;
    for (const KeyConfig &config : m_keyConfigsMap) {
        if (isRuntimeCustomShortcut(config) && config.displayOrder > maxOrder)
            maxOrder = config.displayOrder;
    }
    return maxOrder + 10;
}

QString KeybindingManager::createCustomShortcutId() const
{
    QString id;
    do {
        id = QStringLiteral("org.deepin.dde.keybinding.shortcut.custom.")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
    } while (m_keyConfigsMap.contains(id));
    return id;
}

void KeybindingManager::updateCustomShortcutConfigFields(KeyConfig &config, const QString &displayName,
                                                         const QStringList &commandArguments,
                                                         const QString &normalizedHotkey) const
{
    config.displayName = displayName;
    config.category = QString::fromLatin1(CategoryKey::Custom);
    config.enabled = true;
    config.modifiable = true;
    config.triggerType = static_cast<int>(TriggerType::Command);
    config.triggerValue = commandArguments;
    config.hotkeys = normalizedHotkey.isEmpty() ? QStringList() : QStringList{normalizedHotkey};
}

ShortcutInfo KeybindingManager::toShortcutInfo(const KeyConfig &config)
{
    ShortcutInfo info;
    info.id = config.getId();
    info.displayName = config.displayName;
    info.category = config.category;
    // Emit hotkeys in X11/XKB form (e.g. "<Control><Alt>T", "XF86AudioMute")
    // so the legacy control-center shortcut page can render each modifier as
    // a separate chip without doing its own format translation.
    if (config.hotkeys.isEmpty()) {
        info.hotkeys.append(localizedNoHotkeyText());
    } else {
        info.hotkeys.reserve(config.hotkeys.size());
        for (const QString &hk : config.hotkeys) {
            info.hotkeys.append(QKeySequenceConverter::qKeySequenceToXkb(hk));
        }
    }
    info.localLanguageName = m_translationManager->translate(config.appId, config.displayName);
    info.isCustom = (config.category == CategoryKey::Custom);
    info.modifiable = config.modifiable;
    info.localLanguageCategory = m_translationManager->translate(config.appId, config.category);
    return info;
}

QString KeybindingManager::localizedNoHotkeyText() const
{
    return m_translationManager->translate(QString::fromLatin1(kDefaultShortcutAppId),
                                           QString::fromLatin1(kNoHotkeyText));
}

uint KeybindingManager::GetNumLockState() const
{
    if (m_keyHandler) {
        return m_keyHandler->getNumLockState() ? 1 : 0;
    }
    return 0;
}

uint KeybindingManager::GetCapsLockState() const
{
    if (m_keyHandler) {
        return m_keyHandler->getCapsLockState() ? 1 : 0;
    }
    return 0;
}

void KeybindingManager::SetNumLockState(uint state)
{
    if (m_keyHandler) {
        uint oldState = GetNumLockState();
        m_keyHandler->setNumLockState(state != 0);
        if (oldState != state) {
            emit NumLockStateChanged(state);
        }
    }
}

void KeybindingManager::SetCapsLockState(uint state)
{
    if (m_keyHandler) {
        uint oldState = GetCapsLockState();
        m_keyHandler->setCapsLockState(state != 0);
        if (oldState != state) {
            emit CapsLockStateChanged(state);
        }
    }
}

bool KeybindingManager::prepareConflictShortcutChange(const QString &hotkey,
                                                      CustomShortcutChange &change,
                                                      const QString &selfId,
                                                      const QString &expectedConflictId)
{
    change.hasConflict = false;
    change.oldConflict = KeyConfig();
    change.newConflict = KeyConfig();

    const QString normalizedHotkey = normalizeHotkey(hotkey);
    if (normalizedHotkey.isEmpty()) {
        if (expectedConflictId.isEmpty())
            return true;

        qWarning() << "prepareConflictShortcutChange: expected conflict for empty hotkey:" << expectedConflictId;
        return false;
    }

    ShortcutInfo conflictInfo = LookupConflictShortcut(normalizedHotkey);
    if (conflictInfo.id.isEmpty() || conflictInfo.id == selfId) {
        if (expectedConflictId.isEmpty())
            return true;

        qWarning() << "prepareConflictShortcutChange: expected conflict no longer exists:" << expectedConflictId;
        return false;
    }

    if (expectedConflictId.isEmpty()) {
        qWarning() << "prepareConflictShortcutChange: unconfirmed conflict:" << conflictInfo.id;
        return false;
    }

    if (conflictInfo.id != expectedConflictId) {
        qWarning() << "prepareConflictShortcutChange: conflict changed, expected:"
                   << expectedConflictId << "actual:" << conflictInfo.id;
        return false;
    }

    if (!m_keyConfigsMap.contains(conflictInfo.id)) {
        qWarning() << "prepareConflictShortcutChange: conflict shortcut is missing:" << conflictInfo.id;
        return false;
    }

    KeyConfig conflictConfig = m_keyConfigsMap.value(conflictInfo.id);
    if (!canPersistShortcutHotkeys(conflictConfig)) {
        qWarning() << "prepareConflictShortcutChange: conflict shortcut can not be replaced:"
                    << conflictInfo.id
                    << "modifiable:" << conflictConfig.modifiable
                    << "has writable config:" << m_loader->canUpdateValue(conflictInfo.id);
        return false;
    }

    KeyConfig newConflictConfig = conflictConfig;
    if (!newConflictConfig.hotkeys.removeOne(normalizedHotkey)) {
        qWarning() << "prepareConflictShortcutChange: hotkey not found in conflict shortcut:"
                   << normalizedHotkey << conflictInfo.id;
        return false;
    }

    change.hasConflict = true;
    change.oldConflict = conflictConfig;
    change.newConflict = newConflictConfig;

    return true;
}
