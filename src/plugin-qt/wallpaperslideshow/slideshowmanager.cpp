// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "slideshowmanager.h"
#include "background/backgrounds.h"
#include "commondefine.h"
#include "utils.h"

#include <QJsonParseError>
#include <QJsonObject>
#include <DDBusSender>
#include <DGuiApplicationHelper>

DGUI_USE_NAMESPACE

SlideshowManager::SlideshowManager(QObject *parent)
    : QObject(parent)
    , m_settingDconfig(DConfig::create(APPEARANCEAPPID, APPEARANCESCHEMA, "", this))
    , m_dbusProxy(new AppearanceDBusProxy(this))
{
    loadConfig();
    connect(m_dbusProxy.get(), &AppearanceDBusProxy::HandleForSleep, this, &SlideshowManager::handlePrepareForSleep);
    connect(m_dbusProxy.get(), &AppearanceDBusProxy::WallpaperURlsChanged, this, &SlideshowManager::onWallpaperChanged);
    init();
}

SlideshowManager::~SlideshowManager()
{

}

bool SlideshowManager::doSetWallpaperSlideShow(const QString &monitorName,const QString &wallpaperSlideShow)
{
    int idx = m_dbusProxy->GetCurrentWorkspace();

    QJsonDocument doc = QJsonDocument::fromJson(wallpaperSlideShow.toLatin1());
    QJsonObject cfgObj = doc.object();

    QString key = QString("%1&&%2").arg(monitorName).arg(idx);

    cfgObj[key] = wallpaperSlideShow;

    QJsonDocument docTmp;
    docTmp.setObject(cfgObj);
    QString value = docTmp.toJson(QJsonDocument::Compact);

    setWallpaperSlideShow(value);

    m_curMonitorSpace = key;
    return true;
}

bool SlideshowManager::setWallpaperSlideShow(const QString &value)
{
    if (value == m_wallpaperSlideShow) {
        return true;
    }
    if (!m_settingDconfig->isValid()) {
        return false;
    }
    qInfo() << "value: " << value;
    qInfo() << "value: GSKEYWALLPAPERSLIDESHOW" << m_settingDconfig->value(GSKEYWALLPAPERSLIDESHOW);
    m_settingDconfig->setValue(GSKEYWALLPAPERSLIDESHOW, value);
    m_wallpaperSlideShow = value;
    updateWSPolicy(value);
    return true;
}

QString SlideshowManager::doGetWallpaperSlideShow(QString monitorName)
{
    int index = m_dbusProxy->GetCurrentWorkspace();

    QJsonDocument doc = QJsonDocument::fromJson(m_wallpaperSlideShow.toLatin1());
    QVariantMap tempMap = doc.object().toVariantMap();

    QString key = QString("%1&&%2").arg(monitorName).arg(index);

    if (tempMap.count(key) == 1) {
        return tempMap[key].toString();
    }

    return "";
}


