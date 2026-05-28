// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "backend/abstractkeyhandler.h"
#include "treelandshortcutwrapper.h"

#include <QMap>
#include <QWaylandClientExtension>

class WaylandKeyHandler : public AbstractKeyHandler
{
    Q_OBJECT
public:
    explicit WaylandKeyHandler(TreelandShortcutWrapper *wrapper, QObject *parent = nullptr);
    ~WaylandKeyHandler() override;

    bool registerKey(const KeyConfig &config) override;
    bool unregisterKey(const QString &appId) override;
    bool commit() override;
    bool commitSync() override;

    // CapsLock / NumLock state query and set are no-ops on Wayland —
    // reading / writing the modifier-lock state requires either privileged
    // protocols (e.g. org_kde_kwin_keystate, which we no longer depend on)
    // or fake-input emulation, both unavailable to a regular Wayland client
    // under Treeland. Users interact with the physical keys directly; the
    // shortcut service does not need to mirror those states.
    bool getCapsLockState() const override;
    bool getNumLockState() const override;
    void setCapsLockState(bool on) override;
    void setNumLockState(bool on) override;

private slots:
    void onActivated(const QString &name, uint32_t flags);

private:
    TreelandShortcutWrapper *m_wrapper = nullptr;

    QMap<QString, QString> m_nameToId; // Map binding name -> id
    QMap<QString, QStringList> m_idToNames; // Map id -> list of binding names
};
