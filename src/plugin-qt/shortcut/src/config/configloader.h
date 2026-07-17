// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "config/customshortcutstore.h"
#include "core/shortcutconfig.h"

#include <QObject>
#include <QMap>
#include <QList>
#include <QSet>

#include <DConfig>

DCORE_USE_NAMESPACE

struct LoadedConfigs {
    QList<KeyConfig> keys;
    QList<GestureConfig> gestures;
};

class ConfigLoader : public QObject
{
    Q_OBJECT
public:
    explicit ConfigLoader(QObject *parent = nullptr);
    
    void scanForConfigs();
    void reload();
    QStringList resettableHotkeyIds() const;
    void resetHotkeys(const QStringList &ids);
    bool reloadKeyConfig(const QString &id, KeyConfig *result = nullptr);
    bool updateValue(const QString &id, const QString &key, const QVariant &value);
    bool canUpdateValue(const QString &id) const;
    void dumpConfigs();

    // Custom shortcut persistence (user-level DConfig + INI)
    bool saveCustomShortcut(const KeyConfig &config);
    bool updateCustomShortcut(const KeyConfig &config);
    bool removeCustomShortcut(const QString &subPath);

    const QList<KeyConfig> &keys() const { return m_keys; }
    const QList<GestureConfig> &gestures() const { return m_gestures; }
    const QStringList &customShortcutSubPaths() const { return m_customShortcutSubPaths; }

signals:
    void keyConfigChanged(const KeyConfig &config);
    void gestureConfigChanged(const GestureConfig &config);
    void configRemoved(const QString &id);

    void keyConfigAdded(const KeyConfig &config);
    void gestureConfigAdded(const GestureConfig &config);

private:
    QSet<QString> discoverSubPaths();
    QSet<QString> scanIniSubPaths(const QString &dirPath);
    void loadConfig(const QString &subPath, bool newOne = false);
    KeyConfig parseKeyConfig(DConfig *config);
    GestureConfig parseGestureConfig(DConfig *config);

    QList<KeyConfig> m_keys;
    QList<GestureConfig> m_gestures;
    QMap<QString, DConfig*> m_configs; // subPath(id) -> config, key has no leading /
    QSet<QString> m_loadedSubPaths; // Track all loaded subPaths
    QStringList m_customShortcutSubPaths; // Persisted order for legacy custom shortcuts

    CustomShortcutStore m_customStore;
};
