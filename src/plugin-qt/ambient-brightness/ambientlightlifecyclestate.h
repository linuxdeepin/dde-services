// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

namespace dde::ambient_brightness {

/// 决定环境光传感器是否应处于 Claim 状态。
/// 自动亮度开关、合盖和休眠是相互独立的阻断条件；只有全部允许时才运行。
struct AmbientLightLifecycleState {
    bool enabled = true;
    bool lidClosed = false;
    bool sleeping = false;
    bool sessionActive = true;

    constexpr bool shouldRun() const noexcept
    {
        return enabled && !lidClosed && !sleeping && sessionActive;
    }
};

} // namespace dde::ambient_brightness
