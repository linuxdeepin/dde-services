// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "backend/abstractkeyhandler.h"
#include "treelandshortcutwrapper.h"

#include <QMap>
#include <QWaylandClientExtension>

// Forward declarations for Wayland types
struct org_kde_kwin_keystate;

class KeyStateManager : public QObject
{
    Q_OBJECT
public:
    explicit KeyStateManager(QObject *parent = nullptr);
    ~KeyStateManager() override;

    bool initialize();
    bool getCapsLockState() const { return m_capsLockState; }
    bool getNumLockState() const { return m_numLockState; }
    bool isReady() const { return m_ready; }

    static void handleStateChanged(void *data, struct org_kde_kwin_keystate *keystate,
                                   uint32_t key, uint32_t state);

signals:
    void capsLockStateChanged(bool locked);
    void numLockStateChanged(bool locked);
    void ready(); // Emitted when state is first received (only once)

private:
    struct org_kde_kwin_keystate *m_keystate = nullptr;
    bool m_capsLockState = false;
    bool m_numLockState = false;
    bool m_ready = false; // Flag indicating whether state has been received
};

class WaylandKeyHandler : public AbstractKeyHandler
{
    Q_OBJECT
public:
    explicit WaylandKeyHandler(TreelandShortcutWrapper *wrapper, QObject *parent = nullptr);
    ~WaylandKeyHandler() override;

    bool registerKey(const KeyConfig &config) override;
    bool unregisterKey(const QString &appId) override;
    bool commit() override;

    bool getCapsLockState() const override;
    bool getNumLockState() const override;
    void setCapsLockState(bool on) override;
    void setNumLockState(bool on) override;

private slots:
    void onActivated(const QString &name, uint32_t flags);

private:
    TreelandShortcutWrapper *m_wrapper = nullptr;
    KeyStateManager *m_keyStateManager = nullptr;
    
    QMap<QString, QString> m_nameToId; // Map binding name -> id
    QMap<QString, QStringList> m_idToNames; // Map id -> list of binding names
};