void SlideshowManager::updateWSPolicy(QString policy)
{
    if (utils::checkWallpaperLockedStatus()) {
        return;
    }
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(policy.toLatin1(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "json error:" << policy << error.errorString();
        return;
    }
    loadWSConfig();

    QVariantMap config = doc.object().toVariantMap();
    for (auto iter : config.toStdMap()) {
        if (m_wsSchedulerMap.count(iter.first) == 0) {
            QSharedPointer<WallpaperScheduler> wallpaperScheduler(
                    new WallpaperScheduler(std::bind(&SlideshowManager::autoChangeBg, this, std::placeholders::_1, std::placeholders::_2)));
            m_wsSchedulerMap[iter.first] = wallpaperScheduler;
        }

        if (m_wsLoopMap.count(iter.first) == 0) {
            m_wsLoopMap[iter.first] = QSharedPointer<WallpaperLoop>(new WallpaperLoop(m_wallpaperType));
        }
        m_wsLoopMap[iter.first]->updateWallpaperType(m_wallpaperType);

        if (m_curMonitorSpace == iter.first && WallpaperLoopConfigManger::isValidWSPolicy(iter.second.toString())) {
            bool bOk;
            int nSec = iter.second.toString().toInt(&bOk);
            if (bOk) {
                QDateTime curr = QDateTime::currentDateTimeUtc();
                m_wsSchedulerMap[iter.first]->setLastChangeTime(curr);
                m_wsSchedulerMap[iter.first]->setInterval(iter.first, nSec);
                saveWSConfig(iter.first, curr);
            } else {
                m_wsSchedulerMap[iter.first]->stop();
            }
        }
    }
}

void SlideshowManager::loadWSConfig()
{
    WallpaperLoopConfigManger wallConfig;
    QString fileName = utils::GetUserConfigDir() + "/deepin/dde-daemon/appearance/wallpaper-slideshow.json";
    WallpaperLoopConfigManger::WallpaperLoopConfigMap cfg = wallConfig.loadWSConfig(fileName);

    for (auto monitorSpace : cfg.keys()) {
        if (m_wsSchedulerMap.count(monitorSpace) == 0) {
            QSharedPointer<WallpaperScheduler> wallpaperScheduler(
                    new WallpaperScheduler(std::bind(&SlideshowManager::autoChangeBg, this, std::placeholders::_1, std::placeholders::_2)));
            m_wsSchedulerMap[monitorSpace] = wallpaperScheduler;
        }

        m_wsSchedulerMap[monitorSpace]->setLastChangeTime(cfg[monitorSpace].lastChange);

        if (m_wsLoopMap.count(monitorSpace) == 0) {
            m_wsLoopMap[monitorSpace] = QSharedPointer<WallpaperLoop>(new WallpaperLoop(m_wallpaperType));
            m_wsLoopMap[monitorSpace]->updateWallpaperType(Backgrounds::BT_Custom);
        }

        const auto &cacheShowedList = m_wsLoopMap[monitorSpace]->getShowed();
        for (auto file : cfg[monitorSpace].showedList) {
            if (!cacheShowedList.contains(file)) {
                m_wsLoopMap[monitorSpace]->addToShow(file);
            }
        }
    }
}

bool SlideshowManager::saveWSConfig(QString monitorSpace, QDateTime date)
{
    WallpaperLoopConfigManger configManger;

    QString fileName = utils::GetUserConfigDir() + "/deepin/dde-daemon/appearance/wallpaper-slideshow.json";
    configManger.loadWSConfig(fileName);

    if (m_wsLoopMap.count(monitorSpace) != 0) {
        configManger.setShowed(monitorSpace, m_wsLoopMap[monitorSpace]->getShowed());
    }
    configManger.setLastChange(monitorSpace, date);

    return configManger.save(fileName);
}

void SlideshowManager::autoChangeBg(QString monitorSpace, QDateTime date)
{
    qDebug() << "autoChangeBg: " << monitorSpace << ", " << date;

    if (m_wsLoopMap.count(monitorSpace) == 0) {
        return;
    }

    QString file = m_wsLoopMap[monitorSpace]->getNext();
    if (file.isEmpty()) {
        qDebug() << "file is empty";
        return;
    }

    QString strIndex = QString::number(m_dbusProxy->GetCurrentWorkspace());

    QStringList monitorlist = monitorSpace.split("&&");
    if (monitorlist.size() != 2) {
        qWarning() << "monitorSpace format error";
        return;
    }

    if (strIndex == monitorlist.at(1)) {
        setMonitorBackground(monitorlist.at(0), file);
    }

    saveWSConfig(monitorSpace, date);
}

void SlideshowManager::init()
{
    loadWSConfig();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(m_wallpaperSlideShow.toLatin1(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "parse wallpaperSlideShow: " << m_wallpaperSlideShow << ",fail";
        return;
    }

    QVariantMap tempMap = doc.object().toVariantMap();
    for (auto iter : tempMap.toStdMap()) {
        if (m_wsSchedulerMap.count(iter.first) != 1) {
            QSharedPointer<WallpaperScheduler> wallpaperScheduler(
                    new WallpaperScheduler(std::bind(&SlideshowManager::autoChangeBg, this, std::placeholders::_1, std::placeholders::_2)));
            m_wsSchedulerMap[iter.first] = wallpaperScheduler;
        }

        if (!m_wsLoopMap.contains(iter.first)) {
            m_wsLoopMap[iter.first] = QSharedPointer<WallpaperLoop>(new WallpaperLoop(m_wallpaperType));
        }

        if (WallpaperLoopConfigManger::isValidWSPolicy(iter.second.toString())) {
            if (iter.second.toString() == WSPOLICYLOGIN) {
                bool bSuccess = changeBgAfterLogin(iter.first);
                if (!bSuccess) {
                    qWarning() << "failed to change background after login";
                }
            } else {
                bool ok;
                uint sec = iter.second.toString().toUInt(&ok);
                if (m_wsSchedulerMap.count(iter.first) == 1) {
                    if (ok) {
                        m_wsSchedulerMap[iter.first]->setInterval(iter.first, sec);
                    } else {
                        m_wsSchedulerMap[iter.first]->stop();
                    }
                }
            }
        }
    }
}

void SlideshowManager::loadConfig()
{
    m_wallpaperSlideShow = m_settingDconfig->value(GSKEYWALLPAPERSLIDESHOW).toString();
    onWallpaperChanged();
}

bool SlideshowManager::changeBgAfterLogin(QString monitorSpace)
{
    QString runDir = utils::GetUserRuntimeDir();

    QFile file("/proc/self/sessionid");
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "open /proc/self/sessionid fail";
        return false;
    }

    QString currentSessionId = file.readAll();
    currentSessionId = currentSessionId.simplified();

    bool needChangeBg = false;
    runDir = runDir + "/dde-daemon-wallpaper-slideshow-login" + "/" + monitorSpace;
    QFile fileTemp(runDir);

    if (!file.exists()) {
        needChangeBg = true;
    } else if (!fileTemp.open(QIODevice::ReadOnly)) {
        qWarning() << "open " << runDir << " fail";
        return false;
    } else {
        if (currentSessionId != fileTemp.readAll().simplified()) {
            needChangeBg = true;
        }
    }

    if (needChangeBg) {
        autoChangeBg(monitorSpace, QDateTime::currentDateTimeUtc());
        fileTemp.write(currentSessionId.toLatin1());
    }

    file.close();
    fileTemp.close();

    return true;
}

void SlideshowManager::setMonitorBackground(const QString &monitorName, const QString &imageGile)
{
    qInfo() << "auto change wallpaper: " << monitorName << ", " << imageGile;
    if (DGuiApplicationHelper::testAttribute(DGuiApplicationHelper::IsWaylandPlatform)) {
        QString arg = QString("personalization/wallpaper?url=%1&monitor=%2").arg(utils::enCodeURI(imageGile, SCHEME_FILE)).arg(monitorName);
        DDBusSender()
                .service("org.deepin.dde.ControlCenter1")
                .interface("org.deepin.dde.ControlCenter1")
                .path("/org/deepin/dde/ControlCenter1")
                .method("ShowPage")
                .arg(arg)
                .call();
    } else {
        m_dbusProxy->SetCurrentWorkspaceBackgroundForMonitor(utils::enCodeURI(imageGile, SCHEME_FILE), monitorName);
        m_dbusProxy->SetGreeterBackground(utils::enCodeURI(imageGile, SCHEME_FILE));   
    }
}

void SlideshowManager::handlePrepareForSleep(bool sleep)
{
    if (sleep)
        return;

    QJsonDocument doc = QJsonDocument::fromJson(m_wallpaperSlideShow.toLatin1());
    QVariantMap tempMap = doc.object().toVariantMap();

    for (auto it = tempMap.begin(); it != tempMap.end(); ++it) {
        if (it.value().toString() == WSPOLICYWAKEUP)
            autoChangeBg(it.key(), QDateTime::currentDateTimeUtc());
    }
}

void SlideshowManager::onWallpaperChanged()
{
    const auto wallpaper = m_dbusProxy->getCurrentWorkspaceBackground();
    auto wallpaperType = Backgrounds::getBackgroundType(wallpaper);
    if (wallpaperType != m_wallpaperType) {
        qInfo() << "wallpaperSlideshow type changed: old is " << m_wallpaperType << "new: " << wallpaperType;
        m_wallpaperType = wallpaperType;
        updateWSPolicy(m_wallpaperSlideShow);
    }
}
