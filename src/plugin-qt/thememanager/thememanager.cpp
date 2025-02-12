// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "thememanager.h"
#include "timedatedbusproxy.h"
#include "sunrisesunset.h"

#include <QFile>
#include <QDateTime>
#include <QTimerEvent>
#include <QDebug>
#include <QDBusInterface>
#include <QDBusReply>

#include <pwd.h>

#define NAN_ANGLE         (-200.0)  // 异常经纬度
#define ZONEPATH          "/usr/share/zoneinfo/zone1970.tab"

#define APPEARANCEAPPID   "org.deepin.dde.appearance"
#define APPEARANCESCHEMA  "org.deepin.dde.appearance"
#define KEY_GLOBALTHEME     "Global_Theme"

static const QString dcc_service = "org.deepin.dde.ControlCenter1";
static const QString dcc_path = "/org/deepin/dde/ControlCenter1";
static const QString dcc_interface = "org.deepin.dde.ControlCenter1";


ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_settingDconfig(DConfig::create(APPEARANCEAPPID, APPEARANCESCHEMA, "", this))
    , m_dbusProxy(new TimeDateDBusProxy(this))
    , m_longitude(NAN_ANGLE)
    , m_latitude(NAN_ANGLE)
    , m_timeUpdateTimeId(0)
    , m_ntpTimeId(0)
    , m_themeAutoTimer(this)
    , m_autoTheme(false)
    , m_curThemeType(QString())
{
    init();
}

void ThemeManager::init()
{
    initCoordinate();

    connect(m_dbusProxy.get(), &TimeDateDBusProxy::TimezoneChanged, this, &ThemeManager::handleTimezoneChanged);
    connect(m_dbusProxy.get(), &TimeDateDBusProxy::TimeUpdate, this, &ThemeManager::handleTimeUpdate);
    connect(m_dbusProxy.get(), &TimeDateDBusProxy::NTPChanged, this, &ThemeManager::handleNTPChanged);

    connect(m_settingDconfig, SIGNAL(valueChanged(const QString &)), this, SLOT(handleSettingDConfigChange(QString)));
    connect(&m_themeAutoTimer, SIGNAL(timeout()), this, SLOT(handleGlobalThemeChangeTimeOut()));

    QString globalTheme = m_settingDconfig->value(KEY_GLOBALTHEME).toString();
    m_autoTheme = globalTheme.endsWith(".light") || globalTheme.endsWith(".dark") ? false : true;
    enableThemeAuto(m_autoTheme);
    if (m_autoTheme) {
        autoSetTheme(m_latitude, m_longitude);
    }
}

