// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! 事件循环。
//!
//! 在独立线程上消费 `PulseEvent` channel，调用各模块的 new/update/delete
//! 更新 DeviceManager，动态注册/注销 D-Bus 子对象，并发送属性变更信号。

use std::collections::HashMap;
use std::sync::Arc;
use std::thread;

use parking_lot::RwLock;
use zbus::zvariant::Value;

use crate::backend::pulse::{PulseEvent, PulseManager};

use super::card;
use super::device_manager::DeviceManager;
use super::sink::SinkInterface;
use super::sink_input::SinkInputInterface;
use super::source::SourceInterface;
use super::DBUS_PATH;

/// 发送 org.freedesktop.DBus.Properties.PropertiesChanged 信号。
///
/// `changed`：发生变化的属性（名 → 新值）。
/// `invalidated`：需要客户端重新读取的属性名。
///
/// 注意：控制中心使用 DTK `DDBusInterface`，其 `onPropertiesChanged`
/// 只处理带值的 changed map（`Q_UNUSED(invalidatedProperties)` 忽略
/// invalidated 列表）。因此所有属性变化必须走带值通道，否则控制中心
/// 收不到任何更新。
fn emit_properties_changed(
    connection: &zbus::blocking::Connection,
    path: &str,
    interface_name: &str,
    changed: &HashMap<&str, Value<'_>>,
    invalidated: &[&str],
) {
    let path: zbus::zvariant::ObjectPath = match path.try_into() {
        Ok(p) => p,
        Err(_) => return,
    };
    let interface_name: zbus::names::InterfaceName = match interface_name.try_into() {
        Ok(n) => n,
        Err(_) => return,
    };

    // PropertiesChanged 信号 body: (interface_name, changed_props, invalidated_props)
    let body = (
        interface_name,
        changed,
        invalidated.to_vec(),
    );
    let _ = connection.emit_signal(
        None::<&str>,
        path,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        &body,
    );
}

/// Sink 对象路径。
fn sink_path(index: u32) -> zbus::zvariant::OwnedObjectPath {
    zbus::zvariant::ObjectPath::try_from(format!("/org/deepin/dde/Audio1/Sink{index}"))
        .unwrap()
        .into()
}

/// Source 对象路径。
fn source_path(index: u32) -> zbus::zvariant::OwnedObjectPath {
    zbus::zvariant::ObjectPath::try_from(format!("/org/deepin/dde/Audio1/Source{index}"))
        .unwrap()
        .into()
}

/// SinkInput 对象路径。
fn sink_input_path(index: u32) -> zbus::zvariant::OwnedObjectPath {
    zbus::zvariant::ObjectPath::try_from(format!("/org/deepin/dde/Audio1/SinkInput{index}"))
        .unwrap()
        .into()
}

/// 默认 Sink 对象路径（无则空路径）。
fn default_sink_path(dm: &DeviceManager) -> zbus::zvariant::OwnedObjectPath {
    dm.default_sink
        .as_deref()
        .and_then(|name| dm.find_sink_index_by_name(name))
        .map(sink_path)
        .unwrap_or_default()
}

/// 默认 Source 对象路径（无则空路径）。
fn default_source_path(dm: &DeviceManager) -> zbus::zvariant::OwnedObjectPath {
    dm.default_source
        .as_deref()
        .and_then(|name| dm.find_source_index_by_name(name))
        .map(source_path)
        .unwrap_or_default()
}

