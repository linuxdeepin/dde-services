// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "physicalkeyalias.h"

#include <algorithm>

namespace PhysicalKeyAlias {

namespace {

struct NameMatch {
    QString name;
    QString canonical;
    bool isCanonical = false;
};

const QList<Group> &groups()
{
    // X11-oriented physical siblings of a logical key.
    // Wayland does not consume this table for registration.
    static const QList<Group> table{
        {QStringLiteral("Delete"), {QStringLiteral("KP_Delete")}},
    };
    return table;
}

const QList<NameMatch> &sortedNames()
{
    static const QList<NameMatch> names = [] {
        QList<NameMatch> list;
        for (const Group &group : groups()) {
            list.append({group.canonical, group.canonical, true});
            for (const QString &alias : group.aliases)
                list.append({alias, group.canonical, false});
        }
        std::sort(list.begin(), list.end(), [](const NameMatch &left, const NameMatch &right) {
            if (left.name.size() != right.name.size())
                return left.name.size() > right.name.size();
            return left.name < right.name;
        });
        return list;
    }();
    return names;
}

bool isKeyNameBoundary(const QString &hotkey, int nameStart)
{
    if (nameStart <= 0)
        return true;
    const QChar prev = hotkey.at(nameStart - 1);
    // XKB: <Control>Delete ; Qt PortableText: Ctrl+Delete
    return prev == QLatin1Char('>') || prev == QLatin1Char('+');
}

const NameMatch *matchKeyName(const QString &hotkey)
{
    for (const NameMatch &entry : sortedNames()) {
        if (!hotkey.endsWith(entry.name))
            continue;
        const int start = hotkey.size() - entry.name.size();
        if (!isKeyNameBoundary(hotkey, start))
            continue;
        return &entry;
    }
    return nullptr;
}

// Replace only a trailing key name (with boundary), longest match first.
bool replaceTrailingKeyName(QString &hotkey, const QString &from, const QString &to)
{
    if (!hotkey.endsWith(from))
        return false;
    const int start = hotkey.size() - from.size();
    if (!isKeyNameBoundary(hotkey, start))
        return false;
    hotkey = hotkey.left(start) + to;
    return true;
}

} // namespace

QString canonicalize(const QString &hotkey)
{
    if (hotkey.isEmpty())
        return hotkey;

    QString result = hotkey;
    // Prefer longer alias names (KP_Delete before any shorter sibling).
    for (const NameMatch &entry : sortedNames()) {
        if (entry.isCanonical)
            continue;
        if (replaceTrailingKeyName(result, entry.name, entry.canonical))
            break;
    }
    return result;
}

QList<X11Candidate> expandX11Candidates(const QString &hotkey)
{
    const NameMatch *matched = matchKeyName(hotkey);
    if (!matched)
        return {{hotkey, X11CandidateRequirement::Required}};

    const QString prefix = hotkey.left(hotkey.size() - matched->name.size());
    QList<X11Candidate> candidates;
    candidates.append({prefix + matched->canonical, X11CandidateRequirement::Required});

    for (const Group &group : groups()) {
        if (group.canonical != matched->canonical)
            continue;
        for (const QString &alias : group.aliases) {
            const QString aliasHotkey = prefix + alias;
            if (aliasHotkey == candidates.constFirst().hotkey)
                continue;
            candidates.append({aliasHotkey, X11CandidateRequirement::IfAvailable});
        }
        break;
    }
    return candidates;
}

} // namespace PhysicalKeyAlias
