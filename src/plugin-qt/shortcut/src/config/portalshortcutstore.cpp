// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "portalshortcutstore.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QKeySequence>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

const QString AppId = QStringLiteral("org.deepin.dde.keybinding");
const QString ConfigName = QStringLiteral("org.deepin.shortcut.portal");
const QString ConfigDirectory = QStringLiteral("deepin/dde-services/shortcut");
const QString IniFileName = QStringLiteral("portal-shortcuts.ini");
const QString PortalPrefix = QStringLiteral("org.deepin.dde.keybinding.shortcut.portal.");

constexpr qsizetype MaximumAppIdLength = 255;
constexpr qsizetype MaximumShortcutIdLength = 128;
constexpr qsizetype MaximumDescriptionLength = 256;
constexpr qsizetype MaximumHotkeyLength = 256;

bool containsUnsafeCharacter(const QString &text)
{
    if (!text.isValidUtf16())
        return true;
    for (char32_t character : text.toUcs4()) {
        const QChar::Category category = QChar::category(character);
        if (category == QChar::Other_Control || category == QChar::Other_Format
            || category == QChar::Other_Surrogate || category == QChar::Other_PrivateUse
            || category == QChar::Other_NotAssigned) {
            return true;
        }
    }
    return false;
}

bool isValidStoredHotkey(const QString &hotkey)
{
    if (hotkey.isEmpty() || hotkey.size() > MaximumHotkeyLength
        || containsUnsafeCharacter(hotkey)) {
        return false;
    }
    const QKeySequence sequence = QKeySequence::fromString(hotkey, QKeySequence::PortableText);
    return !sequence.isEmpty() && sequence.count() == 1
            && sequence.toString(QKeySequence::PortableText) == hotkey;
}

bool valuesEqual(const QVariant &actual, const QVariant &expected)
{
    if (expected.metaType() == QMetaType::fromType<QStringList>())
        return actual.toStringList() == expected.toStringList();
    return actual == expected;
}

} // namespace

PortalShortcutStore::PortalShortcutStore(QObject *parent)
    : PortalShortcutStore(
            QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                    .absoluteFilePath(ConfigDirectory + QLatin1Char('/') + IniFileName),
            parent)
{
}

PortalShortcutStore::PortalShortcutStore(const QString &iniPath, QObject *parent)
    : QObject(parent)
    , m_iniPath(iniPath)
{
}

bool PortalShortcutStore::load(QString *errorMessage)
{
    bool pathsOk = true;
    const QStringList paths = orderedSubPaths(&pathsOk);
    if (!pathsOk) {
        setError(errorMessage, QStringLiteral("Could not read the Portal shortcut index"));
        return false;
    }

    QHash<QString, PortalShortcutInfo> loadedRecords;
    QHash<QString, QPointer<DConfig>> loadedConfigs;
    QHash<QString, qsizetype> perApplicationCounts;
    for (const QString &path : paths) {
        if (loadedRecords.size() >= MaximumPersistedShortcuts) {
            qCritical() << "PortalShortcutStore: persisted shortcut limit exceeded";
            break;
        }

        DConfig *config = createConfig(path);
        if (!config || !config->isValid()) {
            qWarning() << "PortalShortcutStore: invalid DConfig for indexed subpath" << path;
            if (config)
                config->deleteLater();
            continue;
        }

        PortalShortcutInfo shortcut;
        QString readError;
        if (!readShortcut(path, config, shortcut, &readError)) {
            qCritical() << "PortalShortcutStore: ignored corrupt indexed record" << path
                        << readError;
            config->deleteLater();
            continue;
        }
        if (perApplicationCounts.value(shortcut.appId) >= MaximumShortcutsPerApplication) {
            qCritical() << "PortalShortcutStore: per-application persisted shortcut limit exceeded";
            config->deleteLater();
            continue;
        }
        loadedRecords.insert(path, shortcut);
        loadedConfigs.insert(path, config);
        ++perApplicationCounts[shortcut.appId];
    }

    for (DConfig *config : std::as_const(m_configs)) {
        if (config && !loadedConfigs.values().contains(config))
            config->deleteLater();
    }
    m_records = loadedRecords;
    m_configs = loadedConfigs;
    return true;
}

