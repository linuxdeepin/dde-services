// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "kbdlightcontroller.h"

#include <QDebug>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QIODevice>

KbdLightController::KbdLightController(QObject *parent) 
    : BaseController(parent)
    , m_backlightInterface(nullptr)
{
    // Connect to backlight service
    m_backlightInterface = new QDBusInterface(
        "org.deepin.dde.BacklightHelper1",
        "/org/deepin/dde/BacklightHelper1",
        "org.deepin.dde.BacklightHelper1",
        QDBusConnection::systemBus(),
        this
    );
    
    if (!m_backlightInterface->isValid()) {
        qWarning() << "Failed to connect to Backlight service for keyboard:" 
                   << m_backlightInterface->lastError().message();
    }
}

KbdLightController::~KbdLightController()
{
    if (m_backlightInterface) {
        delete m_backlightInterface;
    }
}

QStringList KbdLightController::supportedActions() const
{
    return QStringList{
        "toggle",
        "brightness-up",
        "brightness-down"
    };
}

bool KbdLightController::execute(const QString &action, const QStringList &args)
{
    Q_UNUSED(args);
    
    if (action == "toggle") {
        return toggle();
    } else if (action == "brightness-up") {
        return changeBrightness(true);
    } else if (action == "brightness-down") {
        return changeBrightness(false);
    }
    
    qWarning() << "Unknown keyboard light action:" << action;
    return false;
}

QString KbdLightController::actionHelp(const QString &action) const
{
    static const QMap<QString, QString> helpMap = {
        {"toggle", "Toggle keyboard backlight on/off"},
        {"brightness-up", "Increase keyboard backlight brightness"},
        {"brightness-down", "Decrease keyboard backlight brightness"}
    };
    return helpMap.value(action);
}

bool KbdLightController::toggle()
{
    if (!m_backlightInterface || !m_backlightInterface->isValid()) {
        qDebug() << "Keyboard backlight not available";
        return false;
    }
    
    // Get keyboard backlight controller info
    QString controllerName;
    int maxBrightness;
    if (!getKbdController(controllerName, maxBrightness)) {
        qDebug() << "Keyboard backlight controller not found";
        return false;
    }
    
    // Get current brightness (read from sysfs)
    QFile brightnessFile(QString("/sys/class/leds/%1/brightness").arg(controllerName));
    if (!brightnessFile.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to read keyboard brightness file";
        return false;
    }
    
    QString brightnessStr = brightnessFile.readAll().trimmed();
    int currentBrightness = brightnessStr.toInt();
    
    // If currently off, turn on to max; otherwise turn off
    int newBrightness = (currentBrightness == 0) ? maxBrightness : 0;
    
    // Use same parameter format as Go version: SetBrightness(type, name, value)
    QDBusReply<void> setReply = m_backlightInterface->call("SetBrightness", static_cast<uchar>(2), controllerName, static_cast<qint32>(newBrightness));
    if (!setReply.isValid()) {
        qWarning() << "Failed to set keyboard brightness:" << setReply.error().message();
        return false;
    }
    
    qDebug() << "Toggled keyboard backlight:" << currentBrightness << "->" << newBrightness;
    return true;
}

bool KbdLightController::changeBrightness(bool raised)
{
    if (!m_backlightInterface || !m_backlightInterface->isValid()) {
        qDebug() << "Keyboard backlight not available";
        return false;
    }
    
    // Get keyboard backlight controller info
    QString controllerName;
    int maxBrightness;
    if (!getKbdController(controllerName, maxBrightness)) {
        qDebug() << "Keyboard backlight controller not found";
        return false;
    }
    
    // Get current brightness (read from sysfs)
    QFile brightnessFile(QString("/sys/class/leds/%1/brightness").arg(controllerName));
    if (!brightnessFile.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to read keyboard brightness file";
        return false;
    }
    
    QString brightnessStr = brightnessFile.readAll().trimmed();
    int currentBrightness = brightnessStr.toInt();
    
    // Calculate step value (same algorithm as Go version)
    static int kbdBacklightStep = 0;
    if (kbdBacklightStep == 0) {
        int tmp = maxBrightness / 10;
        if (tmp == 0) {
            tmp = 1;
        }
        // Round to nearest integer (round down if < 0.5)
        if (static_cast<double>(maxBrightness) / 10 < static_cast<double>(tmp) + 0.5) {
            kbdBacklightStep = tmp;
        } else {
            kbdBacklightStep = tmp + 1;
        }
    }
    
    qDebug() << "Keyboard backlight info: current=" << currentBrightness 
             << "max=" << maxBrightness << "step=" << kbdBacklightStep;
    
    int newBrightness = currentBrightness + (raised ? kbdBacklightStep : -kbdBacklightStep);
    
    // Clamp to valid range
    if (newBrightness < 0) newBrightness = 0;
    if (newBrightness > maxBrightness) newBrightness = maxBrightness;
    
    qDebug() << "Will set keyboard backlight to:" << newBrightness;
    
    // Use same parameter format as Go version: SetBrightness(type, name, value)
    QDBusReply<void> setReply = m_backlightInterface->call("SetBrightness", static_cast<uchar>(2), controllerName, static_cast<qint32>(newBrightness));
    if (!setReply.isValid()) {
        qWarning() << "Failed to set keyboard brightness:" << setReply.error().message();
        return false;
    }
    
    qDebug() << "Changed keyboard brightness:" << currentBrightness << "->" << newBrightness;
    return true;
}

bool KbdLightController::getKbdController(QString &controllerName, int &maxBrightness)
{
    // Simulate local controller lookup (following Go version logic)
    // In actual system, should read keyboard backlight devices under /sys/class/leds/
    QDir ledsDir("/sys/class/leds");
    if (!ledsDir.exists()) {
        qDebug() << "No LEDs directory found";
        return false;
    }
    
    // Find keyboard backlight device (usually contains keywords like "kbd_backlight")
    QStringList entries = ledsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (entry.contains("kbd") || entry.contains("keyboard") || 
            entry.contains("backlight") || entry.toLower().contains("kbd_backlight")) {
            controllerName = entry;
            
            // Read max brightness
            QFile maxFile(QString("/sys/class/leds/%1/max_brightness").arg(entry));
            if (maxFile.open(QIODevice::ReadOnly)) {
                QString maxStr = maxFile.readAll().trimmed();
                maxBrightness = maxStr.toInt();
                if (maxBrightness > 0) {
                    qDebug() << "Found keyboard controller:" << controllerName << "max brightness:" << maxBrightness;
                    return true;
                }
            }
        }
    }
    
    qDebug() << "No keyboard backlight controllers found";
    return false;
}
