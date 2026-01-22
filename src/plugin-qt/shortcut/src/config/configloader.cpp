// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "configloader.h"
#include "core/shortcutconfig.h"

#include <QDir>
#include <QDebug>
#include <QSettings>
#include <QSet>
#include <dconfig.h>
#include <qlogging.h>
#include <qobject.h>

const QString APP_ID = "org.deepin.dde.keybinding";
const QString CONFIG_NAME = "org.deepin.shortcut";
const QString CONFIG_SUBPATH_DIR = "/usr/share/deepin/org.deepin.dde.keybinding/";

DCORE_USE_NAMESPACE

ConfigLoader::ConfigLoader(QObject *parent)
    : QObject(parent)
{
}

void ConfigLoader::scanForConfigs()
{
    QSet<QString> foundSubPaths = discoverSubPaths();
    qInfo() << "ConfigLoader: Found subpaths:" << foundSubPaths;

    // Process Found SubPaths
    for (const QString &subPath : foundSubPaths) {
        if (!m_subPathToId.contains(subPath)) {
            loadConfig(subPath);
        } else {
            // has conflict
            qWarning() << "ConfigLoader: SubPath conflict:" << subPath;
        }
    }
}

void ConfigLoader::reload()
{
    qInfo() << "ConfigLoader reloading (Smart Diff INI-only)...";
    QSet<QString> currentSubPaths = discoverSubPaths();
    
    // Compare with existing configs (Remove Stale)
    QList<QString> existingSubPaths = m_subPathToId.keys();
    for (const QString &subPath : existingSubPaths) {
        if (!currentSubPaths.contains(subPath)) {
            // Removed
            qInfo() << "Config removed:" << subPath;
            if (m_subPathToId.contains(subPath)) {
                QString id = m_subPathToId.take(subPath);
                emit configRemoved(id);
            }
            
            // Delete config
            if (m_configs.contains(subPath)) {
                DConfig *config = m_configs.take(subPath);
                if (config) config->deleteLater();
            }
        }
    }
    
    // Add New
    for (const QString &subPath : currentSubPaths) {
        if (!m_subPathToId.contains(subPath)) {
            qInfo() << "Config added:" << subPath;
            loadConfig(subPath, true);
        }
    }
}

void ConfigLoader::resetConfig()
{
    QList<DConfig *> allConfigs = m_configs.values();
    for (auto *config : allConfigs) {
        // reset hotkeys, has valueChanged signal for dconfig
        config->reset("hotkeys");
    }
}

void ConfigLoader::updateValue(const QString &id, const QString &key, const QVariant &value)
{
    if (m_configs.contains(id)) {
        m_configs[id]->setValue(key, value);
    } else {
        qWarning() << "ConfigLoader: Update value failed, config not found or can not be changed:" << id << key << value;
    }
}

QSet<QString> ConfigLoader::discoverSubPaths()
{
    // Accumulate all found subpaths
    QSet<QString> foundSubPaths;

    QDir regDir(CONFIG_SUBPATH_DIR);
    if (!regDir.exists()) {
        return foundSubPaths;
    }
    
    // Scan INIs
    QStringList iniFiles = regDir.entryList(QStringList() << "*.ini", QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &iniFile : iniFiles) {
        QString fullPath = CONFIG_SUBPATH_DIR + iniFile;
        QSettings settings(fullPath, QSettings::IniFormat);
        settings.beginGroup("Config");
        
        QVariant subPathsVar = settings.value("SubPaths");
        if (!subPathsVar.isValid()) {
            subPathsVar = settings.value("SubPath");
        }
        
        QString subPathsStr;
        if (subPathsVar.typeId() == QMetaType::QStringList) {
            subPathsStr = subPathsVar.toStringList().join(",");
        } else {
            subPathsStr = subPathsVar.toString();
        }
        
        settings.endGroup();

        qInfo() << "ConfigLoader: File:" << fullPath << "Status:" << settings.status() << "Raw Value:" << subPathsStr;

        if (!subPathsStr.isEmpty()) {
            // Use comma as separator
            QStringList subPaths = subPathsStr.split(",", Qt::SkipEmptyParts);
            qInfo() << "ConfigLoader: Parsed subpaths:" << subPaths;
            for (const QString &subPath : subPaths) {
                foundSubPaths.insert(subPath.trimmed());
            }
        }
    }

    return foundSubPaths;
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

    DConfig *config = DConfig::create(APP_ID, CONFIG_NAME, "/" + subPath, this);
    if (!config->isValid()) {
        qWarning() << "Failed to create DConfig for" << subPath;
        delete config;
        return;
    }

    bool configCanNotChanged = false;
    if (isKey) {
        KeyConfig keyConfig = parseKeyConfig(config);
        if (!keyConfig.isValid()) {
            qWarning() << "Failed to parse KeyConfig:" << subPath;
            config->deleteLater();
            return;
        }

        configCanNotChanged = keyConfig.category == Category::System && !keyConfig.modifiable;
        qDebug() << "Parsed KeyConfig:" << keyConfig.appId << keyConfig.hotkeys << subPath;
        m_subPathToId.insert(subPath, keyConfig.getId());

        if (newOne) {
            emit keyConfigAdded(keyConfig);
        } else {
            m_keys.append(keyConfig);
        }
    } else {
        GestureConfig gestureConfig = parseGestureConfig(config);
        if (!gestureConfig.isValid()) {
            qWarning() << "Failed to parse GestureConfig:" << subPath;
            config->deleteLater();
            return;
        }

        configCanNotChanged = gestureConfig.category == Category::System && !gestureConfig.modifiable;
        qDebug() << "Parsed GestureConfig:" << gestureConfig.appId << subPath;
        m_subPathToId.insert(subPath, gestureConfig.getId());

        if (newOne) {
            emit gestureConfigAdded(gestureConfig);
        } else {
            m_gestures.append(gestureConfig);
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
                emit keyConfigChanged(updatedConfig);
            } else {
                GestureConfig updatedConfig = parseGestureConfig(config);
                emit gestureConfigChanged(updatedConfig);
            }
        });

        m_configs.insert(subPath, config);
    }
}

KeyConfig ConfigLoader::parseKeyConfig(DConfig *config)
{
    KeyConfig keyConfig;
    keyConfig.subPath = config->subpath();
    keyConfig.appId = config->value("appId").toString();
    keyConfig.displayName = config->value("displayName").toString();
    keyConfig.enabled = config->value("enabled").toBool();
    keyConfig.modifiable = config->value("modifiable").toBool();
    keyConfig.triggerType = config->value("triggerType").toInt();
    keyConfig.triggerValue = config->value("triggerValue").toStringList();
    keyConfig.category = config->value("category").toInt(); 
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
    gestureConfig.subPath = config->subpath();
    gestureConfig.appId = config->value("appId").toString();
    gestureConfig.displayName = config->value("displayName").toString();
    gestureConfig.enabled = config->value("enabled").toBool();
    gestureConfig.modifiable = config->value("modifiable").toBool();
    gestureConfig.triggerType = config->value("triggerType").toInt();
    gestureConfig.triggerValue = config->value("triggerValue").toStringList();
    gestureConfig.category = config->value("category").toInt();
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
