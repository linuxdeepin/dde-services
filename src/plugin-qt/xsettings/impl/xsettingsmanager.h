// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef XSETTINGSMANAGER_H
#define XSETTINGSMANAGER_H

#include "dconfinfos.h"
#include "modules/api/keyfile.h"
#include "modules/api/xcbutils.h"
#include "modules/common/common.h"
#include "types/arrayOfColor.h"
#include "types/scaleFactors.h"

#include <DConfig>

#include <QDBusInterface>
#include <QObject>
#include <QSharedPointer>
#include <QVector>

class XSettingsManager : public QObject
{
    Q_OBJECT
public:
    XSettingsManager(QObject *parent = nullptr);
    ArrayOfColor getColor(const QString &prop);
    int getInteger(const QString &prop);
    double getScaleFactor();
    ScaleFactors getScreenScaleFactors();
    QString getString(const QString &prop);
    QString listProps();
    void setColor(const QString &prop, const ArrayOfColor &v);
    void setInteger(const QString &prop, const int &v);
    void setScreenScaleFactors(const ScaleFactors &factors, bool emitSignal);
    void setString(const QString &prop, const QString &v);

Q_SIGNALS: // SIGNALS
    void SetScaleFactorDone();
    void SetScaleFactorStarted();

protected Q_SLOTS:
    void handleDConfigChangedCb(const QString &key);

private:
    double getRecommendedScaleFactor();
    double getForceScaleFactor();
    void adjustScaleFactor(double recommendedScaleFactor);
    void updateDPI();
    void updateXResources();
    void updateFirefoxDPI();
    XsValue getSettingValue(QString prop);
    void setSettings(QVector<XsSetting> settings);
    QVector<XsSetting> getSettingsInSchema();
    ScaleFactors parseScreenFactors(QString screenFactors);
    void setGSettingsByXProp(const QString &prop, XsValue value);
    void setSingleScaleFactor(double scale, bool emitSignal);
    void setScaleFactorForPlymouth(int factor, bool emitSignal);
    QString getPlymouthTheme(QString file);
    int getPlymouthThemeScaleFactor(QString theme);
    QString joinScreenScaleFactors(const ScaleFactors &factors);
    void setScreenScaleFactorsForQt(const ScaleFactors &factors);
    void updateGreeterQtTheme(KeyFile &keyFile);
    void cleanUpDdeEnv();
    QMap<QString, QString> loadDDEUserEnv();
    void saveDDEUserEnv(const QMap<QString, QString> &userEnvs);
    QByteArray getSettingPropValue();
    void emitSignalSetScaleFactor(bool done, bool emitSignal);

private:
    DTK_CORE_NAMESPACE::DConfig *m_settingDconfig;
    QSharedPointer<QDBusInterface> m_greeterInterface;
    QSharedPointer<QDBusInterface> m_sysDaemonInterface;
    // QVector<int> m_plymouthScalingTasks; //
    // bool m_plymouthScaling;              //
    // bool m_restartOSD;                   //
    XcbUtils &m_xcbUtils;
    DconfInfos m_dconfInfos;
};

#endif // XSETTINGSMANAGER_H
