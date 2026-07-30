// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <vector>

namespace dde::ambient_brightness {

/// lux → 亮度曲线控制点。
struct CurvePoint {
    double lux = 0.0;
    double brightness = 0.0;   // [0.0, 1.0]
};

/// 配置驱动的 lux → 亮度映射。
///
/// 在 log1p(lux) 空间做分段线性插值:
/// - 低于第一个控制点:返回第一个亮度
/// - 高于最后一个控制点:返回最后一个亮度
/// - 中间:log1p 空间线性插值
class BrightnessCurve {
public:
    BrightnessCurve() = default;
    explicit BrightnessCurve(std::vector<CurvePoint> points);

    /// lux → brightness。
    double map(double lux) const;

    /// 校验曲线是否有效(lux 严格递增, brightness 单调不降, 范围 [0,1])。
    static bool isValid(const std::vector<CurvePoint> &points);

    /// 替换曲线。无效则返回 false 且不修改。
    bool set(std::vector<CurvePoint> points);

    const std::vector<CurvePoint> &points() const { return m_points; }

private:
    std::vector<CurvePoint> m_points;
};

} // namespace dde::ambient_brightness
