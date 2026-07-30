// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "brightnesscurve.h"

#include <algorithm>
#include <cmath>

namespace dde::ambient_brightness {

BrightnessCurve::BrightnessCurve(std::vector<CurvePoint> points)
{
    if (isValid(points))
        m_points = std::move(points);
}

bool BrightnessCurve::isValid(const std::vector<CurvePoint> &points)
{
    if (points.size() < 2)
        return false;
    for (size_t i = 0; i < points.size(); ++i) {
        const auto &p = points[i];
        if (!std::isfinite(p.lux) || !std::isfinite(p.brightness)
            || p.lux < 0.0 || p.brightness < 0.0 || p.brightness > 1.0) {
            return false;
        }
        if (i > 0 && (p.lux <= points[i - 1].lux
                      || p.brightness < points[i - 1].brightness)) {
            return false;
        }
    }
    return true;
}

bool BrightnessCurve::set(std::vector<CurvePoint> points)
{
    if (!isValid(points))
        return false;
    m_points = std::move(points);
    return true;
}

double BrightnessCurve::map(double lux) const
{
    if (m_points.empty())
        return 0.0;
    if (lux <= m_points.front().lux)
        return m_points.front().brightness;
    if (lux >= m_points.back().lux)
        return m_points.back().brightness;

    const auto upper = std::upper_bound(m_points.begin(), m_points.end(), lux,
                                         [](double v, const CurvePoint &p) {
                                             return v < p.lux;
                                         });
    const auto &high = *upper;
    const auto &low = *(upper - 1);
    const double lowLog = std::log1p(low.lux);
    const double highLog = std::log1p(high.lux);
    const double ratio = (std::log1p(lux) - lowLog) / (highLog - lowLog);
    return low.brightness + ratio * (high.brightness - low.brightness);
}

} // namespace dde::ambient_brightness