void ThemeManager::initCoordinate()
{
    QString context;
    QString zonepath = ZONEPATH;
    if (qEnvironmentVariableIsSet("TZDIR"))
        zonepath = qEnvironmentVariable("TZDIR") + "/zone1970.tab";
    QFile file(zonepath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    while (!file.atEnd()) {
        QString line = file.readLine();
        if (line.length() == 0) {
            continue;
        }
        line = line.trimmed();
        if (line.startsWith("#")) {
            continue;
        }

        QStringList strv = line.split("\t");
        if (strv.size() < 3) {
            continue;
        }

        iso6709Parsing(strv[2], strv[1]);
    }

    QString city = m_dbusProxy->timezone();
    if (m_coordinateMap.count(city) == 1) {
        m_latitude = m_coordinateMap[city].latitude;
        m_longitude = m_coordinateMap[city].longitude;
    }
}

void ThemeManager::iso6709Parsing(QString city, QString coordinates)
{
    QRegExp pattern("(\\+|-)\\d+\\.?\\d*");

    QVector<QString> resultVet;

    int pos = 0;
    while ((pos = pattern.indexIn(coordinates, pos)) != -1 && resultVet.size() <= 2) {
        resultVet.push_back(coordinates.mid(pos, pattern.matchedLength()));
        pos += pattern.matchedLength();
    }

    if (resultVet.size() < 2) {
        return;
    }

    resultVet[0] = resultVet[0].mid(0, 3) + "." + resultVet[0].mid(3, resultVet[0].size());
    resultVet[1] = resultVet[1].mid(0, 4) + "." + resultVet[1].mid(4, resultVet[1].size());

    coordinate cdn;

    cdn.latitude = resultVet[0].toDouble();
    cdn.longitude = resultVet[1].toDouble();

    m_coordinateMap[city] = cdn;
}

void ThemeManager::enableThemeAuto(bool enable)
{
    qDebug() << "[thememanager] enableThemeAuto" << enable;
    if (enable) {
        // 开启定时器，每分钟检查一次是否要切换主题
        m_themeAutoTimer.start(60000);
    } else {
        // 非自动模式时，关闭定时器
        if (m_themeAutoTimer.isActive())
            m_themeAutoTimer.stop();
        
        m_curThemeType = QString();
    }
}

void ThemeManager::autoSetTheme(double latitude, double longitude)
{
    QDateTime curr = QDateTime::currentDateTime();
    double utcOffset = curr.offsetFromUtc() / 3600.0;

    QDateTime sunrise, sunset;
    bool bSuccess = SunriseSunset::getSunriseSunset(latitude, longitude, utcOffset, curr.date(), sunrise, sunset);
    if (!bSuccess) {
        return;
    }
    QString themeType;
    if (sunrise.secsTo(curr) >= 0 && curr.secsTo(sunset) >= 0) {
        themeType = "light";
    } else {
        themeType = "dark";
    }

    if (m_curThemeType != themeType) {
        m_curThemeType = themeType;
        doSetGlobalTheme(themeType);
    }
}

bool ThemeManager::doSetGlobalTheme(QString type)
{
    qDebug() << "[thememanager] doSetGlobalTheme:" << type;

    QString url = QString("personalization/themeRoot?type=themeType&value=%1&keepAuto=true").arg(type);
    QDBusInterface managerInter(dcc_service, dcc_path, dcc_interface, QDBusConnection::sessionBus(), this);
    QDBusReply<QVariant> reply = managerInter.call("ShowPage", url);
    if (managerInter.lastError().isValid() ) {
        qWarning() << "Call failed:" << managerInter.lastError().message();
        return false;
    }

    return true;
}

void ThemeManager::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_timeUpdateTimeId || event->timerId() == m_ntpTimeId) {
        if (m_autoTheme) {
            autoSetTheme(m_latitude, m_longitude);
        }

        killTimer(event->timerId());
    }
}

void ThemeManager::handleTimezoneChanged(QString timezone)
{
    qDebug() << "[thememanager] handleTimezoneChanged" << timezone;
    if (m_coordinateMap.count(timezone) == 1) {
        m_latitude = m_coordinateMap[timezone].latitude;
        m_longitude = m_coordinateMap[timezone].longitude;
    }

    if (m_autoTheme) {
        autoSetTheme(m_latitude, m_longitude);
    }
}

void ThemeManager::handleTimeUpdate()
{
    qDebug() << "[thememanager] handleTimeUpdate";
    m_timeUpdateTimeId = this->startTimer(2000);
}

void ThemeManager::handleNTPChanged()
{
    qDebug() << "[thememanager] handleNTPChanged";
    m_ntpTimeId = this->startTimer(2000);
}

void ThemeManager::handleSettingDConfigChange(QString key)
{
    if (key == KEY_GLOBALTHEME) {
        QString globalTheme = m_settingDconfig->value(key).toString();
        m_autoTheme = globalTheme.endsWith(".light") || globalTheme.endsWith(".dark") ? false : true;
        enableThemeAuto(m_autoTheme);
        if (m_autoTheme) {
            autoSetTheme(m_latitude, m_longitude);
        }
    }
}

void ThemeManager::handleGlobalThemeChangeTimeOut()
{
    if (m_longitude <= NAN_ANGLE || m_latitude <= NAN_ANGLE)
        return;

    if (m_autoTheme) {
        autoSetTheme(m_latitude, m_longitude);
    }
}
