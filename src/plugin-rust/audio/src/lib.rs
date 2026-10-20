// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! dde audio Rust 插件入口。
//!
//! 编译为 `cdylib`，由 deepin-service-manager 的 Rust 加载后端通过
//! `DSMRustStartV1` / `DSMRustStopV1` ABI 加载和卸载。
//!
//! 插件使用 zbus 在 session/system bus 上注册 `org.deepin.dde.Audio1` 服务，
//! 持有 blocking Connection 以保持服务存活。

mod abi;
pub mod backend;
pub mod manager;

use core::ffi::c_void;
use std::panic::{AssertUnwindSafe, catch_unwind};

use zbus::blocking::{Connection, connection};

use abi::{PluginContextV1, SESSION_BUS, SYSTEM_BUS, check_abi, service_name};
use manager::audio::Audio;
use manager::{AudioManager, DBUS_PATH};

/// 插件运行时状态，在 Stop 时释放。
struct PluginState {
    connection: Option<Connection>,
}

/// 启动插件：创建 D-Bus 连接、申请服务名、注册 Audio 对象。
fn start_plugin(context: &PluginContextV1) -> Result<PluginState, i32> {
    if !check_abi(context) {
        eprintln!("[dde-audio] unsupported Rust plugin ABI");
        return Err(-1);
    }

    let name = service_name(context).ok_or_else(|| {
        eprintln!("[dde-audio] invalid service name");
        -1
    })?;

    let builder = match context.bus_type {
        SYSTEM_BUS => connection::Builder::system(),
        SESSION_BUS => connection::Builder::session(),
        value => {
            eprintln!("[dde-audio] unsupported D-Bus type: {value}");
            return Err(-1);
        }
    }
    .map_err(|e| {
        eprintln!("[dde-audio] failed to create D-Bus builder: {e}");
        -1
    })?;

    let connection = builder
        .name(name)
        .map_err(|e| {
            eprintln!("[dde-audio] failed to configure D-Bus name: {e}");
            -1
        })?
        .build()
        .map_err(|e| {
            eprintln!("[dde-audio] failed to build D-Bus connection: {e}");
            -1
        })?;

    // 声卡事件回调的弱引用槽：EventLoop 事件触发 auto_switch_ports
    let manager_slot: std::sync::Arc<parking_lot::RwLock<Option<std::sync::Weak<AudioManager>>>> =
        std::sync::Arc::new(parking_lot::RwLock::new(None));

    let manager = AudioManager::new(connection.clone(), manager_slot.clone()).map_err(|e| {
        eprintln!("[dde-audio] failed to create audio manager: {e}");
        -1
    })?;
    let manager = std::sync::Arc::new(manager);
    *manager_slot.write() = Some(std::sync::Arc::downgrade(&manager));

    connection
        .object_server()
        .at(DBUS_PATH, Audio::new(manager.clone(), manager.device_manager().clone()))
        .map_err(|e| {
            eprintln!("[dde-audio] failed to register Audio object: {e}");
            -1
        })?;

    Ok(PluginState {
        connection: Some(connection),
    })
}

/// 框架入口：启动插件。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn DSMRustStartV1(
    context: *const PluginContextV1,
    plugin_handle: *mut *mut c_void,
) -> i32 {
    if context.is_null() || plugin_handle.is_null() {
        return -1;
    }

    match catch_unwind(AssertUnwindSafe(|| {
        let state = start_plugin(unsafe { &*context })?;
        let handle = Box::into_raw(Box::new(state)).cast::<c_void>();
        unsafe {
            *plugin_handle = handle;
        }
        Ok::<(), i32>(())
    })) {
        Ok(Ok(())) => 0,
        Ok(Err(code)) => code,
        Err(_) => -1,
    }
}

/// 框架入口：停止插件，释放 D-Bus 连接。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn DSMRustStopV1(plugin_handle: *mut c_void) -> i32 {
    if plugin_handle.is_null() {
        return -1;
    }

    match catch_unwind(AssertUnwindSafe(|| {
        let mut state = unsafe { Box::from_raw(plugin_handle.cast::<PluginState>()) };
        if let Some(connection) = state.connection.take() {
            connection.close().map_err(|e| {
                eprintln!("[dde-audio] failed to close D-Bus connection: {e}");
                -1
            })?;
        }
        Ok::<(), i32>(())
    })) {
        Ok(Ok(())) => 0,
        Ok(Err(code)) => code,
        Err(_) => -1,
    }
}

