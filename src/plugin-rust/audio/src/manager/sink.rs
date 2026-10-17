// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! `org.deepin.dde.Audio1.Sink` 接口与设备生命周期。
//!
//! - `SinkInterface::new` — 事件到达时创建 Sink 写入 DeviceManager，并注册 D-Bus 对象
//! - `SinkInterface::update` — 事件到达时更新 Sink
//! - `SinkInterface::delete` — 事件到达时回收资源并注销 D-Bus 对象
//! - D-Bus 属性从 `DeviceManager` 读取，操作委托给 `backend::pulse::sink`

use std::sync::Arc;

use parking_lot::RwLock;
use zbus::interface;

use crate::backend::pulse::PulseManager;
use crate::backend::pulse::sink as pulse_sink;
use super::device_manager::DeviceManager;
/// 音频端口信息。
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize, zbus::zvariant::Type)]
pub struct Port {
    pub name: String,
    pub description: String,
    pub direction: u32,
    /// 端口可用性（0=Unknown, 1=NotAvailable, 2=Available）。
    pub available: u8,
}

/// Sink（输出设备）状态。
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize, zbus::zvariant::Type)]
pub struct Sink {
    pub index: u32,
    pub name: String,
    pub description: String,
    pub base_volume: f64,
    pub mute: bool,
    pub volume: f64,
    pub balance: f64,
    pub support_balance: bool,
    pub fade: f64,
    pub support_fade: bool,
    pub ports: Vec<Port>,
    pub active_port: Port,
    pub card: u32,
}

impl From<crate::backend::pulse::sink::BackendSink> for Sink {
    fn from(b: crate::backend::pulse::sink::BackendSink) -> Self {
        let from_port = |p: crate::backend::pulse::sink::BackendPort| Port {
            name: p.name,
            description: p.description,
            direction: p.direction,
            available: p.available,
        };
        Self {
            index: b.index,
            name: b.name,
            description: b.description,
            base_volume: b.base_volume,
            mute: b.mute,
            volume: b.volume,
            balance: b.balance,
            support_balance: true,
            fade: b.fade,
            support_fade: true,
            ports: b.ports.into_iter().map(from_port).collect(),
            active_port: from_port(b.active_port),
            card: b.card,
        }
    }
}


/// Sink D-Bus 对象。
///
/// 事件到达时由 `SinkInterface::new` 创建并注册到 zbus ObjectServer。
/// 属性通过 DeviceManager 读取，不保存可变状态。
pub struct SinkInterface {
    index: u32,
    pulse: Arc<PulseManager>,
    device_manager: Arc<RwLock<DeviceManager>>,
    connection: zbus::blocking::Connection,
    /// 配置持久化（音量/静音/端口状态）。
    config: Arc<super::config::AudioConfig>,
    /// 音量变化反馈音（`SetVolume`/`SetBalance` 的 `isPlay` 为真时触发）。
    sound_effect: Arc<super::sound_effect::SoundEffect>,
}

impl SinkInterface {
    pub fn new_instance(
        index: u32,
        pulse: Arc<PulseManager>,
        device_manager: Arc<RwLock<DeviceManager>>,
        connection: zbus::blocking::Connection,
        config: Arc<super::config::AudioConfig>,
        sound_effect: Arc<super::sound_effect::SoundEffect>,
    ) -> Self {
        Self { index, pulse, device_manager, connection, config, sound_effect }
    }

    /// 生成 Sink 的 D-Bus 对象路径。
    pub fn path(index: u32) -> String {
        format!("/org/deepin/dde/Audio1/Sink{index}")
    }

    fn state(&self) -> Option<Sink> {
        let reg = self.device_manager.read();
        reg.sinks.get(&self.index).cloned()
    }

