// SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "xsettingsmanager.h"

#include "modules/api/utils.h"
#include "xsdatainfo.h"

#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QThreadPool>
#include <QVariant>
#include <QtMath>

#include <fcntl.h>

const static int DPI_FALLBACK = 96;
const static int BASE_CURSORSIZE = 24;
const static QString PLYMOUTH_CONFIGFILE = "/etc/plymouth/plymouthd.conf";

// 从 ScreenScale 服务获取缩放信息，返回 {current, recommended}
std::pair<double, double> XSettingsManager::getScaleInfoFromService()
{
    QString screensJson = getScreensJson();
    QDBusReply<QString> reply = m_screenScaleInterface->call("GetScreenScaleInfo", screensJson);
    if (!reply.isValid()) {
        return {0.0, 0.0};
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
    if (!doc.isObject()) {
        return {0.0, 0.0};
    }

    QJsonObject obj = doc.object();
    return {obj.value("current").toDouble(), obj.value("recommended").toDouble()};
}

XSettingsManager::XSettingsManager(QObject *parent)
    : QObject(parent)
    , m_settingDconfig(
          DTK_CORE_NAMESPACE::DConfig::create("org.deepin.dde.daemon", "org.deepin.XSettings"))
    , m_greeterInterface(new QDBusInterface("org.deepin.dde.Greeter1",
                                            "/org/deepin/dde/Greeter1",
                                            "org.deepin.dde.Greeter1",
                                            QDBusConnection::systemBus()))
    , m_sysDaemonInterface(new QDBusInterface("org.deepin.dde.Daemon1",
                                              "/org/deepin/dde/Daemon1",
                                              "org.deepin.dde.Daemon1",
                                              QDBusConnection::systemBus()))
    , m_xcbUtils(XcbUtils::getInstance())
    , m_screenScaleInterface(new QDBusInterface("org.deepin.dde.ScreenScale1",
                                                "/org/deepin/dde/ScreenScale1",
                                                "org.deepin.dde.ScreenScale1",
                                                QDBusConnection::systemBus()))
{
    // 监听 ScreenScale1 的信号，实现一处改变，全处同步
    // 对于非常驻服务，直接连接信号，D-Bus 会在服务启动后传递信号
    QDBusConnection::systemBus().connect(m_screenScaleInterface->service(),
                                         m_screenScaleInterface->path(),
                                         m_screenScaleInterface->interface(),
                                         "ScaleFactorChanged",
                                         this,
                                         SLOT(onScaleFactorChanged(double)));

    // 初始化时确定缩放系数（一次 D-Bus 调用获取 current 和 recommended）
    double initScale = 0;
    double recommended = 0;

    auto [current, rec] = getScaleInfoFromService();
    initScale = current;
    recommended = rec;

    // 如果 ScreenScale 为空，尝试从旧配置迁移或取推荐值
    if (initScale <= 0.1) {
        initScale = getForceScaleFactor();
        if (initScale <= 0.1) {
            initScale = recommended > 0.1 ? recommended : 1.0;
        }

        // 如果有了有效值，同步回 ScreenScale (使其成为新的事实来源)
        if (initScale > 0.1) {
            QDBusMessage setReply = m_screenScaleInterface->call("SetScaleFactor", initScale);
            if (setReply.type() == QDBusMessage::ErrorMessage) {
                qWarning() << "Failed to sync initial scale to ScreenScale1:" << setReply.errorMessage();
            }
        }
    }

    connect(m_settingDconfig,
            &DTK_CORE_NAMESPACE::DConfig::valueChanged,
            this,
            &XSettingsManager::handleDConfigChangedCb);

    // 同步到所有兼容性配置中（仅保存配置，不触发运行时生效）
    onScaleFactorChanged(initScale);

    // 初始化时更新 XSETTINGS 和 Qt 主题配置（登录后生效）
    ScaleFactors factors;
    factors.insert("all", initScale);
    setScreenScaleFactorsForQt(factors);
    updateDPI();

    // 处理旧版本迁移：清理 .dde_env 中的缩放相关环境变量
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!env.value("STARTDDE_MIGRATE_SCALE_FACTOR").isEmpty()) {
        cleanUpDdeEnv();
    }

    setSettings(getSettingsInSchema());
}