/// 构造 Audio 主接口属性的带值 changed map。
///
/// 只返回 `props` 中能被构造的值（DTK 需要带值才转发）。
fn audio_prop_values<'a>(
    dm: &DeviceManager,
    props: &[&'a str],
) -> HashMap<&'a str, Value<'static>> {
    let mut changed = HashMap::new();
    for prop in props {
        match *prop {
            "Sinks" => {
                let v: Vec<zbus::zvariant::OwnedObjectPath> =
                    dm.sinks.keys().map(|i| sink_path(*i)).collect();
                changed.insert("Sinks", Value::from(v));
            }
            "Sources" => {
                let v: Vec<zbus::zvariant::OwnedObjectPath> =
                    dm.sources.keys().map(|i| source_path(*i)).collect();
                changed.insert("Sources", Value::from(v));
            }
            "SinkInputs" => {
                let v: Vec<zbus::zvariant::OwnedObjectPath> = dm
                    .visible_sink_input_indices()
                    .into_iter()
                    .map(sink_input_path)
                    .collect();
                changed.insert("SinkInputs", Value::from(v));
            }
            "DefaultSink" => {
                changed.insert("DefaultSink", Value::from(default_sink_path(dm)));
            }
            "DefaultSource" => {
                changed.insert("DefaultSource", Value::from(default_source_path(dm)));
            }
            "Cards" => {
                changed.insert("Cards", Value::from(dm.cards_json()));
            }
            "CardsWithoutUnavailable" => {
                changed.insert(
                    "CardsWithoutUnavailable",
                    Value::from(dm.cards_without_unavailable_json()),
                );
            }
            _ => {}
        }
    }
    changed
}

/// 设备增删后通知 Audio 对象列表属性变化（带值发送）。
fn emit_audio_list_changed(
    connection: &zbus::blocking::Connection,
    device_manager: &Arc<RwLock<DeviceManager>>,
    props: &[&str],
) {
    let dm = device_manager.read();
    let changed = audio_prop_values(&dm, props);
    drop(dm);
    emit_properties_changed(
        connection,
        DBUS_PATH,
        "org.deepin.dde.Audio1",
        &changed,
        &[],
    );
}

/// Sink 属性的带值 changed map（属性名 → 当前值）。
fn sink_prop_values<'a>(dm: &DeviceManager, index: u32, props: &[&'a str]) -> HashMap<&'a str, Value<'static>> {
    let mut changed = HashMap::new();
    let Some(s) = dm.sinks.get(&index) else {
        return changed;
    };
    for prop in props {
        match *prop {
            "Name" => changed.insert("Name", Value::from(s.name.clone())),
            "Description" => changed.insert("Description", Value::from(s.description.clone())),
            "BaseVolume" => changed.insert("BaseVolume", Value::from(s.base_volume)),
            "Mute" => changed.insert("Mute", Value::from(s.mute)),
            "Volume" => changed.insert("Volume", Value::from(s.volume)),
            "Balance" => changed.insert("Balance", Value::from(s.balance)),
            "SupportBalance" => changed.insert("SupportBalance", Value::from(s.support_balance)),
            "Fade" => changed.insert("Fade", Value::from(s.fade)),
            "SupportFade" => changed.insert("SupportFade", Value::from(s.support_fade)),
            "Card" => changed.insert("Card", Value::from(s.card)),
            "ActivePort" => changed.insert(
                "ActivePort",
                Value::from((
                    s.active_port.name.clone(),
                    s.active_port.description.clone(),
                    s.active_port.available,
                )),
            ),
            _ => None,
        };
    }
    changed
}

/// Source 属性的带值 changed map（属性名 → 当前值）。
fn source_prop_values<'a>(dm: &DeviceManager, index: u32, props: &[&'a str]) -> HashMap<&'a str, Value<'static>> {
    let mut changed = HashMap::new();
    let Some(s) = dm.sources.get(&index) else {
        return changed;
    };
    for prop in props {
        match *prop {
            "Name" => changed.insert("Name", Value::from(s.name.clone())),
            "Description" => changed.insert("Description", Value::from(s.description.clone())),
            "BaseVolume" => changed.insert("BaseVolume", Value::from(s.base_volume)),
            "Mute" => changed.insert("Mute", Value::from(s.mute)),
            "Volume" => changed.insert("Volume", Value::from(s.volume)),
            "Balance" => changed.insert("Balance", Value::from(s.balance)),
            "SupportBalance" => changed.insert("SupportBalance", Value::from(s.support_balance)),
            "Fade" => changed.insert("Fade", Value::from(s.fade)),
            "SupportFade" => changed.insert("SupportFade", Value::from(s.support_fade)),
            "Card" => changed.insert("Card", Value::from(s.card)),
            "ActivePort" => changed.insert(
                "ActivePort",
                Value::from((
                    s.active_port.name.clone(),
                    s.active_port.description.clone(),
                    s.active_port.available,
                )),
            ),
            _ => None,
        };
    }
    changed
}

