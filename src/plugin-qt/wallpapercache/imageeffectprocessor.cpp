// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "imageeffectprocessor.h"

#include <QDebug>
#include <QPainter>
#include <QImageReader>
#include <QColor>
#include <QImage>

#include <DGuiApplicationHelper>

// pixmix algorithm parameters
#define PIXMIX_MATRIX       16          // Image sample size
#define PIXMIX_OPACITY      90          // Color opacity
#define PIXMIX_SATURATION   50          // Saturation
#define PIXMIX_BRIGHTNESS   -60         // Brightness

DGUI_USE_NAMESPACE

ImageEffectProcessor::ImageEffectProcessor(QObject *parent)
    : QObject(parent)
{
}

ImageEffectProcessor::~ImageEffectProcessor()
{
}

QImage ImageEffectProcessor::applyEffect(const QString &imagePath, EffectType effectType)
{
    switch (effectType) {
    case PixmixEffect:
        return processPixmixEffect(imagePath);
    default:
        qWarning() << "Unsupported effect type:" << effectType;
        return QImage();
    }
}

QImage ImageEffectProcessor::applyPixmixEffect(const QString &imagePath)
{
    return processPixmixEffect(imagePath);
}

bool ImageEffectProcessor::isSupportedEffect(EffectType effectType)
{
    switch (effectType) {
    case PixmixEffect:
        return true;
    default:
        return false;
    }
}

ImageEffectProcessor::EffectType ImageEffectProcessor::effectTypeFromString(const QString &effectName)
{
    if (effectName.isEmpty() || effectName == "pixmix") {
        return PixmixEffect;
    }
    
    qWarning() << "Unknown effect name:" << effectName << ", fallback to PixmixEffect";
    return PixmixEffect;
}

QImage ImageEffectProcessor::processPixmixEffect(const QString &imagePath)
{
    QImage originalImage;
    if (!originalImage.load(imagePath)) {
        qWarning() << "Failed to load image:" << imagePath;
        return QImage();
    }

    QColor averageColor = calculateAverageColor(originalImage, PIXMIX_MATRIX);
    
    QColor adjustedColor = DGuiApplicationHelper::adjustColor(averageColor,
                                                              0,                    // Hue shift
                                                              PIXMIX_SATURATION,    // Saturation
                                                              PIXMIX_BRIGHTNESS,    // Brightness
                                                              0, 0, 0, 0);

    QImage resultImage = originalImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&resultImage);
    painter.setRenderHints(painter.renderHints() | QPainter::SmoothPixmapTransform);

    adjustedColor.setAlpha(int(PIXMIX_OPACITY * 1.0 / 100 * 255));
    painter.fillRect(resultImage.rect(), adjustedColor);

    painter.end();

    qDebug() << "Applied pixmix effect - original color:" << averageColor
             << "adjusted color:" << adjustedColor
             << "opacity:" << PIXMIX_OPACITY << "%";

    return resultImage;
}

QColor ImageEffectProcessor::calculateAverageColor(const QImage &image, int sampleSize)
{
    QImage sampledImage = image.scaled(sampleSize, sampleSize,
                                       Qt::IgnoreAspectRatio, Qt::FastTransformation);

    int totalR = 0, totalG = 0, totalB = 0;

    for (int i = 0; i < sampleSize; ++i) {
        for (int j = 0; j < sampleSize; ++j) {
            QRgb rgb = sampledImage.pixel(i, j);
            totalR += qRed(rgb);
            totalG += qGreen(rgb);
            totalB += qBlue(rgb);
        }
    }

    int pixelCount = sampleSize * sampleSize;
    return QColor(totalR / pixelCount, totalG / pixelCount, totalB / pixelCount);
}
