// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! deepin-service-manager Rust 插件 ABI 定义。
//!
//! 与 `rustpluginabi.h` 中的 C++ 侧保持一致的 `#[repr(C)]` 结构体和常量，
//! 插件入口通过 `DSMRustStartV1` / `DSMRustStopV1` 与框架交互。

use core::ffi::{c_char, c_void};
use std::mem::size_of;

/// ABI 版本，必须与 C++ 侧 `DSM_RUST_PLUGIN_ABI_VERSION` 一致。
pub const ABI_VERSION: u32 = 1;

/// System bus 标记。
pub const SYSTEM_BUS: u32 = 0;

/// Session bus 标记。
pub const SESSION_BUS: u32 = 1;

/// 框架传入的插件上下文，对应 C++ 侧 `DSMRustPluginContextV1`。
///
/// 字段顺序、大小和对齐必须与头文件完全一致。
#[repr(C)]
pub struct PluginContextV1 {
    pub abi_version: u32,
    pub struct_size: u32,
    pub bus_type: u32,
    pub reserved: u32,
    pub service_name: *const c_char,
    pub service_name_length: usize,
}

/// 从上下文中安全提取服务名。
///
/// 返回 `None` 如果指针为空或字节不是合法 UTF-8。
pub fn service_name(context: &PluginContextV1) -> Option<&str> {
    if context.service_name.is_null() {
        return None;
    }

    // SAFETY: 框架保证 service_name 指向至少 service_name_length 字节的有效内存，
    // 生命周期在 Start 调用期间有效。
    let bytes = unsafe {
        std::slice::from_raw_parts(
            context.service_name.cast::<u8>(),
            context.service_name_length,
        )
    };

    std::str::from_utf8(bytes).ok()
}

/// 校验上下文 ABI 版本和结构体大小是否匹配。
pub fn check_abi(context: &PluginContextV1) -> bool {
    context.abi_version == ABI_VERSION
        && context.struct_size as usize == size_of::<PluginContextV1>()
}

/// 拒绝裸指针的便利别名。
#[allow(dead_code)]
pub type RawHandle = *mut c_void;
