// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "qkeysequenceconverter.h"

#include <QDebug>
#include <QRegularExpression>
#include <QMap>
#include <QKeySequence>

// Build Qt key name to XKB name mapping table
// Key: Qt Canonical Name (as returned by QKeySequence::toString(PortableText))
// Value: XKB/X11 Name
static const QMap<QString, QString>& getQtToXkbMap() {
    static QMap<QString, QString> map;
    if (map.isEmpty()) {
        // App Launches
        map.insert("WWW", "XF86WWW");
        map.insert("My Computer", "XF86MyComputer");
        map.insert("Calculator", "XF86Calculator");
        map.insert("Favorites", "XF86Favorites");
        map.insert("Home Page", "XF86HomePage");
        map.insert("Search", "XF86Search");
        map.insert("Launch Mail", "XF86Mail");
        map.insert("Launch Media", "XF86AudioMedia");
        map.insert("Calendar", "XF86Calendar");

        // Media Controls
        map.insert("Media Play", "XF86AudioPlay");
        map.insert("Media Pause", "XF86AudioPause");
        map.insert("Media Record", "XF86AudioRecord");
        map.insert("Media Stop", "XF86AudioStop");
        map.insert("Media Previous", "XF86AudioPrev");
        map.insert("Media Next", "XF86AudioNext");
        map.insert("Media Rewind", "XF86AudioRewind");
        map.insert("Media Fast Forward", "XF86AudioForward");
        map.insert("Volume Mute", "XF86AudioMute");
        map.insert("Volume Down", "XF86AudioLowerVolume");
        map.insert("Volume Up", "XF86AudioRaiseVolume");
        map.insert("Microphone Mute", "XF86AudioMicMute");
        map.insert("Video", "XF86Video");
        map.insert("Music", "XF86Music");
        map.insert("Pictures", "XF86Pictures");

        // System Launches / Tools
        map.insert("Shop", "XF86Shop");
        map.insert("Finance", "XF86Finance");
        map.insert("Documents", "XF86Documents");
        map.insert("Browser", "XF86Explorer");
        map.insert("Game", "XF86Game");
        map.insert("Phone", "XF86Phone");
        map.insert("Messenger", "XF86Messenger");
        map.insert("WebCam", "XF86WebCam");
        map.insert("Tools", "XF86Tools");
        map.insert("Wireless", "XF86WLAN");
        map.insert("RF Kill", "XF86RFKill");
        map.insert("DOS", "XF86DOS");

        // Browser/Navigation
        map.insert("Back", "XF86Back");
        map.insert("Forward", "XF86Forward");
        map.insert("Reload", "XF86Reload");
        map.insert("Open URL", "XF86OpenURL");
        map.insert("Stop", "XF86Stop");
        map.insert("Refresh", "XF86Refresh");

        // Hardware Controls
        map.insert("Monitor Brightness Up", "XF86MonBrightnessUp");
        map.insert("Monitor Brightness Down", "XF86MonBrightnessDown");
        map.insert("Keyboard Light On/Off", "XF86KbdLightOnOff");
        map.insert("Keyboard Brightness Up", "XF86KbdBrightnessUp");
        map.insert("Keyboard Brightness Down", "XF86KbdBrightnessDown");
        map.insert("Eject", "XF86Eject");
        map.insert("Touchpad Toggle", "XF86TouchpadToggle");
        map.insert("Touchpad On", "XF86TouchpadOn");
        map.insert("Touchpad Off", "XF86TouchpadOff");

        // System State
        map.insert("Power Off", "XF86PowerOff");
        map.insert("Wake Up", "XF86WakeUp");
        map.insert("Sleep", "XF86Sleep");
        map.insert("Screensaver", "XF86ScreenSaver");
        map.insert("Away", "XF86Away");
        map.insert("Suspend", "XF86Suspend");

        // Common App Actions (Global if dedicated keys)
        map.insert("Close", "XF86Close");
        map.insert("Copy", "XF86Copy");
        map.insert("Cut", "XF86Cut");
        map.insert("Paste", "XF86Paste");
        map.insert("Save", "XF86Save");
        map.insert("Open", "XF86Open");
        map.insert("New", "XF86New");
        map.insert("Reply", "XF86Reply");
        map.insert("Send", "XF86Send");
        map.insert("Mail Forward", "XF86MailForward");

        // Others
        map.insert("Go", "XF86Go");
        map.insert("Task Pane", "XF86TaskPane");
        map.insert("Xfer", "XF86Xfer");
        map.insert("ScrollUp", "XF86ScrollUp");
        map.insert("ScrollDown", "XF86ScrollDown");

        // Standard Keys with different XKB names
        map.insert("PgUp", "Page_Up");
        map.insert("PgDown", "Page_Down");
        map.insert("Del", "Delete");
        map.insert("Backspace", "BackSpace");
        map.insert("Esc", "Escape");
        map.insert("Ins", "Insert");
        
        // Modifiers
        map.insert("Meta", "Super_L");
        map.insert("Control", "Control_L");
        map.insert("Alt", "Alt_L"); 
        map.insert("Shift", "Shift_L");
        map.insert("Super", "Super_L"); 
        
        // Lock keys
        map.insert("CapsLock", "Caps_Lock");
        map.insert("NumLock", "Num_Lock");
    }
    return map;
}

