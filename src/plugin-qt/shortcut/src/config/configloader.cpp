// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "configloader.h"

#include <algorithm>

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QTemporaryFile>
#include <QSettings>
#include <QSet>
#include <QRegularExpression>

#include <DConfig>

const QString APP_ID = "org.deepin.dde.keybinding";
const QString CONFIG_NAME_SHORTCUT = "org.deepin.shortcut";
const QString CONFIG_NAME_GESTURE = "org.deepin.gesture";
const QString CONFIG_SUBPATH_DIR = "/usr/share/deepin/org.deepin.dde.keybinding/";

DCORE_USE_NAMESPACE

static DConfig *createDConfig(const QString &name, const QString &subPath, QObject *parent)
{
    const QString normalized = CustomShortcutStore::normalizeSubPath(subPath);
    return DConfig::create(APP_ID, name, QStringLiteral("/") + normalized, parent);
}

ConfigLoader::ConfigLoader(QObject *parent)
    : QObject(parent)
{
}

void ConfigLoader::scanForConfigs()
{
    QSet<QString> foundSubPaths = discoverSubPaths();
    qInfo() << "ConfigLoader: Found subpaths:" << foundSubPaths;

    for (const QString &subPath : foundSubPaths) {
        if (!m_loadedSubPaths.contains(subPath)) {
            loadConfig(subPath);
        }
    }
}

void ConfigLoader::reload()
{
    qInfo() << "ConfigLoader reloading (Smart Diff INI-only)...";
    QSet<QString> currentSubPaths = discoverSubPaths();

    // Compare with existing configs (Remove Stale)
    QList<QString> existingSubPaths(m_loadedSubPaths.constBegin(), m_loadedSubPaths.constEnd());
    for (const QString &subPath : existingSubPaths) {
        if (!currentSubPaths.contains(subPath)) {
            qInfo() << "Config removed:" << subPath;
            m_loadedSubPaths.remove(subPath);

            m_keys.erase(std::remove_if(m_keys.begin(), m_keys.end(),
                                        [&](const KeyConfig &c) { return c.subPath == subPath; }),
                         m_keys.end());
            m_gestures.erase(std::remove_if(m_gestures.begin(), m_gestures.end(),
                                            [&](const GestureConfig &c) { return c.subPath == subPath; }),
                             m_gestures.end());

            if (m_configs.contains(subPath)) {
                DConfig *config = m_configs.take(subPath);
                if (config) config->deleteLater();
            }

            emit configRemoved(subPath);
        }
    }

    // Add New
    for (const QString &subPath : currentSubPaths) {
        if (!m_loadedSubPaths.contains(subPath)) {
            qInfo() << "Config added:" << subPath;
            loadConfig(subPath, true);
        }
    }

    // Content changes to existing entries are delivered via DConfig::valueChanged.
}

void ConfigLoader::resetConfig()
{
    // Reset key hotkeys to defaults. Only keys are touched — gestures have
    // no "hotkeys" field. Other key fields (enabled, keyEventFlags...) are
    // left alone, and non-modifiable keys (hardware keys, locked entries)
    // are skipped.
    for (const KeyConfig &key : m_keys) {
        if (!key.modifiable) continue;
        DConfig *config = m_configs.value(key.subPath);
        if (config) config->reset("hotkeys");
    }
}

bool ConfigLoader::updateValue(const QString &id, const QString &key, const QVariant &value)
{
    if (!m_configs.contains(id)) {
        qWarning() << "ConfigLoader: config not found or can not be changed:" << id << key << value;
        return false;
    }
    
    m_configs[id]->setValue(key, value);
    return true;
}

bool ConfigLoader::canUpdateValue(const QString &id) const
{
    return m_configs.contains(id);
}

QSet<QString> ConfigLoader::discoverSubPaths()
{
    QSet<QString> foundSubPaths;
    foundSubPaths.unite(scanIniSubPaths(CONFIG_SUBPATH_DIR));
    foundSubPaths.unite(m_customStore.discoverSubPaths());

    return foundSubPaths;
}

