// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/shortcutconfig.h"

#include <QString>
#include <QStringList>

#include <DConfig>

DCORE_USE_NAMESPACE

class QObject;

class CustomShortcutStore
{
public:
    CustomShortcutStore();

    QStringList orderedSubPaths() const;
    DConfig *createConfig(const QString &subPath, QObject *parent) const;

    bool save(const KeyConfig &config, DConfig *configObject) const;
    bool updateCustomShortcutFields(const KeyConfig &config, DConfig *configObject) const;
    bool removeSubPath(const QString &subPath) const;
    void reset(DConfig *configObject, const QString &subPath) const;

    static QString normalizeSubPath(const QString &raw);

private:
    bool writeSubPaths(const QStringList &subPaths) const;

    QString m_iniPath;
};
