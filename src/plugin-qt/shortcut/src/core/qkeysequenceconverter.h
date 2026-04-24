// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QString>
#include <QKeySequence>
#include <QKeyCombination>

/**
 * Shortcut format conversion utility
 * Converts between different formats:
 * - QKeySequence format (for Wayland protocol)
 * - XKB format (X11 standard)
 * - Qt internal format
 */
class QKeySequenceConverter
{
public:
    /**
     * Convert XKB format to QKeySequence format
     * e.g.: "<Control><Alt>T" -> "Ctrl+Alt+T"
     *       "Super_R" -> "Meta"
     *       "XF86AudioMute" -> "VolumeMute"
     */
    static QString xkbToQKeySequence(const QString &xkbString);
    
    /**
     * Convert QKeySequence format to XKB format
     * e.g.: "Ctrl+Alt+T" -> "<Control><Alt>T"
     *       "Meta" -> "Super_L"
     */
    static QString qKeySequenceToXkb(const QString &qksString);
    
    /**
     * Convert standard name string to QKeySequence object
     * Forces PortableText format parsing to ensure names like "Volume Mute" are recognized
     */
    static QKeySequence toQKeySequence(const QString &standardName);

    /**
     * Convert Qt key and modifiers to QKeySequence standard string
     * Note: Uses standard names defined in Qt source (e.g. "Volume Mute", "Page Up")
     * This format is required by Wayland protocol bind_key
     */
    static QString toQKeySequenceString(Qt::Key key, Qt::KeyboardModifiers modifiers);
    
    /**
     * Convert QKeyCombination to QKeySequence string
     */
    static QString toQKeySequenceString(const QKeyCombination &combination);
    
    /**
     * Check if key is a multimedia function key
     * These keys have special representation in QKeySequence
     */
    static bool isMultimediaKey(Qt::Key key);
    
    /**
     * Convert multimedia key to QKeySequence supported name
     * e.g.: Qt::Key_VolumeMute -> "VolumeMute"
     */
    static QString multimediaKeyToQKeySequenceName(Qt::Key key);
    
    /**
     * Convert XKB multimedia key name to QKeySequence name
     * e.g.: "XF86AudioMute" -> "VolumeMute"
     */
    static QString xkbMultimediaToQKeySequence(const QString &xkbName);
};