    /// 当前卡名与活动端口名（持久化键）。
    fn config_key(&self) -> Option<(String, String)> {
        let reg = self.device_manager.read();
        let s = reg.sinks.get(&self.index)?;
        let card = reg.cards.get(&s.card)?;
        if s.active_port.name.is_empty() {
            return None;
        }
        Some((card.name.clone(), s.active_port.name.clone()))
    }
}
/// Sink 设备生命周期（事件处理入口，由 event_loop 调用）。
impl SinkInterface {
    /// Sink 新增：查询状态写入 DeviceManager，注册 D-Bus 对象。
    ///
    /// 返回 `(index, 是否成功)`。注册失败不阻断状态更新。
    pub fn new(
        pulse: &Arc<PulseManager>,
        device_manager: &Arc<RwLock<DeviceManager>>,
        connection: &zbus::blocking::Connection,
        config: Arc<super::config::AudioConfig>,
        sound_effect: Arc<super::sound_effect::SoundEffect>,
        index: u32,
    ) -> Result<(), String> {
        let state: Sink = pulse_sink::query_info(pulse, index)?.into();
        device_manager.write().add_sink(index, state);
        let obj = Self::new_instance(
            index,
            pulse.clone(),
            device_manager.clone(),
            connection.clone(),
            config,
            sound_effect,
        );
        connection
            .object_server()
            .at(Self::path(index), obj)
            .map_err(|e| format!("register sink {index} failed: {e}"))?;
        Ok(())
    }


    /// Sink 更新：查询最新状态写入 DeviceManager。
    ///
    /// D-Bus 属性读取时从 DeviceManager 拿最新值，无需持有实例引用。
    pub fn update(
        pulse: &Arc<PulseManager>,
        device_manager: &Arc<RwLock<DeviceManager>>,
        index: u32,
    ) -> Result<(), String> {
        let state: Sink = pulse_sink::query_info(pulse, index)?.into();
        device_manager.write().update_sink(index, state);
        eprintln!("[dde-audio] sink update: {index}");
        Ok(())
    }

    /// Sink 删除：回收资源，注销 D-Bus 对象。
    pub fn delete(
        device_manager: &Arc<RwLock<DeviceManager>>,
        connection: &zbus::blocking::Connection,
        index: u32,
    ) {
        device_manager.write().remove_sink(index);
        // 清理该设备的 meter（含 D-Bus 对象）。
        // Sink meter 无真实 backend，但 Meter 本身持有 DeviceManager/清理线程
        // 等资源；仍遵循 source 侧做法：先取出并释放写锁再注销 D-Bus 对象。
        let meter_id = format!("sink{index}");
        let removed_meter = {
            let mut dm = device_manager.write();
            dm.meters.remove(&meter_id)
        };
        if removed_meter.is_some() {
            use super::meter::Meter;
            let _ = connection.object_server().remove::<Meter, _>(Meter::path(index, true));
        }
        let _ = connection
            .object_server()
            .remove::<SinkInterface, _>(Self::path(index));
        eprintln!("[dde-audio] sink delete: {index}");
    }
}

#[interface(name = "org.deepin.dde.Audio1.Sink")]
impl SinkInterface {
    // ========== 属性 ==========

    #[zbus(property)]
    pub fn name(&self) -> String {
        self.state().map(|s| s.name).unwrap_or_default()
    }

    #[zbus(property)]
    pub fn description(&self) -> String {
        self.state().map(|s| s.description).unwrap_or_default()
    }

    #[zbus(property)]
    pub fn base_volume(&self) -> f64 {
        self.state().map(|s| s.base_volume).unwrap_or_default()
    }

    #[zbus(property)]
    pub fn mute(&self) -> bool {
        self.state().map(|s| s.mute).unwrap_or_default()
    }

    #[zbus(property)]
    pub fn volume(&self) -> f64 {
        self.state().map(|s| s.volume).unwrap_or_default()
    }

    #[zbus(property)]
    pub fn balance(&self) -> f64 {
        self.state().map(|s| s.balance).unwrap_or_default()
    }

    #[zbus(property)]
    pub fn support_balance(&self) -> bool {
        self.state().map(|s| s.support_balance).unwrap_or_default()
    }

    #[zbus(property)]
    pub fn fade(&self) -> f64 {
        self.state().map(|s| s.fade).unwrap_or_default()
    }

    #[zbus(property)]
    pub fn support_fade(&self) -> bool {
        self.state().map(|s| s.support_fade).unwrap_or_default()
    }