ArrayOfColor XSettingsManager::getColor(const QString &prop)
{
    ArrayOfColor arrayOfColor;
    XsValue value = getSettingValue(prop);

    if (std::get_if<ColorValueInfo>(&value) == nullptr) {
        return arrayOfColor;
    }
    ColorValueInfo color = std::get<ColorValueInfo>(value);
    for (size_t i = 0; i < colorSize; i++) {
        arrayOfColor.push_back(color[i]);
    }
    return arrayOfColor;
}

int XSettingsManager::getInteger(const QString &prop)
{
    XsValue value = getSettingValue(prop);

    if (std::get_if<int>(&value) == nullptr) {
        return 0;
    }

    return std::get<int>(value);
}

QString XSettingsManager::getScreensJson()
{
    QList<XcbUtils::MonitorSizeInfo> monitors = m_xcbUtils.getMonitorSizeInfos();
    if (monitors.isEmpty()) {
        return "[]";
    }

    QJsonArray screens;
    for (const auto &monitor : monitors) {
        QJsonObject screen;
        screen["widthPx"] = monitor.width;
        screen["heightPx"] = monitor.height;
        screen["widthMm"] = static_cast<int>(monitor.mmWidth);
        screen["heightMm"] = static_cast<int>(monitor.mmHeight);
        screens.append(screen);
    }

    return QJsonDocument(screens).toJson(QJsonDocument::Compact);
}

double XSettingsManager::getScaleFactor()
{
    // 从 ScreenScale 获取，它是唯一的事实来源
    auto [current, _] = getScaleInfoFromService();
    return current > 0.1 ? current : 0;
}

ScaleFactors XSettingsManager::getScreenScaleFactors()
{
    // 不再通过 individual-scaling 配置获取缩放，改为使用 screenscale 的配置
    // 如果需要用到 ScaleFactors map，直接将当前缩放系数转成 map 即可
    double currentScale = getScaleFactor();

    ScaleFactors factors;
    if (currentScale > 0.1) {
        factors.insert("all", currentScale);
    }

    return factors;
}

QString XSettingsManager::getString(const QString &prop)
{
    XsValue value = getSettingValue(prop);

    if (std::get_if<QString>(&value) == nullptr) {
        return QString();
    }

    return std::get<QString>(value);
}

QByteArray XSettingsManager::getSettingPropValue()
{
    xcb_atom_t atom = m_xcbUtils.getAtom("_XSETTINGS_SETTINGS");
    if (atom == XCB_NONE) {
        qWarning() << "can not get _XSETTINGS_SETTINGS Atom";
        return QByteArray();
    }
    return m_xcbUtils.getXcbAtomProperty(atom);
}

void XSettingsManager::emitSignalSetScaleFactor(bool done, bool emitSignal)
{
    if (!emitSignal) {
        return;
    }
    if (done) {
        Q_EMIT SetScaleFactorDone();
    } else {
        Q_EMIT SetScaleFactorStarted();
    }
}

QString XSettingsManager::listProps()
{
    QByteArray datas = getSettingPropValue();

    XSDataInfo xSDataInfo(datas);

    return xSDataInfo.listProps();
}

void XSettingsManager::setColor(const QString &prop, const ArrayOfColor &v)
{
    if (v.size() != 4) {
        return;
    }
    ColorValueInfo color;
    for (size_t i = 0; i < colorSize; i++) {
        color[i] = v[i];
    }

    XsSetting xsValue;
    xsValue.prop = prop;
    xsValue.type = HeadTypeColor;
    xsValue.value = color;

    QVector<XsSetting> xsSetting{ xsValue };
    setSettings(xsSetting);
    setGSettingsByXProp(prop, color);
}

void XSettingsManager::setInteger(const QString &prop, const int &v)
{
    XsSetting xsValue;
    xsValue.prop = prop;
    xsValue.type = HeadTypeInteger;
    xsValue.value = v;

    QVector<XsSetting> xsSetting{ xsValue };
    setSettings(xsSetting);

    setGSettingsByXProp(prop, v);
}

void XSettingsManager::setScreenScaleFactors(const ScaleFactors &factors, bool emitSignal)
{
    if (factors.isEmpty()) {
        return;
    }

    double singleFactor = 1.0;
    if (factors.contains("all")) {
        singleFactor = factors["all"];
    } else {
        singleFactor = factors.first();
    }

    setSingleScaleFactor(singleFactor, emitSignal);
}

