// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "customshortcutstore.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVariant>

namespace {

const QString APP_ID = "org.deepin.dde.keybinding";
const QString CONFIG_NAME_SHORTCUT = "org.deepin.shortcut";
const QString CUSTOM_CONFIG_SUBPATH_DIR = "deepin/dde-services/shortcut";
const QString CUSTOM_CONFIG_INI = "custom-shortcuts.ini";

const QStringList CustomShortcutKeys = {
    QStringLiteral("appId"),
    QStringLiteral("displayName"),
    QStringLiteral("displayOrder"),
    QStringLiteral("enabled"),
    QStringLiteral("modifiable"),
    QStringLiteral("triggerType"),
    QStringLiteral("triggerValue"),
    QStringLiteral("category"),
    QStringLiteral("hotkeys")
};

bool configValuesEqual(const QVariant &actual, const QVariant &expected)
{
    if (expected.metaType().id() == QMetaType::QStringList)
        return actual.toStringList() == expected.toStringList();

    return actual == expected;
}

QList<QPair<QString, QVariant>> shortcutFields(const KeyConfig &config)
{
    return {
        {QStringLiteral("appId"), config.appId},
        {QStringLiteral("displayName"), config.displayName},
        {QStringLiteral("displayOrder"), config.displayOrder},
        {QStringLiteral("enabled"), config.enabled},
        {QStringLiteral("modifiable"), config.modifiable},
        {QStringLiteral("triggerType"), config.triggerType},
        {QStringLiteral("triggerValue"), config.triggerValue},
        {QStringLiteral("category"), config.category},
        {QStringLiteral("hotkeys"), config.hotkeys},
    };
}

bool writeShortcutFields(DConfig *configObject, const KeyConfig &config)
{
    const QList<QPair<QString, QVariant>> fields = shortcutFields(config);
    for (const auto &field : fields) {
        if (configObject->isReadOnly(field.first)) {
            qWarning() << "CustomShortcutStore: read-only field:" << field.first
                       << config.subPath;
            return false;
        }
    }

    const QSignalBlocker blocker(configObject);
    for (const auto &field : fields) {
        if (!configValuesEqual(configObject->value(field.first), field.second))
            configObject->setValue(field.first, field.second);
    }
    for (const auto &field : fields) {
        if (!configValuesEqual(configObject->value(field.first), field.second)) {
            qWarning() << "CustomShortcutStore: field verification failed:"
                       << field.first << config.subPath;
            return false;
        }
    }
    return true;
}

} // namespace

CustomShortcutStore::CustomShortcutStore()
    : m_iniPath(QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                        .absoluteFilePath(CUSTOM_CONFIG_SUBPATH_DIR + QLatin1Char('/') + CUSTOM_CONFIG_INI))
{
}

DConfig *CustomShortcutStore::createConfig(const QString &subPath, QObject *parent) const
{
    const QString normalized = normalizeSubPath(subPath);
    if (normalized.isEmpty()) {
        qWarning() << "CustomShortcutStore: invalid custom shortcut subPath:" << subPath;
        return nullptr;
    }
    return DConfig::create(APP_ID, CONFIG_NAME_SHORTCUT, QStringLiteral("/") + normalized, parent);
}

bool CustomShortcutStore::save(const KeyConfig &config, DConfig *configObject) const
{
    if (!configObject) {
        qWarning() << "CustomShortcutStore: missing DConfig object for save:" << config.subPath;
        return false;
    }

    const QString subPath = normalizeSubPath(config.subPath);
    qDebug() << "CustomShortcutStore: saving custom shortcut"
             << "subPath:" << subPath
             << "appId:" << config.appId
             << "displayName:" << config.displayName
             << "enabled:" << config.enabled
             << "modifiable:" << config.modifiable
             << "triggerType:" << config.triggerType
             << "argument count:" << config.triggerValue.size()
             << "category:" << config.category
             << "hotkeys:" << config.hotkeys;

    if (!writeShortcutFields(configObject, config))
        return false;

    QStringList paths = orderedSubPaths();
    if (!paths.contains(subPath))
        paths.append(subPath);
    return writeSubPaths(paths);
}

