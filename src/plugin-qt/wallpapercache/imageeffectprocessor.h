// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef IMAGE_EFFECT_PROCESSOR_H
#define IMAGE_EFFECT_PROCESSOR_H

#include <QObject>
#include <QImage>
#include <QColor>
#include <QString>

/**
 * @brief Image effect processor
 *
 * Handles image effects such as blur and color adjustment.
 * Currently implements the pixmix effect.
 */
class ImageEffectProcessor : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Supported image effect types
     */
    enum EffectType {
        PixmixEffect,       // pixmix blur effect
        // Future effect types can be added here
        // GaussianBlur,    // Gaussian blur
        // ColorAdjust,     // Color adjustment
    };

    explicit ImageEffectProcessor(QObject *parent = nullptr);
    ~ImageEffectProcessor();

    /**
     * @brief Apply the specified image effect
     * @param imagePath Input image path
     * @param effectType Effect type
     * @return Processed image, null QImage on failure
     */
    static QImage applyEffect(const QString &imagePath, EffectType effectType = PixmixEffect);

    /**
     * @brief Apply pixmix effect (compatibility interface)
     * @param imagePath Input image path
     * @return Processed image
     */
    static QImage applyPixmixEffect(const QString &imagePath);

    /**
     * @brief Check if the specified effect is supported
     * @param effectType Effect type
     * @return Whether supported
     */
    static bool isSupportedEffect(EffectType effectType);

    /**
     * @brief Convert effect name string to EffectType
     * @param effectName Effect name (e.g. "pixmix", "")
     * @return Effect type, defaults to PixmixEffect if unsupported
     */
    static EffectType effectTypeFromString(const QString &effectName);

private:
    /**
     * @brief Built-in pixmix algorithm implementation
     * @param imagePath Input image path
     * @return Processed image
     */
    static QImage processPixmixEffect(const QString &imagePath);

    /**
     * @brief Calculate the average color of an image
     * @param image Input image
     * @param sampleSize Sample size (default 16x16)
     * @return Average color
     */
    static QColor calculateAverageColor(const QImage &image, int sampleSize = 16);
};

#endif // IMAGE_EFFECT_PROCESSOR_H
