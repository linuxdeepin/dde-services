// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace CommandLineParser {

// Parse a user-entered command line into the program and its argv entries.
// Returns std::nullopt for empty input, a trailing escape, or unterminated
// quotes. No shell expansion or command substitution is performed.
std::optional<QStringList> split(const QString &command);

// Reconstruct an editable command line while preserving argv boundaries.
QString join(const QStringList &arguments);

// Apply the Desktop Entry field-code behavior that is meaningful when a
// shortcut is triggered without files: %f/%F/%u/%U are removed and %% is
// converted to a literal percent sign. Other field codes are preserved.
QStringList expandFieldCodesWithoutFiles(const QStringList &arguments);

} // namespace CommandLineParser