/// SinkInput 属性的带值 changed map（属性名 → 当前值）。
fn sink_input_prop_values<'a>(dm: &DeviceManager, index: u32, props: &[&'a str]) -> HashMap<&'a str, Value<'static>> {
    let mut changed = HashMap::new();
    let Some(s) = dm.sink_inputs.get(&index) else {
        return changed;
    };
    for prop in props {
        match *prop {
            "Name" => changed.insert("Name", Value::from(s.name.clone())),
            "Mute" => changed.insert("Mute", Value::from(s.mute)),
            "Volume" => changed.insert("Volume", Value::from(s.volume)),
            "Balance" => changed.insert("Balance", Value::from(s.balance)),
            "SupportBalance" => changed.insert("SupportBalance", Value::from(s.support_balance)),
            "Fade" => changed.insert("Fade", Value::from(s.fade)),
            "SupportFade" => changed.insert("SupportFade", Value::from(s.support_fade)),
            _ => None,
        };
    }
    changed
}

/// 子对象更新后通知其属性变化（带值发送）。
fn emit_device_changed(
    connection: &zbus::blocking::Connection,
    device_manager: &Arc<RwLock<DeviceManager>>,
    path: &str,
    interface_name: &str,
    kind: DeviceKind,
    index: u32,
    props: &[&str],
) {
    let dm = device_manager.read();
    let changed = match kind {
        DeviceKind::Sink => sink_prop_values(&dm, index, props),
        DeviceKind::Source => source_prop_values(&dm, index, props),
        DeviceKind::SinkInput => sink_input_prop_values(&dm, index, props),
    };
    drop(dm);
    emit_properties_changed(connection, path, interface_name, &changed, &[]);
}

/// 子对象设备类型（信号带值构造用）。
#[derive(Clone, Copy)]
enum DeviceKind {
    Sink,
    Source,
    SinkInput,
}

/// 设备创建后通知声卡模块检查 pending profile 完成。
fn try_complete_pending_profile(
    device_manager: &Arc<RwLock<DeviceManager>>,
    device_index: u32,
    is_sink: bool,
    switch_tx: &crossbeam_channel::Sender<()>,
) {
    card::on_device_created(device_manager, device_index, is_sink);
    // 异步触发自动切换（worker 线程执行），见 `start` 注释
    let _ = switch_tx.try_send(());
}

