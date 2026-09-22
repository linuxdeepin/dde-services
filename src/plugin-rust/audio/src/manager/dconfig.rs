// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! dconfig 对接：读取端口类型优先级顺序并监听变更。
//!
//! dconfig 服务 `org.desktopspec.ConfigManager` 在系统总线上提供
//! `org.deepin.dde.daemon.audio` 配置。其中
//! `outputDefaultPrioritiesByType` / `inputDefaultPrioritiesByType`
//! 为 `int64` 数组，元素是 `PortType` 的整数编码，列表顺序即优先级
//! （前=高）。本模块将这些值映射为 `PortPriority.type_order`。
//!
//! 流程：`acquireManager` 获取 app manager 对象路径 → 读两个键 →
//! 应用；再监听 manager 的 `valueChanged` 信号，键变更时重读并回调。
//!
//! dconfig 在系统总线上，插件在独立线程用独立 `Connection::system()`
//! 通信，不阻塞主 D-Bus 服务线程。

use std::sync::Arc;
use std::thread;

use zbus::blocking::{Connection, Proxy, proxy::Builder as ProxyBuilder};
use zbus::zvariant::{OwnedObjectPath, OwnedValue};

use crate::manager::device_type::PortType;

/// dconfig 服务名。
const DCONFIG_DEST: &str = "org.desktopspec.ConfigManager";
/// 管理接口根对象路径（acquireManager 所在；服务本身在 `/`）。
const DCONFIG_ROOT_PATH: &str = "/";
const DCONFIG_APP_ID: &str = "org.deepin.dde.daemon";
const DCONFIG_MODULE: &str = "org.deepin.dde.daemon.audio";
/// 类型优先级键。
const KEY_OUTPUT: &str = "outputDefaultPrioritiesByType";
const KEY_INPUT: &str = "inputDefaultPrioritiesByType";
/// 音量增强键（开启时 MaxUIVolume=1.5）。
pub const KEY_VOLUME_INCREASE: &str = "volumeIncrease";
/// 降噪开关键。
pub const KEY_REDUCE_NOISE: &str = "reduceNoiseEnabled";
/// 插拔暂停播放键。
pub const KEY_PAUSE_PLAYER: &str = "pausePlayer";
/// 单声道开关键。
pub const KEY_MONO: &str = "monoEnabled";
/// Manager 接口名。
const MANAGER_IFACE: &str = "org.desktopspec.ConfigManager.Manager";
/// 根接口名。
const ROOT_IFACE: &str = "org.desktopspec.ConfigManager";

/// 从 dconfig 读取的类型优先级顺序（输出/输入）。
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct TypeOrderConfig {
    pub output: Vec<PortType>,
    pub input: Vec<PortType>,
}

/// 将 `int64` 编码列表转为 `PortType` 列表，忽略无效/Unknown 编码。
fn codes_to_types(codes: &[i64]) -> Vec<PortType> {
    codes
        .iter()
        .map(|c| PortType::from_u32(*c as u32))
        .filter(|t| *t != PortType::Unknown)
        .collect()
}

/// 构建一个 blocking proxy（借用 `conn`/`path`/`interface` 返回，非跨线程）。
fn build_proxy<'a>(
    conn: &Connection,
    path: &'a str,
    interface: &'a str,
) -> Result<Proxy<'a>, String> {
    let builder = ProxyBuilder::new(conn)
        .destination(DCONFIG_DEST)
        .map_err(|e| format!("dconfig destination: {e}"))?;
    let builder = builder.path(path).map_err(|e| format!("dconfig path: {e}"))?;
    let builder = builder
        .interface(interface)
        .map_err(|e| format!("dconfig iface: {e}"))?;
    builder
        .build()
        .map_err(|e| format!("dconfig proxy build: {e}"))
}

/// 获取 app manager 对象路径。
///
/// `acquireManager(appId, module, user)`：`user` 传空串表示当前用户。
fn acquire_manager(conn: &Connection) -> Result<OwnedObjectPath, String> {
    let proxy = build_proxy(conn, DCONFIG_ROOT_PATH, ROOT_IFACE)?;
    let msg = proxy
        .call_method("acquireManager", &(DCONFIG_APP_ID, DCONFIG_MODULE, ""))
        .map_err(|e| format!("dconfig acquireManager: {e}"))?;
    let path: OwnedObjectPath = msg
        .body()
        .deserialize()
        .map_err(|e| format!("dconfig acquireManager decode: {e}"))?;
    Ok(path)
}