PortalShortcutInfoList PortalShortcutStore::shortcutsForApp(const QString &appId) const
{
    PortalShortcutInfoList shortcuts;
    for (const PortalShortcutInfo &shortcut : m_records) {
        if (shortcut.appId == appId)
            shortcuts.append(shortcut);
    }
    std::sort(shortcuts.begin(), shortcuts.end(), [](const auto &left, const auto &right) {
        return left.shortcutId < right.shortcutId;
    });
    return shortcuts;
}

std::optional<PortalShortcutInfo> PortalShortcutStore::shortcut(const QString &appId,
                                                                const QString &shortcutId) const
{
    const QString id = internalId(appId, shortcutId);
    const auto record = m_records.constFind(id);
    if (record == m_records.cend() || record->appId != appId || record->shortcutId != shortcutId)
        return std::nullopt;
    return *record;
}

bool PortalShortcutStore::apply(const PortalShortcutInfoList &shortcuts, QString *errorMessage)
{
    if (shortcuts.isEmpty())
        return true;

    QHash<QString, PortalShortcutInfo> desired;
    QHash<QString, int> perApplicationCounts;
    for (PortalShortcutInfo shortcut : shortcuts) {
        shortcut.active = false;
        QString validationError;
        if (!validate(shortcut, &validationError)) {
            setError(errorMessage, validationError);
            return false;
        }
        if (desired.contains(shortcut.id)) {
            setError(errorMessage, QStringLiteral("The persistence batch contains duplicate records"));
            return false;
        }
        desired.insert(shortcut.id, shortcut);
        ++perApplicationCounts[shortcut.appId];
    }

    for (auto it = perApplicationCounts.cbegin(); it != perApplicationCounts.cend(); ++it) {
        qsizetype existingOutsideBatch = 0;
        for (const PortalShortcutInfo &stored : m_records) {
            if (stored.appId == it.key() && !desired.contains(stored.id))
                ++existingOutsideBatch;
        }
        if (existingOutsideBatch + it.value() > MaximumShortcutsPerApplication) {
            setError(errorMessage, QStringLiteral("The application shortcut limit was exceeded"));
            return false;
        }
    }
    qsizetype newRecords = 0;
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        if (!m_records.contains(it.key()))
            ++newRecords;
    }
    if (m_records.size() + newRecords > MaximumPersistedShortcuts) {
        setError(errorMessage, QStringLiteral("The per-user Portal shortcut limit was exceeded"));
        return false;
    }

    bool indexOk = true;
    const QStringList oldPaths = orderedSubPaths(&indexOk);
    if (!indexOk) {
        setError(errorMessage, QStringLiteral("Could not read the Portal shortcut index"));
        return false;
    }
    QSet<QString> indexedPaths(oldPaths.cbegin(), oldPaths.cend());
    for (auto it = desired.cbegin(); it != desired.cend(); ++it)
        indexedPaths.insert(it.key());
    if (indexedPaths.size() > MaximumPersistedShortcuts) {
        setError(errorMessage, QStringLiteral("The Portal shortcut index limit was exceeded"));
        return false;
    }

    QHash<QString, PortalShortcutInfo> oldRecords;
    QStringList newRecordIds;
    QStringList newPaths = oldPaths;
    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        const QString &id = it.key();
        const bool existed = m_records.contains(id);
        if (existed)
            oldRecords.insert(id, m_records.value(id));

        DConfig *config = m_configs.value(id);
        if (!config) {
            config = createConfig(id);
            if (config)
                m_configs.insert(id, config);
        }
        if (!config || !config->isValid()) {
            if (!existed) {
                m_configs.remove(id);
                if (config)
                    config->deleteLater();
            }
            setError(errorMessage, QStringLiteral("Could not open a Portal shortcut record"));
            compensate(oldPaths, oldRecords, newRecordIds);
            return false;
        }

        const QString storedAppId = config->value(QStringLiteral("appId")).toString();
        const QString storedShortcutId = config->value(QStringLiteral("shortcutId")).toString();
        if ((!storedAppId.isEmpty() && storedAppId != it->appId)
            || (!storedShortcutId.isEmpty() && storedShortcutId != it->shortcutId)) {
            qCritical() << "PortalShortcutStore: identity mismatch for subpath" << id;
            setError(errorMessage, QStringLiteral("The Portal shortcut record identity is corrupt"));
            compensate(oldPaths, oldRecords, newRecordIds);
            return false;
        }

        if (!existed)
            newRecordIds.append(id);

        QString writeError;
        if (!writeShortcut(config, *it, &writeError)) {
            setError(errorMessage, writeError);
            compensate(oldPaths, oldRecords, newRecordIds);
            return false;
        }
        if (!newPaths.contains(id))
            newPaths.append(id);
    }

    if (!writeSubPaths(newPaths)) {
        setError(errorMessage, QStringLiteral("Could not update the Portal shortcut index"));
        compensate(oldPaths, oldRecords, newRecordIds);
        return false;
    }

    for (auto it = desired.cbegin(); it != desired.cend(); ++it)
        m_records.insert(it.key(), it.value());
    return true;
}

