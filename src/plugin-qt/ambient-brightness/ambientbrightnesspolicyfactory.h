// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "ambientbrightnesspolicy.h"

#include <memory>
#include <optional>
#include <QString>

namespace Dtk::Core {
class DConfig;
}

namespace dde::ambient_brightness {

/// 亮度算法标识。当前仅 continuous；保留枚举与 Factory 扩展点,
/// 未来新增算法时在此添加枚举值并在 Factory 注册创建分支。
enum class BrightnessAlgorithm {
    Continuous,
};

std::optional<BrightnessAlgorithm> parseBrightnessAlgorithm(const QString &value);
QString brightnessAlgorithmName(BrightnessAlgorithm algorithm);

/// 创建默认配置的策略实例（测试与回退路径）。
std::unique_ptr<AmbientBrightnessPolicy> createAmbientBrightnessPolicy(BrightnessAlgorithm algorithm);

/// 创建策略并从 DConfig 注入运行时配置。config 为空时等价于无配置版本。
/// 配置读取失败/字段非法时各策略自行回退到默认值，保证可用。
std::unique_ptr<AmbientBrightnessPolicy> createAmbientBrightnessPolicy(BrightnessAlgorithm algorithm,
                                                                       Dtk::Core::DConfig *config);

} // namespace dde::ambient_brightness