QSet<QString> ConfigLoader::scanIniSubPaths(const QString &dirPath)
{
    QSet<QString> foundSubPaths;

    QDir regDir(dirPath);
    if (!regDir.exists()) {
        return foundSubPaths;
    }
    
    // Scan INIs
    // QSettings does not support backslash line continuation, so we read
    // the raw file, join continuation lines, then feed to QSettings.
    static const QRegularExpression reContinuation(QStringLiteral("\\\\\\s*\\n"));
    QStringList iniFiles = regDir.entryList(QStringList() << "*.ini", QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &iniFile : iniFiles) {
        QString fullPath = regDir.absoluteFilePath(iniFile);
        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "ConfigLoader: Failed to open" << fullPath;
            continue;
        }
        QString content = QString::fromUtf8(file.readAll());
        content.replace(reContinuation, QString());

        // Write processed content to a temp file for QSettings
        // (QSettings does not support backslash line continuation natively).
        QTemporaryFile tmpFile;
        tmpFile.setAutoRemove(true);
        if (!tmpFile.open()) {
            qWarning() << "ConfigLoader: Failed to create temp file for" << fullPath;
            continue;
        }
        tmpFile.write(content.toUtf8());
        tmpFile.flush();
        tmpFile.close();

        QSettings settings(tmpFile.fileName(), QSettings::IniFormat);
        settings.beginGroup("Config");

        QVariant subPathsVar = settings.value("SubPaths");
        if (!subPathsVar.isValid()) {
            subPathsVar = settings.value("SubPath");
        }

        QStringList subPaths;
        if (subPathsVar.typeId() == QMetaType::QStringList) {
            subPaths = subPathsVar.toStringList();
        } else {
            subPaths = subPathsVar.toString().split(",", Qt::SkipEmptyParts);
        }

        settings.endGroup();

        qDebug() << "ConfigLoader: File:" << fullPath << "Parsed" << subPaths.size() << "subpaths";

        if (!subPaths.isEmpty()) {
            for (const QString &subPath : subPaths) {
                foundSubPaths.insert(subPath.trimmed());
            }
        }
    }

    return foundSubPaths;
}

bool ConfigLoader::saveCustomShortcut(const KeyConfig &config)
{
    const QString subPath = CustomShortcutStore::normalizeSubPath(config.subPath);
    DConfig *customDconfig = m_configs.value(subPath);
    const bool existingDConfig = customDconfig;
    if (!customDconfig) {
        customDconfig = m_customStore.createConfig(subPath, this);
        if (!customDconfig->isValid()) {
            qWarning() << "ConfigLoader: failed to create custom shortcut DConfig:" << subPath;
            delete customDconfig;
            return false;
        }
    }

    KeyConfig storedConfig = config;
    storedConfig.subPath = subPath;
    if (!m_customStore.save(storedConfig, customDconfig)) {
        if (!existingDConfig) {
            m_customStore.reset(customDconfig, subPath);
            customDconfig->deleteLater();
        }
        return false;
    }

    auto existing = std::find_if(m_keys.begin(), m_keys.end(),
                                 [&](const KeyConfig &item) { return item.subPath == subPath; });
    if (existing != m_keys.end())
        *existing = storedConfig;
    else
        m_keys.append(storedConfig);

    m_loadedSubPaths.insert(subPath);
    if (!m_configs.contains(subPath)) {
        connect(customDconfig, &DConfig::valueChanged, this, [this, subPath, customDconfig](const QString &key) {
            if (!customDconfig->isValid() || !m_configs.contains(subPath)) {
                qWarning() << "DConfig invalid or not found:" << subPath;
                return;
            }

            KeyConfig updatedConfig = parseKeyConfig(customDconfig);
            qDebug() << "DConfig value changed:" << subPath << key;
            auto existing = std::find_if(m_keys.begin(), m_keys.end(),
                                         [&](const KeyConfig &item) { return item.subPath == subPath; });
            if (existing != m_keys.end()) {
                *existing = updatedConfig;
            } else {
                m_keys.append(updatedConfig);
            }
            emit keyConfigChanged(updatedConfig);
        });
        m_configs.insert(subPath, customDconfig);
    }

    return true;
}

bool ConfigLoader::updateCustomShortcut(const KeyConfig &config)
{
    const QString subPath = CustomShortcutStore::normalizeSubPath(config.subPath);
    DConfig *customDconfig = m_configs.value(subPath);
    if (!customDconfig) {
        qWarning() << "ConfigLoader: custom shortcut config not found for update:" << subPath;
        return false;
    }

    KeyConfig storedConfig = config;
    storedConfig.subPath = subPath;
    if (!m_customStore.updateCustomShortcutFields(storedConfig, customDconfig))
        return false;

    auto existing = std::find_if(m_keys.begin(), m_keys.end(),
                                 [&](const KeyConfig &item) { return item.subPath == subPath; });
    if (existing != m_keys.end())
        *existing = storedConfig;
    else
        m_keys.append(storedConfig);

    m_loadedSubPaths.insert(subPath);
    return true;
}

