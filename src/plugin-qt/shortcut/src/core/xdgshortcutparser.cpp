// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "xdgshortcutparser.h"

#include "qkeysequenceconverter.h"

#include <QKeySequence>
#include <QRegularExpression>
#include <QSet>

#include <xkbcommon/xkbcommon.h>

namespace {

const QSet<QString> &allowedModifiers()
{
    static const QSet<QString> modifiers = {
        QStringLiteral("CTRL"),
        QStringLiteral("ALT"),
        QStringLiteral("SHIFT"),
        QStringLiteral("NUM"),
        QStringLiteral("LOGO"),
    };
    return modifiers;
}

QString qtModifierName(const QString &modifier)
{
    if (modifier == QLatin1String("CTRL"))
        return QStringLiteral("Ctrl");
    if (modifier == QLatin1String("ALT"))
        return QStringLiteral("Alt");
    if (modifier == QLatin1String("SHIFT"))
        return QStringLiteral("Shift");
    if (modifier == QLatin1String("LOGO"))
        return QStringLiteral("Meta");
    return {};
}

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage)
        *errorMessage = message;
}

QString qtKeyName(xkb_keysym_t keysym)
{
    char canonicalName[128] = {};
    if (xkb_keysym_get_name(keysym, canonicalName, sizeof(canonicalName)) <= 0)
        return {};

    QString converted = QKeySequenceConverter::xkbToQKeySequence(
            QString::fromLatin1(canonicalName));
    QKeySequence sequence = QKeySequence::fromString(converted, QKeySequence::PortableText);
    if (QByteArrayView(canonicalName).startsWith("KP_")) {
        if (sequence.isEmpty() || sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown)
            return {};
        const QString normalized = sequence.toString(QKeySequence::PortableText);
        return QKeySequenceConverter::qKeySequenceToXkb(normalized)
                == QString::fromLatin1(canonicalName) ? normalized : QString();
    }
    if (!sequence.isEmpty() && sequence.count() == 1 && sequence[0].key() != Qt::Key_unknown
            && !sequence.toString(QKeySequence::PortableText).isEmpty())
        return sequence.toString(QKeySequence::PortableText);

    const char32_t codepoint = static_cast<char32_t>(xkb_keysym_to_utf32(keysym));
    if (codepoint != 0 && QChar::isPrint(codepoint)) {
        const QString text = QString::fromUcs4(&codepoint, 1);
        sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (!sequence.isEmpty() && sequence.count() == 1 && sequence[0].key() != Qt::Key_unknown
                && !sequence.toString(QKeySequence::PortableText).isEmpty())
            return sequence.toString(QKeySequence::PortableText);
    }
    return {};
}

} // namespace

std::optional<QString> XdgShortcutParser::normalize(const QString &trigger,
                                                    QString *errorMessage)
{
    if (trigger.isEmpty() || trigger.size() > MaximumTriggerLength) {
        setError(errorMessage, QStringLiteral("Trigger is empty or exceeds the length limit"));
        return std::nullopt;
    }

    const QStringList parts = trigger.split(QLatin1Char('+'), Qt::KeepEmptyParts);
    if (parts.isEmpty() || parts.contains(QString())) {
        setError(errorMessage, QStringLiteral("Trigger contains an empty component"));
        return std::nullopt;
    }

    static const QRegularExpression identifierPattern(QStringLiteral("^[A-Za-z0-9_]+$"));
    const QString keyIdentifier = parts.constLast();
    if (!identifierPattern.match(keyIdentifier).hasMatch()) {
        setError(errorMessage, QStringLiteral("The keysym identifier is malformed"));
        return std::nullopt;
    }

    QSet<QString> seenModifiers;
    QStringList qtParts;
    for (qsizetype index = 0; index + 1 < parts.size(); ++index) {
        const QString &modifier = parts.at(index);
        if (!allowedModifiers().contains(modifier) || seenModifiers.contains(modifier)) {
            setError(errorMessage, QStringLiteral("Trigger contains an unknown or duplicate modifier"));
            return std::nullopt;
        }
        // XDG NUM is XKB's NumLock/Mod2, not Qt's keypad-location modifier.
        // PortableText cannot express that lock-state requirement.
        if (modifier == QLatin1String("NUM")) {
            setError(errorMessage, QStringLiteral("Num Lock modifiers are not supported"));
            return std::nullopt;
        }
        seenModifiers.insert(modifier);
        qtParts.append(qtModifierName(modifier));
    }

    const QByteArray keysymName = keyIdentifier.toLatin1();
    const xkb_keysym_t keysym = xkb_keysym_from_name(keysymName.constData(), XKB_KEYSYM_NO_FLAGS);
    if (keysym == XKB_KEY_NoSymbol) {
        setError(errorMessage, QStringLiteral("Trigger contains an unknown keysym"));
        return std::nullopt;
    }

    const QString keyName = qtKeyName(keysym);
    if (keyName.isEmpty()) {
        setError(errorMessage, QStringLiteral("The keysym cannot be represented by the current backend"));
        return std::nullopt;
    }
    qtParts.append(keyName);

    const QKeySequence sequence = QKeySequence::fromString(qtParts.join(QLatin1Char('+')),
                                                           QKeySequence::PortableText);
    if (sequence.isEmpty() || sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown
            || sequence.toString(QKeySequence::PortableText).isEmpty()) {
        setError(errorMessage, QStringLiteral("The trigger is not a single keyboard shortcut"));
        return std::nullopt;
    }
    return sequence.toString(QKeySequence::PortableText);
}

std::optional<PortalTriggerList> XdgShortcutParser::normalizeBatch(
        const PortalTriggerList &requested,
        QString *errorMessage)
{
    QSet<QString> shortcutIds;
    PortalTriggerList normalized;
    normalized.reserve(requested.size());
    for (const PortalTrigger &item : requested) {
        if (item.shortcutId.isEmpty() || shortcutIds.contains(item.shortcutId)) {
            setError(errorMessage, QStringLiteral("Shortcut IDs must be non-empty and unique"));
            return std::nullopt;
        }
        shortcutIds.insert(item.shortcutId);

        const std::optional<QString> hotkey = normalize(item.trigger, errorMessage);
        if (!hotkey)
            return std::nullopt;
        normalized.append({item.shortcutId, *hotkey});
    }
    return normalized;
}