// Reverse mapping: XKB -> Qt Standard Name
static const QMap<QString, QString>& getXkbToQtMap() {
    static QMap<QString, QString> map;
    if (map.isEmpty()) {
        const auto &qtToXkb = getQtToXkbMap();
        for (auto it = qtToXkb.begin(); it != qtToXkb.end(); ++it) {
             // Simple reverse: XKB -> Qt Name
             if (!map.contains(it.value())) {
                 map.insert(it.value(), it.key());
             }
        }
        
        // Ensure specific mappings (preferences)
        map.insert("XF86AudioMute", "Volume Mute");
        map.insert("Page_Up", "PgUp");
        map.insert("Page_Down", "PgDown");
        map.insert("Delete", "Del");
        map.insert("Insert", "Ins");
    }
    return map;
}

QString QKeySequenceConverter::xkbToQKeySequence(const QString &xkbString)
{
    if (xkbString.isEmpty()) return QString();

    QStringList parts;
    QString remain = xkbString;
    
    // 1. Extract modifiers
    if (remain.contains("<Control>")) { parts << "Ctrl"; remain.replace("<Control>", ""); }
    if (remain.contains("<Shift>")) { parts << "Shift"; remain.replace("<Shift>", ""); }
    if (remain.contains("<Alt>")) { parts << "Alt"; remain.replace("<Alt>", ""); }
    if (remain.contains("<Super>") || remain.contains("<Meta>")) { 
        parts << "Meta"; 
        remain.replace("<Super>", ""); 
        remain.replace("<Meta>", ""); 
    }
    
    // 2. Convert main key
    QString keyName = remain;
    const auto &map = getXkbToQtMap();
    if (map.contains(keyName)) {
        keyName = map.value(keyName);
    } else {
         if (keyName.length() > 0 && keyName[0].isLower()) {
             keyName[0] = keyName[0].toUpper();
         }
    }
    
    if (!keyName.isEmpty()) parts << keyName;
    return parts.join("+");
}

QString QKeySequenceConverter::qKeySequenceToXkb(const QString &qksString)
{
    if (qksString.isEmpty()) return QString();
    
    QKeySequence seq = QKeySequence::fromString(qksString, QKeySequence::PortableText);
    if (!seq.isEmpty()) {
        QKeyCombination combo = seq[0];
        Qt::KeyboardModifiers mods = combo.keyboardModifiers();
        Qt::Key key = combo.key();
        
        QString result;
        if (mods & Qt::ControlModifier) result += "<Control>";
        if (mods & Qt::ShiftModifier) result += "<Shift>";
        if (mods & Qt::AltModifier) result += "<Alt>";
        if (mods & Qt::MetaModifier) result += "<Super>";
        
        QString qtKeyName = QKeySequence(key).toString(QKeySequence::PortableText);
        
        const auto &map = getQtToXkbMap();
        if (map.contains(qtKeyName)) {
            result += map.value(qtKeyName);
        } else {
            result += qtKeyName;
        }
        return result;
    }
    
    return qksString;
}

QKeySequence QKeySequenceConverter::toQKeySequence(const QString &standardName)
{
    return QKeySequence::fromString(standardName, QKeySequence::PortableText);
}

QString QKeySequenceConverter::toQKeySequenceString(const QKeyCombination &combination)
{
    return QKeySequence(combination).toString(QKeySequence::PortableText);
}

QString QKeySequenceConverter::toQKeySequenceString(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    return toQKeySequenceString(QKeyCombination(modifiers, key));
}

bool QKeySequenceConverter::isMultimediaKey(Qt::Key key)
{
    return (key >= Qt::Key_Back && key <= Qt::Key_Calculator) ||
           (key >= Qt::Key_AudioRewind && key <= Qt::Key_MediaLast) ||
           (key >= Qt::Key_KeyboardLightOnOff && key <= Qt::Key_KeyboardBrightnessUp);
}

QString QKeySequenceConverter::multimediaKeyToQKeySequenceName(Qt::Key key)
{
    return QKeySequence(key).toString(QKeySequence::PortableText);
}

QString QKeySequenceConverter::xkbMultimediaToQKeySequence(const QString &xkbName)
{
    const auto &map = getXkbToQtMap();
    return map.value(xkbName, xkbName);
}
