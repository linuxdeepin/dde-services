// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/portalshortcuttypes.h"

#include <QHash>
#include <QObject>
#include <QPointer>

#include <optional>

#include <DConfig>

DCORE_USE_NAMESPACE

class PortalShortcutStore : public QObject
{
    Q_OBJECT

public:
    static constexpr qsizetype MaximumPersistedShortcuts = 1000;
    static constexpr qsizetype MaximumShortcutsPerApplication = 100;

    explicit PortalShortcutStore(QObject *parent = nullptr);
    explicit PortalShortcutStore(const QString &iniPath, QObject *parent = nullptr);
    ~PortalShortcutStore() override = default;

    virtual bool load(QString *errorMessage = nullptr);
    virtual PortalShortcutInfoList shortcutsForApp(const QString &appId) const;
    virtual std::optional<PortalShortcutInfo> shortcut(const QString &appId,
                                                       const QString &shortcutId) const;

    // Inserts or updates the supplied records while leaving all other records
    // untouched. A failed batch performs one best-effort compensation. Callers
    // must reload before restoring runtime state, as compensation can fail too.
    virtual bool apply(const PortalShortcutInfoList &shortcuts,
                       QString *errorMessage = nullptr);

    static QString internalId(const QString &appId, const QString &shortcutId);
    static QString normalizeSubPath(const QString &raw);
    static bool validate(const PortalShortcutInfo &shortcut, QString *errorMessage = nullptr);

private:
    static const QStringList &fieldNames();
    static QList<QPair<QString, QVariant>> fields(const PortalShortcutInfo &shortcut);

    QStringList orderedSubPaths(bool *ok = nullptr) const;
    bool writeSubPaths(const QStringList &subPaths) const;
    DConfig *createConfig(const QString &subPath);
    bool readShortcut(const QString &subPath,
                      DConfig *config,
                      PortalShortcutInfo &shortcut,
                      QString *errorMessage) const;
    bool writeShortcut(DConfig *config,
                       const PortalShortcutInfo &shortcut,
                       QString *errorMessage) const;
    bool resetShortcut(DConfig *config) const;
    bool compensate(const QStringList &oldPaths,
                    const QHash<QString, PortalShortcutInfo> &oldRecords,
                    const QStringList &newRecordIds);
    void setError(QString *errorMessage, const QString &message) const;

    QString m_iniPath;
    QHash<QString, PortalShortcutInfo> m_records;
    QHash<QString, QPointer<DConfig>> m_configs;
};