QString PortalShortcutStore::internalId(const QString &appId, const QString &shortcutId)
{
    QByteArray identity = appId.toUtf8();
    identity.append('\0');
    identity.append(shortcutId.toUtf8());
    const QByteArray digest = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    return PortalPrefix + QString::fromLatin1(digest.left(32));
}

QString PortalShortcutStore::normalizeSubPath(const QString &raw)
{
    const QString trimmed = raw.trimmed();
    const QString normalized = trimmed.startsWith(QLatin1Char('/')) ? trimmed.mid(1) : trimmed;
    if (!normalized.startsWith(PortalPrefix)
        || normalized.size() != PortalPrefix.size() + 32
        || normalized.contains(QLatin1Char('/'))
        || normalized.contains(QLatin1Char('\\'))) {
        return {};
    }
    static const QRegularExpression idPattern(
            QStringLiteral("^org\\.deepin\\.dde\\.keybinding\\.shortcut\\.portal\\.[0-9a-f]{32}$"));
    return idPattern.match(normalized).hasMatch() ? normalized : QString();
}

bool PortalShortcutStore::validate(const PortalShortcutInfo &shortcut, QString *errorMessage)
{
    if (shortcut.appId.isEmpty() || shortcut.appId.size() > MaximumAppIdLength
        || containsUnsafeCharacter(shortcut.appId)
        || shortcut.shortcutId.isEmpty()
        || shortcut.shortcutId.size() > MaximumShortcutIdLength
        || containsUnsafeCharacter(shortcut.shortcutId)
        || shortcut.description.size() > MaximumDescriptionLength
        || containsUnsafeCharacter(shortcut.description)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Portal shortcut text fields are invalid");
        return false;
    }
    if (shortcut.id != internalId(shortcut.appId, shortcut.shortcutId)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The Portal shortcut internal ID is invalid");
        return false;
    }
    if (shortcut.hotkeys.size() > 1 || shortcut.preferredKeys.size() > 1) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Only one hotkey is supported per Portal action");
        return false;
    }
    for (const QStringList *hotkeys : {&shortcut.hotkeys, &shortcut.preferredKeys}) {
        if (!hotkeys->isEmpty() && !isValidStoredHotkey(hotkeys->constFirst())) {
            if (errorMessage)
                *errorMessage = QStringLiteral("A persisted Portal shortcut hotkey is invalid");
            return false;
        }
    }
    return true;
}

const QStringList &PortalShortcutStore::fieldNames()
{
    static const QStringList names = {
        QStringLiteral("appId"),
        QStringLiteral("shortcutId"),
        QStringLiteral("description"),
        QStringLiteral("hotkeys"),
        QStringLiteral("preferredKeys"),
        QStringLiteral("enabled"),
        QStringLiteral("category"),
        QStringLiteral("modifiable"),
    };
    return names;
}

QList<QPair<QString, QVariant>> PortalShortcutStore::fields(const PortalShortcutInfo &shortcut)
{
    return {
        {QStringLiteral("appId"), shortcut.appId},
        {QStringLiteral("shortcutId"), shortcut.shortcutId},
        {QStringLiteral("description"), shortcut.description},
        {QStringLiteral("hotkeys"), shortcut.hotkeys},
        {QStringLiteral("preferredKeys"), shortcut.preferredKeys},
        {QStringLiteral("enabled"), shortcut.enabled},
        {QStringLiteral("category"), QStringLiteral("Applications")},
        {QStringLiteral("modifiable"), true},
    };
}

