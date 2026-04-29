// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef SCREEN_SCALE_H
#define SCREEN_SCALE_H

#include <DConfig>

#include <QDBusContext>
#include <QList>
#include <QObject>

class ScreenScale : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.ScreenScale1")

public:
    explicit ScreenScale(QObject *parent = nullptr);
    ~ScreenScale() override = default;

public Q_SLOTS:
    // 获取缩放信息，返回 JSON: {"current":1.5,"recommended":1.5,"available":[1.0,1.25,1.5,1.75,2.0]}
    // current: 配置值在可用范围内则返回配置值，否则返回推荐值
    // 入参 JSON 格式: [{"widthPx":1920,"heightPx":1080,"widthMm":477,"heightMm":268}, ...]
    QString GetScreenScaleInfo(const QString &screensJson);

    // 设置缩放值 (1.0 ~ 3.0)
    void SetScaleFactor(double factor);

Q_SIGNALS:
    void ScaleFactorChanged(double factor);

private:
    double getScaleStep() const;
    double calcRecommendedScale(
        double widthPx, double heightPx, double widthMm, double heightMm, double step) const;

    DTK_CORE_NAMESPACE::DConfig *m_config = nullptr;
};

#endif // SCREEN_SCALE_H