bool ConfigLoader::removeCustomShortcut(const QString &subPath)
{
    const QString normalized = CustomShortcutStore::normalizeSubPath(subPath);
    DConfig *config = m_configs.take(normalized);
    if (!config) {
        config = m_customStore.createConfig(normalized, this);
        if (!config->isValid()) {
            qWarning() << "ConfigLoader: failed to create custom shortcut DConfig for reset:" << normalized;
            delete config;
            return false;
        }
    } else {
        disconnect(config, nullptr, this, nullptr);
    }

    m_customStore.reset(config, normalized);
    if (!m_customStore.removeSubPath(normalized)) {
        qWarning() << "ConfigLoader: failed to remove custom shortcut subPath after reset,"
                   << "leaving a stale ini entry:" << normalized;
    }

    m_loadedSubPaths.remove(normalized);
    m_keys.erase(std::remove_if(m_keys.begin(), m_keys.end(),
                                [&](const KeyConfig &config) { return config.subPath == normalized; }),
                 m_keys.end());

    if (config)
        config->deleteLater();

    return true;
}

void ConfigLoader::loadConfig(const QString &subPath, bool newOne)
{
    qDebug() << "Loading config from:" << subPath;
    // Check if it's a shortcut or gesture config
    bool isKey = subPath.contains(".shortcut");
    bool isGesture = subPath.contains(".gesture");

    if (!isKey && !isGesture) {
        qWarning() << "Skipping" << subPath << "(not shortcut or gesture)";
        return;
    }

    if (isGesture && qgetenv("XDG_SESSION_TYPE").toLower() != "wayland") {
        qWarning() << "Skipping" << subPath << "(not wayland session)";
        return;
    }

    DConfig *config = createDConfig(isKey ? CONFIG_NAME_SHORTCUT : CONFIG_NAME_GESTURE,
                                    subPath, this);
    if (!config->isValid()) {
        qWarning() << "Failed to create DConfig for" << subPath;
        delete config;
        return;
    }

    bool configCanNotChanged = false;
    if (isKey) {
        KeyConfig keyConfig = parseKeyConfig(config);
        if (!keyConfig.isValid()) {
            qWarning() << "Skipping invalid or disabled KeyConfig:" << subPath
                       << "(enabled=" << keyConfig.enabled
                       << ", appId set=" << !keyConfig.appId.isEmpty()
                       << ", displayName set=" << !keyConfig.displayName.isEmpty()
                       << ", hotkey count=" << keyConfig.hotkeys.size() << ")";
            m_loadedSubPaths.insert(subPath);
            config->deleteLater();
            return;
        }

        configCanNotChanged = !keyConfig.modifiable;
        qDebug() << "Parsed KeyConfig:" << keyConfig.appId << keyConfig.hotkeys << subPath;
        m_loadedSubPaths.insert(subPath);
        m_keys.append(keyConfig);

        if (newOne) {
            emit keyConfigAdded(keyConfig);
        }
    } else {
        GestureConfig gestureConfig = parseGestureConfig(config);
        if (!gestureConfig.isValid()) {
            qWarning() << "Skipping invalid or disabled GestureConfig:" << subPath
                       << "(enabled=" << gestureConfig.enabled
                       << ", appId set=" << !gestureConfig.appId.isEmpty()
                       << ", displayName set=" << !gestureConfig.displayName.isEmpty()
                       << ", gestureType=" << gestureConfig.gestureType
                       << ", fingerCount=" << gestureConfig.fingerCount << ")";
            m_loadedSubPaths.insert(subPath);
            config->deleteLater();
            return;
        }

        configCanNotChanged = !gestureConfig.modifiable;
        qDebug() << "Parsed GestureConfig:" << gestureConfig.appId << subPath;
        m_loadedSubPaths.insert(subPath);
        m_gestures.append(gestureConfig);

        if (newOne) {
            emit gestureConfigAdded(gestureConfig);
        }
    }

    if (configCanNotChanged) {
        config->deleteLater();
    } else {
        connect(config, &DConfig::valueChanged, this, [this, subPath, isKey, config](const QString &key) {
            if (!config->isValid() || !m_configs.contains(subPath)) {
                qWarning() << "DConfig invalid or not found:" << subPath;
                return;
            }

            qDebug() << "DConfig value changed:" << subPath << key;
            if (isKey) {
                KeyConfig updatedConfig = parseKeyConfig(config);
                auto existing = std::find_if(m_keys.begin(), m_keys.end(),
                                             [&](const KeyConfig &item) { return item.subPath == subPath; });
                if (existing != m_keys.end()) {
                    *existing = updatedConfig;
                } else {
                    m_keys.append(updatedConfig);
                }
                emit keyConfigChanged(updatedConfig);
            } else {
                GestureConfig updatedConfig = parseGestureConfig(config);
                auto existing = std::find_if(m_gestures.begin(), m_gestures.end(),
                                             [&](const GestureConfig &item) { return item.subPath == subPath; });
                if (existing != m_gestures.end()) {
                    *existing = updatedConfig;
                } else {
                    m_gestures.append(updatedConfig);
                }
                emit gestureConfigChanged(updatedConfig);
            }
        });

        m_configs.insert(subPath, config);
    }
}