    #[zbus(property)]
    pub fn active_port(&self) -> (String, String, u8) {
        self.state()
            .map(|s| (s.active_port.name, s.active_port.description, s.active_port.available))
            .unwrap_or_default()
    }

    #[zbus(property)]
    pub fn card(&self) -> u32 {
        self.state().map(|s| s.card).unwrap_or_default()
    }

    // ========== 方法 ==========

    fn get_meter(&self) -> zbus::fdo::Result<zbus::zvariant::OwnedObjectPath> {
        use super::meter::{Meter, ZbusMeterCleanup};

        let id = format!("sink{}", self.index);
        // 已存在则直接返回
        if self.device_manager.read().meters.contains_key(&id) {
            return zbus::zvariant::ObjectPath::try_from(Meter::path(self.index, true))
                .map(Into::into)
                .map_err(|e| zbus::fdo::Error::Failed(e.to_string()));
        }

        // Sink 无真实峰值监测（Go 版亦为 TODO），backend 传 None
        let meter = Meter::new(
            id.clone(),
            self.index,
            true,
            None,
            self.device_manager.clone(),
            ZbusMeterCleanup::new(self.connection.clone()),
        );
        let path = Meter::path(self.index, true);
        self.connection
            .object_server()
            .at(path.clone(), meter.as_ref().clone())
            .map_err(|e| zbus::fdo::Error::Failed(format!("register meter failed: {e}")))?;
        // 登记到 DeviceManager，供清理线程与 get_meter 复用
        self.device_manager.write().meters.insert(id, meter);
        zbus::zvariant::ObjectPath::try_from(path)
            .map(Into::into)
            .map_err(|e| zbus::fdo::Error::Failed(e.to_string()))
    }

    /// 设置左右声道平衡。`is_play` 为真时播放反馈音（对齐 Go）。
    fn set_balance(&self, value: f64, is_play: bool) -> zbus::fdo::Result<()> {
        pulse_sink::set_balance(&self.pulse, self.index, value, is_play)
            .map_err(|e| zbus::fdo::Error::Failed(e))?;
        if let Some((card, port)) = self.config_key() {
            self.config.set_port_balance(&card, &port, value);
        }
        if is_play {
            self.sound_effect.play_volume_change();
        }
        Ok(())
    }

    /// 设置前后声道平衡。Go 版无 isPlay 参数、总是播放反馈音。
    fn set_fade(&self, value: f64) -> zbus::fdo::Result<()> {
        pulse_sink::set_fade(&self.pulse, self.index, value)
            .map_err(|e| zbus::fdo::Error::Failed(e))?;
        self.sound_effect.play_volume_change();
        Ok(())
    }

    /// 设置静音。
    ///
    /// 仅**取消**静音时播放反馈音：静音状态下播放提示音自相矛盾
    /// （对齐 Go `setMuteInternal` 的 `if !value && isPlayFeedback`）。
    fn set_mute(&self, value: bool) -> zbus::fdo::Result<()> {
        pulse_sink::set_mute(&self.pulse, self.index, value)
            .map_err(|e| zbus::fdo::Error::Failed(e))?;
        self.config.set_mute(false, value);
        if !value {
            self.sound_effect.play_volume_change();
        }
        Ok(())
    }

    fn set_port(&self, name: &str) -> zbus::fdo::Result<()> {
        pulse_sink::set_port(&self.pulse, self.index, name)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    /// 设置音量。`is_play` 为真时播放反馈音。
    ///
    /// 与 Go 的关键差异：Go 用 500ms `time.AfterFunc` 后沿去抖
    /// （`sink.go:403`），停手后才出声；这里是前沿节流，第一次变化
    /// 立即出声，窗口内后续变化丢弃。
    fn set_volume(&self, value: f64, is_play: bool) -> zbus::fdo::Result<()> {
        pulse_sink::set_volume(&self.pulse, self.index, value, is_play)
            .map_err(|e| zbus::fdo::Error::Failed(e))?;
        if let Some((card, port)) = self.config_key() {
            self.config.set_port_volume(&card, &port, value);
        }
        if is_play {
            self.sound_effect.play_volume_change();
        }
        Ok(())
    }
}
