// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! PulseAudio 模块管理。
//!
//! 模块加载是异步过程：`load_module` 只是提交请求，真正的完成是
//! 关联的 sink/source 创建出来。用状态机管理模块生命周期。
//!
//! 状态迁移：
//! - `Unloaded` → `Loading`（发起加载）
//! - `Loading` → `Complete`（Sink/Source 创建）
//! - `Loading` → `Unloaded`（失败/超时）
//! - `Complete` → `Unloaded`（卸载）
//!
//! 各模块的加载参数不同，在 [`module_argument`] 中统一编排，
//! 上层通过 [`load`] 接口加载，无需关心参数细节。

use std::time::Instant;

/// 模块名常量。
pub const MODULE_NULL_SINK: &str = "module-null-sink";
pub const MODULE_REMAP_SINK: &str = "module-remap-sink";
pub const MODULE_ECHO_CANCEL: &str = "module-echo-cancel";

/// 加载 null-sink 后创建的 sink 名。
#[allow(dead_code)]
pub const NULL_SINK_NAME: &str = "null-sink";

/// 加载 remap-sink（单声道）后创建的 sink 名。
pub const MONO_SINK_NAME: &str = "mono-sink";

/// 模块超时时间。
#[allow(dead_code)]
const MODULE_LOAD_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(3);

/// 模块状态。
#[derive(Clone, Debug)]
pub enum ModuleState {
    /// 模块未加载（初始状态，也是卸载后的状态）。
    Unloaded,
    /// 已提交加载，等待关联设备创建。
    Loading { #[allow(dead_code)] loaded_at: Instant },
    /// 模块生效，关联设备已创建。
    Complete { #[allow(dead_code)] module_index: u32 },
}

impl Default for ModuleState {
    fn default() -> Self {
        Self::Unloaded
    }
}

impl ModuleState {
    /// 是否处于 Loading 状态且已超时。
    #[allow(dead_code)]
    pub fn is_loading_timed_out(&self) -> bool {
        match self {
            Self::Loading { loaded_at } => loaded_at.elapsed() > MODULE_LOAD_TIMEOUT,
            _ => false,
        }
    }
}

/// 生成模块加载参数。
///
/// 具体参数规则在模块内部编排（以 PipeWire 模块 module.usage 为准）：
/// - null-sink：无参数
/// - remap-sink（单声道）：`sink_name=mono-sink channels=1 channel_map=mono master=<channel>`
/// - echo-cancel（降噪）：同时绑定 `source_master=<channel>` 和 `sink_master=<extra_channel>`
///
/// `channel` 为主绑定设备名，`extra_channel` 为附加绑定设备名（仅 echo-cancel 用）。
pub fn module_argument(
    name: &str,
    channel: Option<&str>,
    extra_channel: Option<&str>,
) -> String {
    match name {
        MODULE_NULL_SINK => String::new(),
        MODULE_REMAP_SINK => {
            format!(
                "sink_name=mono-sink channels=1 channel_map=mono master={}",
                channel.unwrap_or("")
            )
        }
        MODULE_ECHO_CANCEL => {
            format!(
                "source_master={} sink_master={}",
                channel.unwrap_or(""),
                extra_channel.unwrap_or("")
            )
        }
        _ => String::new(),
    }
}
