// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QTimer>
#include <QMap>
#include <QSharedPointer>

#include <DConfig>

using Dtk::Core::DConfig;

class TimeDateDBusProxy;

class ThemeManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.service.thememanager")

    // 经纬坐标
    struct coordinate{
        double latitude;
        double longitude;
    };

public:
    explicit ThemeManager(QObject *parent = 0);

    void init();

private:
    void initCoordinate();
    void iso6709Parsing(QString city, QString coordinates);

    void enableThemeAuto(bool enable);
    void autoSetTheme(double latitude, double longitude);
    bool doSetGlobalTheme(QString type);

protected:
    void timerEvent(QTimerEvent *event) override;

public slots:
    void handleTimezoneChanged(QString timezone);
    void handleTimeUpdate();
    void handleNTPChanged();
    void handleSettingDConfigChange(QString key);
    void handleGlobalThemeChangeTimeOut();

private:
    DConfig *m_settingDconfig = nullptr;
    QSharedPointer<TimeDateDBusProxy> m_dbusProxy;
    QMap<QString, coordinate> m_coordinateMap;  // 时区与经纬度对应关系
    double m_longitude;  // 经度
    double m_latitude;  // 纬度
    int m_timeUpdateTimeId;
    int m_ntpTimeId;
    QTimer m_themeAutoTimer;  // 自动切换深浅主题定时器
    bool m_autoTheme;
    QString m_curThemeType;
};

#endif // THEMEMANAGER_H
