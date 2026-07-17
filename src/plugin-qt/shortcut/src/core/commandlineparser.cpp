// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "commandlineparser.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace {

enum class ParserState : uint8_t {
    Normal,
    InSingleQuote,
    InDoubleQuote,
};

QString quoteArgument(const QString &argument)
{
    const bool needsQuotes = argument.isEmpty()
            || std::any_of(argument.cbegin(), argument.cend(), [](QChar ch) {
        return ch.isSpace() || ch == QLatin1Char('\\') || ch == QLatin1Char('"')
                || ch == QLatin1Char('\'');
    });
    if (!needsQuotes)
        return argument;

    QString quoted;
    quoted.reserve(argument.size() + 2);
    quoted.append(QLatin1Char('"'));
    for (const QChar ch : argument) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('"'))
            quoted.append(QLatin1Char('\\'));
        quoted.append(ch);
    }
    quoted.append(QLatin1Char('"'));
    return quoted;
}

} // namespace

namespace CommandLineParser {

std::optional<QStringList> split(const QString &command)
{
    if (command.isEmpty())
        return std::nullopt;

    QStringList arguments;
    QString currentArgument;
    currentArgument.reserve(command.size());

    ParserState state = ParserState::Normal;
    bool hasArgument = false;

    for (auto it = command.cbegin(); it != command.cend(); ++it) {
        const QChar ch = *it;
        switch (state) {
        case ParserState::Normal:
            if (ch == QLatin1Char('\\')) {
                if (++it == command.cend())
                    return std::nullopt;
                currentArgument.append(*it);
                hasArgument = true;
            } else if (ch == QLatin1Char('"')) {
                state = ParserState::InDoubleQuote;
                hasArgument = true;
            } else if (ch == QLatin1Char('\'')) {
                state = ParserState::InSingleQuote;
                hasArgument = true;
            } else if (ch.isSpace()) {
                if (hasArgument) {
                    arguments.append(currentArgument);
                    currentArgument.clear();
                    hasArgument = false;
                }
            } else {
                currentArgument.append(ch);
                hasArgument = true;
            }
            break;
        case ParserState::InDoubleQuote:
            if (ch == QLatin1Char('\\')) {
                if (++it == command.cend())
                    return std::nullopt;

                const QChar next = *it;
                if (next == QLatin1Char('"') || next == QLatin1Char('\\')
                        || next == QLatin1Char('$') || next == QLatin1Char('`')) {
                    currentArgument.append(next);
                } else {
                    currentArgument.append(QLatin1Char('\\'));
                    currentArgument.append(next);
                }
            } else if (ch == QLatin1Char('"')) {
                state = ParserState::Normal;
            } else {
                currentArgument.append(ch);
            }
            break;
        case ParserState::InSingleQuote:
            if (ch == QLatin1Char('\'')) {
                state = ParserState::Normal;
            } else {
                currentArgument.append(ch);
            }
            break;
        }
    }

    if (state != ParserState::Normal)
        return std::nullopt;

    if (hasArgument)
        arguments.append(currentArgument);

    if (arguments.isEmpty() || arguments.constFirst().isEmpty())
        return std::nullopt;

    return arguments;
}

QString join(const QStringList &arguments)
{
    QStringList quotedArguments;
    quotedArguments.reserve(arguments.size());
    for (const QString &argument : arguments)
        quotedArguments.append(quoteArgument(argument));
    return quotedArguments.join(QLatin1Char(' '));
}

QStringList expandFieldCodesWithoutFiles(const QStringList &arguments)
{
    if (arguments.isEmpty())
        return arguments;

    QStringList expandedArguments{arguments.constFirst()};
    expandedArguments.reserve(arguments.size());
    for (auto argumentIt = std::next(arguments.cbegin());
         argumentIt != arguments.cend(); ++argumentIt) {
        const QString &argument = *argumentIt;
        QString expanded;
        expanded.reserve(argument.size());

        for (auto chIt = argument.cbegin(); chIt != argument.cend(); ++chIt) {
            const QChar ch = *chIt;
            if (ch != QLatin1Char('%') || std::next(chIt) == argument.cend()) {
                expanded.append(ch);
                continue;
            }

            const QChar fieldCode = *++chIt;
            if (fieldCode == QLatin1Char('%')) {
                expanded.append(QLatin1Char('%'));
            } else if (fieldCode != QLatin1Char('f') && fieldCode != QLatin1Char('F')
                       && fieldCode != QLatin1Char('u') && fieldCode != QLatin1Char('U')) {
                expanded.append(QLatin1Char('%'));
                expanded.append(fieldCode);
            }
        }

        if (!expanded.isEmpty() || argument.isEmpty())
            expandedArguments.append(expanded);
    }
    return expandedArguments;
}

} // namespace CommandLineParser
