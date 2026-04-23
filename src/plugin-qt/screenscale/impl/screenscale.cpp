// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screenscale.h"

#include <DConfig>

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

ScreenScale::ScreenScale(QObject *parent)
    : QObject(parent)
{
    m_config = DTK_CORE_NAMESPACE::DConfig::create(
        "org.deepin.dde.daemon", "org.deepin.dde.ScreenScale1", "", this);
    if (!m_config || !m_config->isValid()) {
        qWarning() << "DConfig instance for ScreenScale is null or invalid";
        m_config = nullptr;
    }
}

/*
测试入参示例：

1. 单屏 1920x1080:
[{"widthPx":1920,"heightPx":1080,"widthMm":477,"heightMm":268}]

2. 单屏 4K:
[{"widthPx":3840,"heightPx":2160,"widthMm":597,"heightMm":336}]

3. 笔记本 14英寸:
[{"widthPx":1920,"heightPx":1080,"widthMm":310,"heightMm":174}]

4. 多屏 (笔记本 + 外接显示器):
[{"widthPx":1920,"heightPx":1080,"widthMm":310,"heightMm":174},{"widthPx":2560,"heightPx":1440,"widthMm":597,"heightMm":336}]
*/
QString ScreenScale::GetScreenScaleInfo(const QString &screensJson)
{
    qDebug() << "GetScreenScaleInfo input:" << screensJson;

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(screensJson.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse screens JSON:" << error.errorString();
        QString result = R"({"current":1.0,"recommended":1.0,"available":[1.0,1.25]})";
        qDebug() << "GetScreenScaleInfo output:" << result;
        return result;
    }

    QJsonArray screens = doc.array();
    if (screens.isEmpty()) {
        QString result = R"({"current":1.0,"recommended":1.0,"available":[1.0,1.25]})";
        qDebug() << "GetScreenScaleInfo output:" << result;
        return result;
    }

    double step = getScaleStep();

    // 计算推荐缩放
    double recommended = 3.0;
    for (const auto &screenVal : screens) {
        QJsonObject screen = screenVal.toObject();
        double scale = calcRecommendedScale(
            screen["widthPx"].toDouble(),
            screen["heightPx"].toDouble(),
            screen["widthMm"].toDouble(),
            screen["heightMm"].toDouble(),
            step);
        if (scale < recommended) {
            recommended = scale;
        }
    }

    // 计算可用缩放列表（确保缩放后有效分辨率不小于 1024x768）
    double maxScale = 3.0;
    for (const auto &screenVal : screens) {
        QJsonObject screen = screenVal.toObject();
        double widthPx = screen["widthPx"].toDouble();
        double heightPx = screen["heightPx"].toDouble();

        double limit = std::min(widthPx / 1024.0, heightPx / 768.0);
        if (limit < maxScale) {
            maxScale = limit;
        }
    }
    maxScale = std::clamp(std::floor(maxScale / step) * step, 1.0, 3.0);

    QJsonArray available;
    int count = static_cast<int>((maxScale - 1.0 + 0.0001) / step);
    for (int i = 0; i <= count; ++i) {
        available.append(1.0 + i * step);
    }

    // 确定当前缩放值
    double current = recommended;
    if (m_config && m_config->isValid()) {
        double configured = m_config->value("scaleFactor").toDouble();
        if (configured > 0.0) {
            for (const auto &v : available) {
                if (qFuzzyCompare(v.toDouble(), configured)) {
                    current = configured;
                    break;
                }
            }
        }
    }

    QJsonObject result;
    result["current"] = current;
    result["recommended"] = recommended;
    result["available"] = available;

    QString output = QJsonDocument(result).toJson(QJsonDocument::Compact);
    qDebug() << "GetScreenScaleInfo output:" << output;
    return output;
}

void ScreenScale::SetScaleFactor(double factor)
{
    qDebug() << "SetScaleFactor input:" << factor;

    if (!m_config || !m_config->isValid()) {
        return;
    }

    if (std::isnan(factor) || std::isinf(factor) || factor < 1.0 || factor > 3.0) {
        qWarning() << "Invalid scale factor:" << factor;
        if (calledFromDBus()) {
            sendErrorReply("org.deepin.dde.ScreenScale1.Error.InvalidParameter",
                           "Invalid scale factor. Must be between 1.0 and 3.0.");
        }
        return;
    }

    double current = m_config->value("scaleFactor").toDouble();
    if (qFuzzyCompare(current, factor)) {
        qDebug() << "Scale factor unchanged:" << factor;
        return;
    }

    m_config->setValue("scaleFactor", factor);
    qDebug() << "Scale factor set to:" << factor;
    Q_EMIT ScaleFactorChanged(factor);
}

double ScreenScale::getScaleStep() const
{
    double step = 0.25;
    if (m_config && m_config->isValid()) {
        step = m_config->value("scaleStep", 0.25).toDouble();
    }
    return step > 0 ? step : 0.25;
}

double ScreenScale::calcRecommendedScale(
    double widthPx, double heightPx, double widthMm, double heightMm, double step) const
{
    if (widthMm <= 0.0 || heightMm <= 0.0) {
        return 1.0;
    }

    double lenPx = std::sqrt(widthPx * widthPx + heightPx * heightPx);
    double lenMm = std::sqrt(widthMm * widthMm + heightMm * heightMm);

    // 标准 1080p 21.5 英寸显示器
    double lenPxStd = std::sqrt(1920.0 * 1920.0 + 1080.0 * 1080.0);
    double lenMmStd = std::sqrt(477.0 * 477.0 + 268.0 * 268.0);

    double a = 0.00158; // 经验修正系数
    double fix = (lenMm - lenMmStd) * (lenPx / lenPxStd) * a;
    double scaleFactor = (lenPx / lenMm) / (lenPxStd / lenMmStd) + fix;

    // 对齐到 step
    return std::clamp(std::round(scaleFactor / step) * step, 1.0, 3.0);
}