/// 读取某方向的类型优先级顺序。
///
/// `manager_path` 为 acquireManager 返回的 app manager 路径。
fn read_type_order(
    conn: &Connection,
    manager_path: &OwnedObjectPath,
    key: &str,
) -> Result<Vec<PortType>, String> {
    let proxy = build_proxy(conn, manager_path.as_str(), MANAGER_IFACE)?;
    let msg = proxy
        .call_method("value", &(key,))
        .map_err(|e| format!("dconfig value({key}): {e}"))?;
    let v: OwnedValue = msg
        .body()
        .deserialize()
        .map_err(|e| format!("dconfig value({key}) decode: {e}"))?;
    let codes =
        Vec::<i64>::try_from(v).map_err(|e| format!("dconfig value({key}) not int64 list: {e}"))?;
    Ok(codes_to_types(&codes))
}

/// 一次性读取类型优先级配置。失败说明 dconfig 不可用或配置非法，
/// 调用方应静默回退默认值。
pub fn load_type_order(conn: &Connection) -> Result<TypeOrderConfig, String> {
    let manager_path = acquire_manager(conn)?;
    Ok(TypeOrderConfig {
        output: read_type_order(conn, &manager_path, KEY_OUTPUT)?,
        input: read_type_order(conn, &manager_path, KEY_INPUT)?,
    })
}

/// 读取单个 bool 配置值。
///
/// 失败说明 dconfig 不可用或配置非法，调用方应静默回退默认值。
pub fn load_bool(conn: &Connection, key: &str) -> Result<bool, String> {
    let manager_path = acquire_manager(conn)?;
    let proxy = build_proxy(conn, manager_path.as_str(), MANAGER_IFACE)?;
    let msg = proxy
        .call_method("value", &(key,))
        .map_err(|e| format!("dconfig value({key}): {e}"))?;
    let v: OwnedValue = msg
        .body()
        .deserialize()
        .map_err(|e| format!("dconfig value({key}) decode: {e}"))?;
    bool::try_from(v).map_err(|e| format!("dconfig value({key}) not bool: {e}"))
}

/// 写入单个 bool 配置值。
pub fn set_bool(conn: &Connection, key: &str, value: bool) -> Result<(), String> {
    let manager_path = acquire_manager(conn)?;
    let proxy = build_proxy(conn, manager_path.as_str(), MANAGER_IFACE)?;
    // dconfig setValue 签名是 sv（第二个参数为 variant），需用 OwnedValue 包裹
    let wrapped: OwnedValue = OwnedValue::try_from(zbus::zvariant::Value::from(value))
        .map_err(|e| format!("dconfig setValue({key}) wrap: {e}"))?;
    proxy
        .call_method("setValue", &(key, wrapped))
        .map_err(|e| format!("dconfig setValue({key}): {e}"))?;
    Ok(())
}

/// 订阅 manager 的 `valueChanged` 信号并回调。
///
/// `apply` 收到完整 TypeOrderConfig 时调用（任意线程）。连接 move 进
/// 线程以保活，返回 JoinHandle 由调用方持有。
pub fn subscribe(
    conn: Connection,
    apply: Arc<dyn Fn(TypeOrderConfig) + Send + Sync>,
) -> Result<thread::JoinHandle<()>, String> {
    let manager_path = acquire_manager(&conn)?;

    let handle = thread::spawn(move || {
        // 线程内构建 proxy：借用闭包内拥有的 conn 与 path，跨迭代存活
        let proxy = match build_proxy(&conn, manager_path.as_str(), MANAGER_IFACE) {
            Ok(p) => p,
            Err(e) => {
                eprintln!("[dde-audio] dconfig manager proxy: {e}");
                return;
            }
        };
        let iter = match proxy.receive_signal("valueChanged") {
            Ok(it) => it,
            Err(e) => {
                eprintln!("[dde-audio] dconfig valueChanged subscribe failed: {e}");
                return;
            }
        };
        for msg in iter {
            // 解出 key（valueChanged 签名: s）
            let body = msg.body();
            let key: String = match body.deserialize() {
                Ok(k) => k,
                Err(_) => continue,
            };
            if key != KEY_OUTPUT && key != KEY_INPUT {
                continue;
            }
            // 重读全部两个键（可能两者联动），成功则回调
            match load_type_order(&conn) {
                Ok(cfg) => apply(cfg),
                Err(e) => eprintln!("[dde-audio] dconfig reload failed: {e}"),
            }
        }
    });

    Ok(handle)
}