void XSettingsManager::setString(const QString &prop, const QString &v)
{
    XsSetting xsValue;
    xsValue.prop = prop;
    xsValue.type = HeadTypeString;
    xsValue.value = v;

    QVector<XsSetting> xsSetting{ xsValue };
    setSettings(xsSetting);

    setGSettingsByXProp(prop, v); // 设置dconfig
}

void XSettingsManager::handleDConfigChangedCb(const QString &key)
{
    static const QStringList excludedkeys = { "xft-dpi",
                                              "scale-factor",
                                              "window-scale",
                                              "individual-scaling" };
    if (excludedkeys.contains(key)) {
        return;
    }
    if (key == "gtk-cursor-theme-name" || key == "gtk-cursor-theme-size") {
        updateXResources();
    } else if (key == "gtk-cursor-theme-size-base") {
        int cursorSizeBase = m_settingDconfig->value(key).toInt();
        double scale = m_settingDconfig->value(dcKeyScaleFactor).toDouble();
        if (scale <= 0) {
            qWarning() << "invalid scale factor:" << scale << ", fallback to 1.0";
            scale = 1.0;
        }
        qDebug() << "update gtk-cursor-theme-size to" << cursorSizeBase * scale;
        m_settingDconfig->setValue("gtk-cursor-theme-size", cursorSizeBase * scale);
        return;
    }

    QSharedPointer<DconfInfo> dconfInfo = m_dconfInfos.getByDconfKey(key);
    if (dconfInfo.isNull()) {
        return;
    }
    XsValue value = dconfInfo->getValue(*m_settingDconfig);
    XsSetting xsValue;
    xsValue.prop = dconfInfo->getXsetKey();
    xsValue.type = dconfInfo->getKeySType();
    xsValue.value = value;
    QVector<XsSetting> xsSetting{ xsValue };
    setSettings(xsSetting);
}

// GetForceScaleFactor 允许用户通过 force-scale-factor.ini 强制设置全局缩放
double XSettingsManager::getForceScaleFactor()
{
    QString fileName = Utils::GetUserConfigDir() + "/deepin/force-scale-factor.ini";
    KeyFile keyFile;
    bool bSuccess = keyFile.loadFile(fileName);
    if (bSuccess) {
        bool ok = false;
        double forceScaleFactor = keyFile.getStr("ForceScaleFactor", "scale").toDouble(&ok);
        if (ok && forceScaleFactor >= 1.0 && forceScaleFactor <= 3.0) {
            return forceScaleFactor;
        }
        qWarning() << "invalid forceScaleFactor:" << keyFile.getStr("ForceScaleFactor", "scale");
    }
    return -1.0;
}

void XSettingsManager::updateDPI()
{
    double scale = 1;
    QVector<XsSetting> xsSettngVec;
    int scaledDpi = static_cast<int>((DPI_FALLBACK * 1024) * scale);
    int cursorSize = BASE_CURSORSIZE;
    int windowScale = 1;

    if (m_settingDconfig->isValid()) {
        bool bOk = false;
        double tempScaleFactor = m_settingDconfig->value(dcKeyScaleFactor).toDouble(&bOk);
        if (bOk) {
            if (tempScaleFactor <= 0) {
                scale = 1;
            } else {
                scale = tempScaleFactor;
            }
        }

        bOk = false;
        int tempXftDpi = m_settingDconfig->value(dcKeyXftDpi).toInt(&bOk);
        if (bOk) {
            scaledDpi = static_cast<int>((DPI_FALLBACK * 1024) * scale);
            if (tempXftDpi != scaledDpi) {
                m_settingDconfig->setValue(dcKeyXftDpi, scaledDpi);
                XsSetting setting;
                setting.prop = "Xft/DPI";
                setting.value = scaledDpi;
                setting.type = HeadTypeInteger;

                xsSettngVec.push_back(setting);
            }
        }

        bOk = false;
        windowScale = m_settingDconfig->value(dcKeyWindowScale).toInt(&bOk);
        if (bOk) {
            if (windowScale > 1) {
                scaledDpi = static_cast<int>(DPI_FALLBACK * 1024);
            }
        }

        bOk = false;
        int tempGtkCursorThemeSize = m_settingDconfig->value(dcKeyGtkCursorThemeSize).toInt(&bOk);
        if (bOk) {
            cursorSize = tempGtkCursorThemeSize;
        }
    }

    int windowScalingFactor = getInteger("Gdk/WindowScalingFactor");
    if (windowScalingFactor != windowScale) {
        XsSetting sWindowScalingFactor;
        sWindowScalingFactor.prop = "Gdk/WindowScalingFactor";
        sWindowScalingFactor.value = windowScale;
        sWindowScalingFactor.type = HeadTypeInteger;

        xsSettngVec.push_back(sWindowScalingFactor);

        XsSetting sDpi;
        sDpi.prop = "Gdk/UnscaledDPI";
        sDpi.value = scaledDpi;
        sDpi.type = HeadTypeInteger;

        xsSettngVec.push_back(sDpi);

        XsSetting sCursorThemeSize;
        sCursorThemeSize.prop = "Gtk/CursorThemeSize";
        sCursorThemeSize.value = cursorSize;
        sCursorThemeSize.type = HeadTypeInteger;

        xsSettngVec.push_back(sCursorThemeSize);
    }

    if (!xsSettngVec.isEmpty()) {
        setSettings(xsSettngVec);
        updateXResources();
    }
}

