// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "shortcutconfig.h"

#include <QObject>
#include <QVariant>
#include <QDBusContext>
#include <QDBusArgument>
#include <QDBusMetaType>


class ConfigLoader;
class ActionExecutor;
class AbstractKeyHandler;
class SpecialKeyHandler;
class TranslationManager;

struct ShortcutInfo {
    QString id;
    QString displayName;
    int category;
    QStringList hotkeys;
    QString localLanguageName;
};
Q_DECLARE_METATYPE(ShortcutInfo)

class KeybindingManager : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.Keybinding1")
    
    // Lock key state properties (0=off, 1=on)
    Q_PROPERTY(uint NumLockState READ GetNumLockState WRITE SetNumLockState NOTIFY NumLockStateChanged)
    Q_PROPERTY(uint CapsLockState READ GetCapsLockState WRITE SetCapsLockState NOTIFY CapsLockStateChanged)

public:
    explicit KeybindingManager(ConfigLoader *loader, ActionExecutor *executor, 
                               TranslationManager *translationManager, 
                               AbstractKeyHandler *keyHandler,
                               QObject *parent = nullptr);
    ~KeybindingManager() override;

    /**
     * @brief Register all shortcuts
     * @param autoCommit Whether to auto-commit (Wayland), pass false when coordinated by ShortcutManager
     */
    void registerAllShortcuts();

    /**
     * @brief Clear internal state (called when protocol disconnects)
     */
    void clearState();

public slots:
    // DBus Methods
    Q_SCRIPTABLE QList<ShortcutInfo> ListAllShortcuts();
    Q_SCRIPTABLE QList<ShortcutInfo> ListShortcutsByApp(const QString &appId);
    Q_SCRIPTABLE QList<ShortcutInfo> ListShortcutsByCategory(int category);

    Q_SCRIPTABLE ShortcutInfo GetShortcut(const QString &id);
    Q_SCRIPTABLE ShortcutInfo LookupConflictShortcut(const QString &hotkey);
    
    Q_SCRIPTABLE QList<ShortcutInfo> SearchShortcuts(const QString &keyword);
    Q_SCRIPTABLE bool ModifyHotkeys(const QString &id, const QStringList &newHotkeys);
    Q_SCRIPTABLE bool Disable(const QString &id);
    Q_SCRIPTABLE void ReloadConfigs();
    Q_SCRIPTABLE void Reset();
    
    // Lock key state methods (0=off, 1=on)
    Q_SCRIPTABLE uint GetNumLockState() const;
    Q_SCRIPTABLE uint GetCapsLockState() const;
    Q_SCRIPTABLE void SetNumLockState(uint state);
    Q_SCRIPTABLE void SetCapsLockState(uint state);

signals:
    // DBus Signals
    Q_SCRIPTABLE void ShortcutChanged(const QString &id, const ShortcutInfo &info);
    Q_SCRIPTABLE void ShortcutActivated(const QString &id, const QStringList &triggerValue);
    Q_SCRIPTABLE void ShortcutRemoved(const QString &id);
    
    // Lock key state signals (0=off, 1=on)
    Q_SCRIPTABLE void NumLockStateChanged(uint state);
    Q_SCRIPTABLE void CapsLockStateChanged(uint state);

private slots:
    void onKeyConfigChanged(const KeyConfig &config);
    void onConfigRemoved(const QString &id);
    void onKeyActivated(const QString &shortcutId);
    ShortcutInfo toShortcutInfo(const KeyConfig &config);

private:
    bool registerShortcut(const KeyConfig &config);
    QString checkConflictForConfig(const KeyConfig &config, const QString &excludeId = QString());


    ConfigLoader *m_loader;
    AbstractKeyHandler *m_keyHandler;
    SpecialKeyHandler *m_specialKeyHandler;
    ActionExecutor *m_executor;
    TranslationManager *m_translationManager;

    // id(shortcutId) -> KeyConfig
    QMap<QString, KeyConfig> m_keyConfigsMap;
};

Q_DECLARE_METATYPE(QList<ShortcutInfo>)

inline QDBusArgument &operator<<(QDBusArgument &argument, const ShortcutInfo &info) {
    argument.beginStructure();
    argument << info.id << info.displayName << info.category << info.hotkeys << info.localLanguageName;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, ShortcutInfo &info) {
    argument.beginStructure();
    argument >> info.id >> info.displayName >> info.category >> info.hotkeys >> info.localLanguageName;
    argument.endStructure();
    return argument;
}