bool CustomShortcutStore::updateCustomShortcutFields(const KeyConfig &config, DConfig *configObject) const
{
    if (!configObject) {
        qWarning() << "CustomShortcutStore: missing DConfig object for update:" << config.subPath;
        return false;
    }

    const QString subPath = normalizeSubPath(config.subPath);
    qDebug() << "CustomShortcutStore: updating custom shortcut fields"
             << "subPath:" << subPath
             << "displayName:" << config.displayName
             << "argument count:" << config.triggerValue.size()
             << "hotkeys:" << config.hotkeys;

    return writeShortcutFields(configObject, config);
}

bool CustomShortcutStore::removeSubPath(const QString &subPath) const
{
    QStringList paths = orderedSubPaths();
    paths.removeAll(normalizeSubPath(subPath));
    return writeSubPaths(paths);
}

void CustomShortcutStore::reset(DConfig *configObject, const QString &subPath) const
{
    if (!configObject)
        return;

    const QString normalized = normalizeSubPath(subPath);
    qDebug() << "CustomShortcutStore: resetting custom shortcut DConfig:" << normalized;

    QStringList resetKeys = configObject->keyList();
    resetKeys.append(CustomShortcutKeys);
    resetKeys.removeDuplicates();

    const QSignalBlocker blocker(configObject);
    for (const QString &key : resetKeys) {
        if (!configObject->isReadOnly(key))
            configObject->reset(key);
    }
}

QString CustomShortcutStore::normalizeSubPath(const QString &raw)
{
    const QString trimmed = raw.trimmed();
    const QString normalized = trimmed.startsWith(QLatin1Char('/')) ? trimmed.mid(1) : trimmed;
    if (normalized.isEmpty()
        || normalized.contains(QStringLiteral(".."))
        || normalized.contains(QLatin1Char('/'))
        || normalized.contains(QLatin1Char('\\'))
        || normalized.contains(QChar::Null)) {
        qWarning() << "CustomShortcutStore: rejected unsafe subPath:" << raw;
        return QString();
    }
    return normalized;
}

QStringList CustomShortcutStore::orderedSubPaths() const
{
    QSettings settings(m_iniPath, QSettings::IniFormat);
    settings.beginGroup("Config");
    const QVariant subPathsVar = settings.value("SubPaths");
    QStringList paths;
    if (subPathsVar.typeId() == QMetaType::QStringList) {
        paths = subPathsVar.toStringList();
    } else {
        paths = subPathsVar.toString().split(",", Qt::SkipEmptyParts);
    }
    settings.endGroup();

    for (QString &subPath : paths)
        subPath = normalizeSubPath(subPath);
    paths.removeAll(QString());
    paths.removeDuplicates();
    return paths;
}

bool CustomShortcutStore::writeSubPaths(const QStringList &subPaths) const
{
    const QFileInfo info(m_iniPath);
    QDir dir(info.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qWarning() << "CustomShortcutStore: failed to create custom shortcut ini dir:" << info.absolutePath();
        return false;
    }

    QStringList normalized;
    normalized.reserve(subPaths.size());
    for (const QString &subPath : subPaths) {
        const QString value = normalizeSubPath(subPath);
        if (!value.isEmpty() && !normalized.contains(value))
            normalized.append(value);
    }
    QSettings settings(m_iniPath, QSettings::IniFormat);
    settings.beginGroup("Config");
    settings.setValue("SubPaths", normalized);
    settings.endGroup();
    settings.sync();

    if (settings.status() != QSettings::NoError) {
        qWarning() << "CustomShortcutStore: failed to write custom shortcut ini:" << m_iniPath;
        return false;
    }
    return true;
}