/// 事件循环管理器。
pub struct EventLoop {
    handle: Option<thread::JoinHandle<()>>,
    /// 自动切换 worker 线程（消费卡事件触发的异步切换请求）。
    worker_handle: Option<thread::JoinHandle<()>>,
    /// 关闭信号（Drop 时置位，线程轮询退出）。
    shutdown: std::sync::Arc<std::sync::atomic::AtomicBool>,
}
impl EventLoop {
    /// 启动事件消费线程。
    ///
    /// `sound_effect` 注入新建的 Sink/Source/SinkInput 子对象，供其
    /// `SetVolume` 等方法在 `isPlay` 为真时播放反馈音。
    pub fn start(
        pulse: Arc<PulseManager>,
        device_manager: Arc<RwLock<DeviceManager>>,
        connection: zbus::blocking::Connection,
        events: crossbeam_channel::Receiver<PulseEvent>,
        on_card_event: Option<Arc<dyn Fn() + Send + Sync>>,
        config: Arc<super::config::AudioConfig>,
        sound_effect: Arc<super::sound_effect::SoundEffect>,
    ) -> Self {
        // 自动切换 worker 线程：
        // EventLoop 线程只做轻量状态更新，真正的 auto_switch_ports
        // （可能 op.wait 数秒）由独立线程执行。否则 EventLoop 阻塞在
        // op.wait 上，等不到所需的设备重建事件（self-deadlock），
        // 并阻塞全部后续 Pulse 事件消费。
        //
        // 触发采用「debounce 合并」：首个触发唤醒 worker，随后在
        // DEBOUNCE 窗口内吸收所有新触发；窗口期无新触发才执行一次
        const DEBOUNCE: std::time::Duration = std::time::Duration::from_millis(200);
        let shutdown = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
        let (switch_tx, switch_rx) = crossbeam_channel::bounded(1);
        let worker_cb = on_card_event.clone();
        let worker_shutdown = shutdown.clone();
        let worker_handle = thread::spawn(move || {
            loop {
                // 等待首个触发；channel 关闭或收到关闭信号则退出
                let first = if worker_shutdown.load(std::sync::atomic::Ordering::SeqCst) {
                    None
                } else {
                    match switch_rx.recv_timeout(std::time::Duration::from_millis(100)) {
                        Ok(()) => Some(()),
                        Err(crossbeam_channel::RecvTimeoutError::Timeout) => {
                            // 继续轮询关闭信号
                            continue;
                        }
                        Err(crossbeam_channel::RecvTimeoutError::Disconnected) => None,
                    }
                };
                if first.is_none() {
                    break;
                }
                // 吸收窗口期内的后续触发
                while worker_shutdown.load(std::sync::atomic::Ordering::SeqCst) == false
                    && switch_rx.recv_timeout(DEBOUNCE).is_ok()
                {}
                // 窗口期无新触发，执行一次自动切换（用最新设备状态）
                if !worker_shutdown.load(std::sync::atomic::Ordering::SeqCst) {
                    if let Some(cb) = &worker_cb {
                        cb();
                    }
                }
            }
            eprintln!("[dde-audio] auto-switch worker exited");
        });
        let event_shutdown = shutdown.clone();
        let event_config = config.clone();
        let event_sound_effect = sound_effect.clone();
        let handle = thread::spawn(move || {
            loop {
                // 轮询：收到关闭信号则退出；否则等事件（100ms 超时以响应关闭）
                if event_shutdown.load(std::sync::atomic::Ordering::SeqCst) {
                    break;
                }
                let event = match events.recv_timeout(std::time::Duration::from_millis(100)) {
                    Ok(ev) => ev,
                    Err(crossbeam_channel::RecvTimeoutError::Timeout) => continue,
                    Err(crossbeam_channel::RecvTimeoutError::Disconnected) => break,
                };
                match event {
                    PulseEvent::SinkAdded { index } => {
                        let _ = SinkInterface::new(&pulse, &device_manager, &connection, event_config.clone(), event_sound_effect.clone(), index);
                        try_complete_pending_profile(&device_manager, index, true, &switch_tx);
                        emit_audio_list_changed(&connection, &device_manager, &["Sinks"]);
                    }
                    PulseEvent::SinkChanged { index } => {
                        let _ = SinkInterface::update(&pulse, &device_manager, index);
                        let path = SinkInterface::path(index);
                        emit_device_changed(
                            &connection,
                            &device_manager,
                            &path,
                            "org.deepin.dde.Audio1.Sink",
                            DeviceKind::Sink,
                            index,
                            &["Name", "Description", "Volume", "Mute", "BaseVolume", "Balance", "Fade", "Card", "ActivePort"],
                        );
                    }
                    PulseEvent::SinkRemoved { index } => {
                        SinkInterface::delete(&device_manager, &connection, index);
                        emit_audio_list_changed(&connection, &device_manager, &["Sinks"]);
                    }
                    PulseEvent::SourceAdded { index } => {
                        let _ = SourceInterface::new(&pulse, &device_manager, &connection, event_config.clone(), event_sound_effect.clone(), index);
                        try_complete_pending_profile(&device_manager, index, false, &switch_tx);
                        emit_audio_list_changed(&connection, &device_manager, &["Sources"]);
                    }
                    PulseEvent::SourceChanged { index } => {
                        let _ = SourceInterface::update(&pulse, &device_manager, index);
                        let path = SourceInterface::path(index);
                        emit_device_changed(
                            &connection,
                            &device_manager,
                            &path,
                            "org.deepin.dde.Audio1.Source",
                            DeviceKind::Source,
                            index,
                            &["Name", "Description", "Volume", "Mute", "BaseVolume", "Balance", "Fade", "Card", "ActivePort"],
                        );
                    }
                    PulseEvent::SourceRemoved { index } => {
                        SourceInterface::delete(&device_manager, &connection, index);
                        emit_audio_list_changed(&connection, &device_manager, &["Sources"]);
                    }
                    PulseEvent::SinkInputAdded { index } => {
                        let _ = SinkInputInterface::new(&pulse, &device_manager, &connection, event_config.clone(), event_sound_effect.clone(), index);
                        emit_audio_list_changed(&connection, &device_manager, &["SinkInputs"]);
                    }
                    PulseEvent::SinkInputChanged { index } => {
                        let _ = SinkInputInterface::update(&pulse, &device_manager, index);
                        let path = SinkInputInterface::path(index);
                        emit_device_changed(
                            &connection,
                            &device_manager,
                            &path,
                            "org.deepin.dde.Audio1.SinkInput",
                            DeviceKind::SinkInput,
                            index,
                            &["Name", "Volume", "Mute", "Balance", "Fade"],
                        );
                    }
                    PulseEvent::SinkInputRemoved { index } => {
                        SinkInputInterface::delete(&device_manager, &connection, index);
                        emit_audio_list_changed(&connection, &device_manager, &["SinkInputs"]);
                    }
                    PulseEvent::CardAdded { index } => {
                        let _ = card::new(&pulse, &device_manager, index);
                        // 异步触发自动切换（worker 线程执行，不阻塞事件消费）
                        let _ = switch_tx.try_send(());
                        emit_audio_list_changed(&connection, &device_manager, &["Cards", "CardsWithoutUnavailable"]);
                    }
                    PulseEvent::CardChanged { index } => {
                        let _ = card::update(&pulse, &device_manager, index);
                        let _ = switch_tx.try_send(());
                        emit_audio_list_changed(&connection, &device_manager, &["Cards", "CardsWithoutUnavailable"]);
                    }
                    PulseEvent::CardRemoved { index } => {
                        card::delete(&device_manager, index);
                        let _ = switch_tx.try_send(());
                        emit_audio_list_changed(&connection, &device_manager, &["Cards", "CardsWithoutUnavailable"]);
                    }
                    PulseEvent::DefaultSinkChanged { name } => {
                        device_manager.write().set_default_sink(name.clone());
                        emit_audio_list_changed(&connection, &device_manager, &["DefaultSink"]);
                    }
                    PulseEvent::DefaultSourceChanged { name } => {
                        device_manager.write().set_default_source(name.clone());
                        emit_audio_list_changed(&connection, &device_manager, &["DefaultSource"]);
                    }
                    PulseEvent::Server => {
                        // 服务器变化：查询最新默认 sink/source 并更新
                        if let Ok((sink, source)) = pulse.default_sink_source() {
                            device_manager.write().set_default_sink(sink.clone());
                            device_manager.write().set_default_source(source.clone());
                            if !sink.is_empty() {
                                emit_audio_list_changed(&connection, &device_manager, &["DefaultSink"]);
                            }
                            if !source.is_empty() {
                                emit_audio_list_changed(&connection, &device_manager, &["DefaultSource"]);
                            }
                        }
                    }
                }
            }
            eprintln!("[dde-audio] event loop thread exited");
            let _ = &pulse;
        });

        Self {
            handle: Some(handle),
            worker_handle: Some(worker_handle),
            shutdown,
        }
    }
}
impl Drop for EventLoop {
    fn drop(&mut self) {
        use std::sync::atomic::Ordering;
        self.shutdown.store(true, Ordering::SeqCst);
        if let Some(handle) = self.handle.take() {
            let _ = handle.join();
        }
        if let Some(wh) = self.worker_handle.take() {
            let _ = wh.join();
        }
    }
}