KeyConfig ConfigLoader::parseKeyConfig(DConfig *config)
{
    KeyConfig keyConfig;
    keyConfig.subPath = CustomShortcutStore::normalizeSubPath(config->subpath());
    keyConfig.appId = config->value("appId").toString();
    keyConfig.displayName = config->value("displayName").toString();
    keyConfig.enabled = config->value("enabled").toBool();
    keyConfig.modifiable = config->value("modifiable").toBool();
    keyConfig.triggerType = config->value("triggerType").toInt();
    keyConfig.triggerValue = config->value("triggerValue").toStringList();
    keyConfig.category = config->value("category").toString();
    keyConfig.hotkeys = config->value("hotkeys").toStringList();

    if (config->keyList().contains("keyEventFlags")) {
        // Parse keyEventFlags, default to Release (0x2) if not specified
        keyConfig.keyEventFlags = config->value("keyEventFlags", KeyEventFlag::Release).toInt();
    }
    
    return keyConfig;
}

GestureConfig ConfigLoader::parseGestureConfig(DConfig *config)
{
    GestureConfig gestureConfig;
    gestureConfig.subPath = CustomShortcutStore::normalizeSubPath(config->subpath());
    gestureConfig.appId = config->value("appId").toString();
    gestureConfig.displayName = config->value("displayName").toString();
    gestureConfig.enabled = config->value("enabled").toBool();
    gestureConfig.modifiable = config->value("modifiable").toBool();
    gestureConfig.triggerType = config->value("triggerType").toInt();
    gestureConfig.triggerValue = config->value("triggerValue").toStringList();
    gestureConfig.category = config->value("category").toString();
    gestureConfig.gestureType = config->value("gestureType").toInt();
    gestureConfig.fingerCount = config->value("fingerCount").toInt();
    gestureConfig.direction = config->value("direction").toInt();
    return gestureConfig;
}

void ConfigLoader::dumpConfigs()
{
    qDebug() << "--- Registered Keybindings (from Config) --- keyEventFlags:1-press,2-release,4-autoRepeat---";
    for (const KeyConfig &config : m_keys) {
        qDebug().noquote() << QString("  ID: %1").arg(config.getId());
        qDebug().noquote() << QString("  Hotkeys: %1").arg(config.hotkeys.join(", "));
        qDebug().noquote() << QString("  Action:  %1").arg(config.triggerValue.join(" "));
        qDebug().noquote() << QString("  keyEventFlags: %1").arg(config.keyEventFlags);
        qDebug() << "";
    }

    if (!m_gestures.isEmpty()) {
        qDebug() << "--- Registered Gestures (from Config) ---";
        for (const GestureConfig &config : m_gestures) {
            QString directionStr;
            switch (config.direction) {
            case 1: directionStr = "Down"; break;
            case 2: directionStr = "Left"; break;
            case 3: directionStr = "Up"; break;
            case 4: directionStr = "Right"; break;
            default: directionStr = "None"; break;
            }

            qDebug().noquote() << QString("  ID: %1").arg(config.getId());
            qDebug().noquote() << QString("  Type:    %1 (%2 fingers, %3)")
                                 .arg(config.gestureType == 1 ? "Swipe" : "Hold")
                                 .arg(config.fingerCount)
                                 .arg(directionStr);
            qDebug().noquote() << QString("  Action:  %1").arg(config.triggerValue.join(" "));
            qDebug() << "";
        }
    }
}