void XSettingsManager::updateXResources()
{
    QVector<QPair<QString, QString>> xresourceInfos;
    if (m_settingDconfig->isValid()) {
        xresourceInfos.push_back(
            qMakePair(QString("Xcursor.theme"),
                      m_settingDconfig->value("gtk-cursor-theme-name").toString()));
        xresourceInfos.push_back(
            qMakePair(QString("Xcursor.size"),
                      QString::number(m_settingDconfig->value(dcKeyGtkCursorThemeSize).toInt())));

        double scaleFactor = m_settingDconfig->value(dcKeyScaleFactor).toDouble();
        int xftDpi = static_cast<int>(DPI_FALLBACK * scaleFactor);
        xresourceInfos.push_back(qMakePair(QString("Xft.dpi"), QString::number(xftDpi)));
    }

    m_xcbUtils.updateXResources(xresourceInfos);
}

XsValue XSettingsManager::getSettingValue(QString prop)
{
    QByteArray datas = getSettingPropValue();
    XSDataInfo xsInfo(datas);

    QSharedPointer<XSItemInfo> item = xsInfo.getPropItem(prop);
    if (item.isNull()) {
        qDebug() << "get item null:" << prop;
        return XsValue();
    }
    return item->getValue();
}

void XSettingsManager::setSettings(QVector<XsSetting> settings)
{
    QByteArray datas = getSettingPropValue();
    XSDataInfo xsInfo(datas);
    xsInfo.increaseSerial();

    for (auto xsettingItem : settings) {
        QSharedPointer<XSItemInfo> xsItem = xsInfo.getPropItem(xsettingItem.prop);
        if (xsItem) {
            qDebug() << "setSettings modify:" << xsItem->getHeadName();
            xsItem->modifyProperty(xsettingItem);
            continue;
        }

        QSharedPointer<XSItemInfo> xSItemInfo(
            new XSItemInfo(xsettingItem.prop, xsettingItem.value));
        xsInfo.inserItem(xSItemInfo);
        xsInfo.increaseNumSettings();
    }

    QByteArray value = xsInfo.marshalSettingData();
    m_xcbUtils.changeSettingProp(value);
}

QVector<XsSetting> XSettingsManager::getSettingsInSchema()
{
    QVector<XsSetting> xsSettingVec;
    if (!m_settingDconfig->isValid()) {
        return xsSettingVec;
    }

    QStringList keys = m_settingDconfig->keyList();
    for (auto key : keys) {
        QSharedPointer<DconfInfo> dconfInfo = m_dconfInfos.getByDconfKey(key);
        if (dconfInfo.isNull()) {
            qWarning() << "dconfigInfo is nullptr:" << key;
            continue;
        }

        XsValue value = dconfInfo->getValue(*m_settingDconfig);
        if (!Utils::hasXsValue(value)) {
            qWarning() << "unknown Xs type:" << dconfInfo->getDconfKey()
                       << dconfInfo->getKeySType();
            continue;
        }

        XsSetting xsStting;
        xsStting.type = dconfInfo->getKeySType();
        xsStting.prop = dconfInfo->getXsetKey();
        xsStting.value = value;
        xsSettingVec.push_back(xsStting);
    }
    return xsSettingVec;
}

