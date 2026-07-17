// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/**
 * Logical/physical key equivalence used by the shortcut service.
 *
 * Why this exists:
 * - Wayland binds Qt key strings via the compositor; Treeland resolves main
 *   Delete and keypad Delete from the same Qt logical binding. No expand there.
 * - X11 grabs concrete keycodes for each keysym. XK_Delete and XK_KP_Delete are
 *   different keys, so the X11 backend resolves and grabs physical aliases.
 *
 * Shared storage/conflict path uses canonicalize() so KP_Delete inputs collapse
 * to the logical name (Delete). Only the X11 backend calls expandX11Candidates().
 *
 * To support another pair later, add one row to the table in
 * physicalkeyalias.cpp. Call sites stay unchanged.
 */
namespace PhysicalKeyAlias {

struct Group {
    QString canonical;   // Logical / persisted keysym name, e.g. "Delete"
    QStringList aliases; // X11 physical siblings that may be absent from a keymap
};

enum class X11CandidateRequirement {
    Required,
    IfAvailable,
};

// One physical registration candidate for an X11 logical shortcut.
struct X11Candidate {
    QString hotkey; // Full XKB (or Qt) hotkey string with key name substituted
    X11CandidateRequirement requirement = X11CandidateRequirement::Required;
};

// Collapse known alias key names to their canonical name (storage / conflict).
// Safe on both X11 and Wayland code paths.
QString canonicalize(const QString &hotkey);

// Expand one hotkey into a required canonical candidate plus physical aliases
// that may be absent from the active X11 keymap. Once an alias resolves to a
// concrete keycode, grabbing it is part of the atomic registration operation.
// Intended only for X11 keycode registration (not Wayland bind_key).
QList<X11Candidate> expandX11Candidates(const QString &hotkey);

} // namespace PhysicalKeyAlias
