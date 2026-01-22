// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/shortcutconfig.h"

#include <QObject>
#include <QMap>
#include <QList>

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
    void resetConfig();
    void updateValue(const QString &id, const QString &key, const QVariant &value);
    void dumpConfigs();
    
    QList<KeyConfig> keys() const { return m_keys; }
    QList<GestureConfig> gestures() const { return m_gestures; }

signals:
    void keyConfigChanged(const KeyConfig &config);
    void gestureConfigChanged(const GestureConfig &config);
    void configRemoved(const QString &id);

    void keyConfigAdded(const KeyConfig &config);
    void gestureConfigAdded(const GestureConfig &config);

private:
    QSet<QString> discoverSubPaths();
    bool needLoad(const QString &subPath);
    void loadConfig(const QString &subPath, bool newOne = false);
    KeyConfig parseKeyConfig(DConfig *config);
    GestureConfig parseGestureConfig(DConfig *config);

    QList<KeyConfig> m_keys;
    QList<GestureConfig> m_gestures;
    QMap<QString, DConfig*> m_configs; // Map subPath -> config
    QMap<QString, QString> m_subPathToId; // Map subPath -> id
};