void XSettingsManager::setGSettingsByXProp(const QString &prop, XsValue value)
{
    QSharedPointer<DconfInfo> info = m_dconfInfos.getByXSKey(prop);
    if (info.isNull()) {
        return;
    }
    info->setValue(*m_settingDconfig, value);
}

void XSettingsManager::setSingleScaleFactor(double scale, bool emitSignal)
{
    if (scale <= 0.1) {
        return;
    }

    emitSignalSetScaleFactor(false, emitSignal);

    // 优先同步到 ScreenScale1 (事实来源)
    auto [remoteScale, _] = getScaleInfoFromService();
    bool screenScaleAvailable = remoteScale > 0.1;

    if (!qFuzzyCompare(remoteScale, scale)) {
        QDBusMessage setReply = m_screenScaleInterface->call("SetScaleFactor", scale);
        if (setReply.type() == QDBusMessage::ErrorMessage) {
            qWarning() << "Failed to set scale factor in ScreenScale1:" << setReply.errorMessage();
            screenScaleAvailable = false;
        }
    }

    // 如果 ScreenScale 调用失败，则降级手动触发同步逻辑
    if (!screenScaleAvailable) {
        onScaleFactorChanged(scale);
    }

    emitSignalSetScaleFactor(true, emitSignal);
}

void XSettingsManager::onScaleFactorChanged(double scale)
{
    if (scale <= 0.1) {
        return;
    }

    qDebug() << "onScaleFactorChanged saving configs for scale:" << scale;

    int windowScale = qFloor((scale + 0.3) * 10) / 10;
    if (windowScale < 1) {
        windowScale = 1;
    }

    ScaleFactors factors;
    factors.insert("all", scale);

    // 1. 同步到 XSettings 的 DConfig (仅为了兼容性，注销后生效)
    if (m_settingDconfig->isValid()) {
        m_settingDconfig->setValue(dcKeyScaleFactor, scale);
        m_settingDconfig->setValue(dcKeyWindowScale, windowScale);
        m_settingDconfig->setValue(cKeyIndividualScaling, joinScreenScaleFactors(factors));

        bool ok = false;
        int baseCursorSizeInt = m_settingDconfig->value("gtk-cursor-theme-size-base").toInt(&ok);
        if (!ok || baseCursorSizeInt <= 0) {
            baseCursorSizeInt = BASE_CURSORSIZE;
        }
        int cursorSize = static_cast<int>(baseCursorSizeInt * scale);
        m_settingDconfig->setValue(dcKeyGtkCursorThemeSize, cursorSize);
    }

    // 2. 同步 Plymouth 缩放（下次启动生效）
    setScaleFactorForPlymouth(windowScale, true);

    // 注意：不在此处调用 updateDPI() 和 setScreenScaleFactorsForQt()
    // 这些操作只应在初始化时执行，运行时修改缩放需要注销后生效
}

void XSettingsManager::setScaleFactorForPlymouth(int factor, bool emitSignal)
{
    if (factor > 2) {
        factor = 2;
    }

    QString theme = getPlymouthTheme(PLYMOUTH_CONFIGFILE);
    int currentFactor = getPlymouthThemeScaleFactor(theme);
    if (currentFactor == factor) {
        emitSignalSetScaleFactor(true, emitSignal);
        return;
    }

    m_sysDaemonInterface->call("ScalePlymouth", static_cast<uint32_t>(factor));
    emitSignalSetScaleFactor(true, emitSignal);
}

QString XSettingsManager::getPlymouthTheme(QString file)
{
    KeyFile keyFile;
    bool bSuccess = keyFile.loadFile(file);
    if (!bSuccess) {
        return "";
    }

    return keyFile.getStr("Daemon", "Theme");
}

int XSettingsManager::getPlymouthThemeScaleFactor(QString theme)
{
    if (theme == "deepin-logo" || theme == "deepin-ssd-logo" || theme == "uos-ssd-logo") {
        return 1;
    } else if (theme == "deepin-hidpi-logo" || theme == "deepin-hidpi-ssd-logo"
               || theme == "uos-hidpi-ssd-logo") {
        return 2;
    } else {
        return 0;
    }
}

