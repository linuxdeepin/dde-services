// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "slideshowmanager.h"
#include "background/backgrounds.h"
#include "commondefine.h"
#include "utils.h"

#include <QDBusPendingReply>
#include <QJsonParseError>
#include <QJsonObject>
#include <QScreen>
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

bool SlideshowManager::doSetWallpaperSlideShow(const QString &monitorName, const QString &wallpaperSlideShow)
{
    if (!isValidScreen(monitorName)) {
        qWarning() << "monitor can not found: " << monitorName;
        return false;
    }
    QByteArray jsonData = m_wallpaperSlideShow.toUtf8();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &err);

    QJsonObject cfgObj;
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        cfgObj = doc.object();
    }

    cfgObj[monitorName] = wallpaperSlideShow;

    doc.setObject(cfgObj);

    QString value = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    setWallpaperSlideShow(value);
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
    emit propertyChanged(WALLPAPERSLIDESHOWNAME, value);
    updateWSPolicy(value);
    return true;
}

QString SlideshowManager::doGetWallpaperSlideShow(QString monitorName)
{
    QJsonDocument doc = QJsonDocument::fromJson(m_wallpaperSlideShow.toLatin1());
    QVariantMap tempMap = doc.object().toVariantMap();

    if (tempMap.count(monitorName) == 1) {
        return tempMap[monitorName].toString();
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
        const QString screenName = iter.first;
        if (screenName.isEmpty()) {
            qWarning() << "screenName is empty: " << iter.first;
            continue;
        }
        if (m_wsSchedulerMap.count(iter.first) == 0) {
            QSharedPointer<WallpaperScheduler> wallpaperScheduler(
                    new WallpaperScheduler(std::bind(&SlideshowManager::autoChangeBg, this, std::placeholders::_1, std::placeholders::_2)));
            m_wsSchedulerMap[iter.first] = wallpaperScheduler;
        }

        if (m_wsLoopMap.count(iter.first) == 0) {
            m_wsLoopMap[iter.first] = QSharedPointer<WallpaperLoop>(new WallpaperLoop(m_wallpaperType[screenName]));
        }
        m_wsLoopMap[iter.first]->updateWallpaperType(m_wallpaperType[screenName]);

        if (WallpaperLoopConfigManger::isValidWSPolicy(iter.second.toString())) {
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
    WallpaperLoopConfigManger::WallpaperLoopConfigMap cfg = wallConfig.loadWSConfig(WS_CONFIG_PATH);

    for (auto monitorSpace : cfg.keys()) {
        const QString screenName  = monitorSpace;
        if (screenName.isEmpty()) {
            continue;
        }
        if (m_wsSchedulerMap.count(monitorSpace) == 0) {
            QSharedPointer<WallpaperScheduler> wallpaperScheduler(
                    new WallpaperScheduler(std::bind(&SlideshowManager::autoChangeBg, this, std::placeholders::_1, std::placeholders::_2)));
            m_wsSchedulerMap[monitorSpace] = wallpaperScheduler;
        }

        m_wsSchedulerMap[monitorSpace]->setLastChangeTime(cfg[monitorSpace].lastChange);

        if (m_wsLoopMap.count(monitorSpace) == 0) {
            m_wsLoopMap[monitorSpace] = QSharedPointer<WallpaperLoop>(new WallpaperLoop(m_wallpaperType.value(screenName)));
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

    if (file.isEmpty() || !QFile::exists(file)) {
        qWarning() << "auto change bg error, file not exist: " << file;
        return;
    }

    setMonitorBackground(monitorSpace, file);

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
        const QString screenName = iter.first.split("&&").first();
        if (screenName.isEmpty()) {
            qWarning() << "screenName is empty: " << iter.first;
            continue;
        }

        if (m_wsSchedulerMap.count(iter.first) != 1) {
            QSharedPointer<WallpaperScheduler> wallpaperScheduler(
                    new WallpaperScheduler(std::bind(&SlideshowManager::autoChangeBg, this, std::placeholders::_1, std::placeholders::_2)));
            m_wsSchedulerMap[iter.first] = wallpaperScheduler;
        }

        if (!m_wsLoopMap.contains(iter.first)) {
            m_wsLoopMap[iter.first] = QSharedPointer<WallpaperLoop>(new WallpaperLoop(m_wallpaperType.value(screenName)));
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
    QFile::remove(WS_CONFIG_PATH);

    const QString wallpaperSlideShow = m_settingDconfig->value(GSKEYWALLPAPERSLIDESHOW).toString();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(wallpaperSlideShow.toLatin1(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "parse wallpaperSlideShow failed:" << err.errorString();
        return;
    }

    QJsonObject rootObject = doc.object();
    QJsonObject newObject;

    // 兼容老配置，去掉&&
    for (auto it = rootObject.begin(); it != rootObject.end(); ++it) {
        QString key = it.key();
        QJsonValue value = it.value();

        if (key.contains("&&")) {
            QString newKey = key.split("&&").first();
            newObject[newKey] = value;
        } else {
            newObject[key] = value;
        }
    }

    QJsonDocument newDoc(newObject);
    m_wallpaperSlideShow = newDoc.toJson(QJsonDocument::Compact);

    m_settingDconfig->setValue(GSKEYWALLPAPERSLIDESHOW, m_wallpaperSlideShow);

    onWallpaperChanged();
}

bool SlideshowManager::changeBgAfterLogin(QString monitorSpace)
{
    QString runDir = utils::GetUserRuntimeDir();

    QDBusPendingReply<QString> currentSessionPath = DDBusSender()
        .service("org.deepin.dde.SessionManager1")
        .path("/org/deepin/dde/Session1")
        .interface("org.deepin.dde.Session1")
        .method("GetSessionPath")
        .call();

    runDir = runDir + "/dde-daemon-wallpaper-slideshow-login" + "/" + monitorSpace;
    QFileInfo fileInfo(runDir);
    QDir().mkpath(fileInfo.absolutePath());

    if (!fileInfo.exists()) {
        // 文件不存在，创建并写入当前会话路径
        QFile fileTemp(runDir);
        if (fileTemp.open(QIODevice::WriteOnly | QIODevice::Text)) {
            fileTemp.write(currentSessionPath.value().toLatin1());
            fileTemp.close();
            autoChangeBg(monitorSpace, QDateTime::currentDateTimeUtc());
        } else {
            qWarning() << "failed to create file: " << runDir;
            return false;
        }
    } else {
        // 文件存在，读取并比较
        QFile fileTemp(runDir);
        if (fileTemp.open(QIODevice::ReadWrite | QIODevice::Text)) {
            const QString &recordSessionPath = fileTemp.readAll().simplified();
            if (recordSessionPath != currentSessionPath) {
                autoChangeBg(monitorSpace, QDateTime::currentDateTimeUtc());
                fileTemp.resize(0);
                fileTemp.write(currentSessionPath.value().toLatin1());
            }
            fileTemp.close();
        } else {
            qWarning() << "failed to open file: " << runDir;
            return false;
        }
    }
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
    qDebug() << "wallpaper changed";
    Backgrounds::instance()->refreshBackground();
    bool update = false;
    for (const auto &screen : qApp->screens()) {
        if (screen) {
            const QString &screenName = screen->name();
            const auto &wallpaper = m_dbusProxy->getCurrentWorkspaceBackgroundForMonitor(screenName);
            const auto &wallpaperType = Backgrounds::getBackgroundType(wallpaper);

            if (m_wallpaperType.value(screenName) != wallpaperType) {
                qInfo() << "wallpaperSlideshow type changed: old is " << m_wallpaperType[screenName] << "new: " << wallpaperType << "screen: " << screenName;
                m_wallpaperType[screenName] = wallpaperType;
                update = true;
            }
        }
    }

    if (update) {
        updateWSPolicy(m_wallpaperSlideShow);

        for (auto it = m_wsLoopMap.begin(); it != m_wsLoopMap.end(); ++it) {
            it.value()->updateLoopList();
        }
    }
}

bool SlideshowManager::isValidScreen(const QString &screenName)
{
    for (auto screen : qApp->screens()) {
        if (screen && screen->name() == screenName) {
            return true;
        }
    }
    return false;
}