QStringList PortalShortcutStore::orderedSubPaths(bool *ok) const
{
    QSettings settings(m_iniPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Config"));
    const QVariant value = settings.value(QStringLiteral("SubPaths"));
    settings.endGroup();

    QStringList paths = value.metaType() == QMetaType::fromType<QStringList>()
            ? value.toStringList()
            : value.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &path : paths)
        path = normalizeSubPath(path);
    paths.removeAll(QString());
    paths.removeDuplicates();
    if (ok)
        *ok = settings.status() == QSettings::NoError;
    return paths;
}

bool PortalShortcutStore::writeSubPaths(const QStringList &subPaths) const
{
    const QFileInfo info(m_iniPath);
    QDir directory(info.absolutePath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
        return false;

    QStringList normalized;
    for (const QString &path : subPaths) {
        const QString value = normalizeSubPath(path);
        if (!value.isEmpty() && !normalized.contains(value))
            normalized.append(value);
    }

    QSettings settings(m_iniPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Config"));
    settings.setValue(QStringLiteral("SubPaths"), normalized);
    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

DConfig *PortalShortcutStore::createConfig(const QString &subPath)
{
    const QString normalized = normalizeSubPath(subPath);
    if (normalized.isEmpty())
        return nullptr;
    return DConfig::create(AppId, ConfigName, QStringLiteral("/") + normalized, this);
}

bool PortalShortcutStore::readShortcut(const QString &subPath,
                                       DConfig *config,
                                       PortalShortcutInfo &shortcut,
                                       QString *errorMessage) const
{
    shortcut.id = normalizeSubPath(subPath);
    shortcut.appId = config->value(QStringLiteral("appId")).toString();
    shortcut.shortcutId = config->value(QStringLiteral("shortcutId")).toString();
    shortcut.description = config->value(QStringLiteral("description")).toString();
    shortcut.hotkeys = config->value(QStringLiteral("hotkeys")).toStringList();
    shortcut.preferredKeys = config->value(QStringLiteral("preferredKeys")).toStringList();
    shortcut.enabled = config->value(QStringLiteral("enabled")).toBool();
    shortcut.active = false;

    if (config->value(QStringLiteral("category")).toString() != QLatin1String("Applications")
        || !config->value(QStringLiteral("modifiable")).toBool()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Portal record has invalid fixed fields");
        return false;
    }
    return validate(shortcut, errorMessage);
}

bool PortalShortcutStore::writeShortcut(DConfig *config,
                                        const PortalShortcutInfo &shortcut,
                                        QString *errorMessage) const
{
    const auto values = fields(shortcut);
    for (const auto &field : values) {
        if (config->isReadOnly(field.first)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("A Portal shortcut field is read-only");
            return false;
        }
    }

    const QSignalBlocker blocker(config);
    for (const auto &field : values) {
        if (!valuesEqual(config->value(field.first), field.second))
            config->setValue(field.first, field.second);
    }
    for (const auto &field : values) {
        if (!valuesEqual(config->value(field.first), field.second)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Portal shortcut field verification failed");
            return false;
        }
    }
    return true;
}

bool PortalShortcutStore::resetShortcut(DConfig *config) const
{
    if (!config)
        return true;
    bool success = true;
    const QSignalBlocker blocker(config);
    for (const QString &field : fieldNames()) {
        if (config->isReadOnly(field)) {
            success = false;
            continue;
        }
        config->reset(field);
    }
    return success;
}

bool PortalShortcutStore::compensate(
        const QStringList &oldPaths,
        const QHash<QString, PortalShortcutInfo> &oldRecords,
        const QStringList &newRecordIds)
{
    bool success = writeSubPaths(oldPaths);
    for (auto it = oldRecords.cbegin(); it != oldRecords.cend(); ++it) {
        DConfig *config = m_configs.value(it.key());
        QString ignored;
        success = config && writeShortcut(config, it.value(), &ignored) && success;
    }
    for (const QString &id : newRecordIds) {
        DConfig *config = m_configs.take(id);
        success = resetShortcut(config) && success;
        if (config)
            config->deleteLater();
    }

    if (!success) {
        qCritical() << "PortalShortcutStore: persistence compensation failed; reloading state";
        QString ignored;
        load(&ignored);
    }
    return success;
}

void PortalShortcutStore::setError(QString *errorMessage, const QString &message) const
{
    if (errorMessage)
        *errorMessage = message;
}