QString XSettingsManager::joinScreenScaleFactors(const ScaleFactors &factors)
{
    QString value;

    for (auto key : factors.keys()) {
        value += QString::asprintf("%s=%.2f;", key.toStdString().c_str(), factors[key]);
    }

    return value;
}

void XSettingsManager::setScreenScaleFactorsForQt(const ScaleFactors &factors)
{
    if (factors.isEmpty()) {
        return;
    }

    QString fileName = Utils::GetUserConfigDir() + "/deepin/qt-theme.ini";
    KeyFile keyFile;
    if (!keyFile.loadFile(fileName)) {
        qWarning() << "failed to load qt-theme.ini:";
    }
    QString value;
    if (factors.size() == 1) {
        value = QString::number(factors.first());
    } else {
        value = joinScreenScaleFactors(factors);
    }
    keyFile.setKey(qtThemeSection, qtThemeKeyScreenScaleFactors, value);
    keyFile.deleteKey(qtThemeSection, qtThemeKeyScaleFactor);
    keyFile.setKey(qtThemeSection, qtThemeKeyScaleLogicalDpi, "-1,-1");

    // 当文件路径不存在时创建
    QFile qfile(fileName);
    if (!qfile.exists()) {
        QDir dir(fileName.left(fileName.lastIndexOf("/")));
        dir.mkpath(fileName.left(fileName.lastIndexOf("/")));
        qInfo() << "mkpath" << fileName;
    }

    qfile.open(QIODevice::WriteOnly);
    qfile.close();
    //    keyFile.print();
    keyFile.saveToFile(fileName);

    updateGreeterQtTheme(keyFile);

    cleanUpDdeEnv();
}

void XSettingsManager::updateGreeterQtTheme(KeyFile &keyFile)
{
    QFile file("/tmp/startdde-qt-theme-");
    if (!file.open(QIODevice::ReadWrite)) {
        qWarning() << "open /tmp/startdde-qt-theme- failed";
        return;
    }
    keyFile.setKey(qtThemeSection, qtThemeKeyScaleLogicalDpi, "96,96");
    keyFile.saveToFile(file.fileName());
    if (!file.seek(0)) {
        qWarning() << "file reset failed";
        return;
    }
    QDBusUnixFileDescriptor dbusFd(file.handle());
    QDBusMessage reply =
        m_greeterInterface->call("UpdateGreeterQtTheme", QVariant::fromValue(dbusFd));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "update greeter qt-theme failed:" << reply.errorMessage();
    }
    file.close();
    file.remove();
}

void XSettingsManager::cleanUpDdeEnv()
{
    bool bNeedSave = false;
    QMap<QString, QString> envMap = loadDDEUserEnv();
    const QStringList keyEnvs{ "QT_SCALE_FACTOR",
                               "QT_SCREEN_SCALE_FACTORS",
                               "QT_AUTO_SCREEN_SCALE_FACTOR",
                               "QT_FONT_DPI",
                               "DEEPIN_WINE_SCALE" };

    for (auto env : keyEnvs) {
        if (envMap.find(env) != envMap.end()) {
            bNeedSave = true;
            envMap.erase(envMap.find(env));
        }
    }

    if (bNeedSave) {
        saveDDEUserEnv(envMap);
    }
}

QMap<QString, QString> XSettingsManager::loadDDEUserEnv()
{
    QMap<QString, QString> result;

    QString envFile = Utils::getUserHomeDir() + "/.dde_env";
    QFile file(envFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }

    while (!file.atEnd()) {
        QString line = file.readLine();
        if (line.front() == '#') {
            continue;
        }
        QStringList match = line.split(' ');
        if (match.length() == 3) {
            result[match[1]] = match[2];
        }
    }

    return result;
}

void XSettingsManager::saveDDEUserEnv(const QMap<QString, QString> &userEnvs)
{
    QString envFile = Utils::getUserHomeDir() + "/.dde_env";
    QFile file(envFile);
    if (!file.open(QFile::WriteOnly | QFile::Truncate)) {
        return;
    }

    QString text = QString::asprintf("# DDE user env file, bash script\n");
    for (auto key : userEnvs.keys()) {
        text += QString::asprintf("export %s=%s;\n",
                                  key.toStdString().c_str(),
                                  userEnvs[key].toStdString().c_str());
    }

    file.write(text.toLatin1(), text.length());
    file.close();
}
