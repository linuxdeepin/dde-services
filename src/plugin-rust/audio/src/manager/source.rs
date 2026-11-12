// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! `org.deepin.dde.Audio1.Source` 接口与设备生命周期。
//!
//! - `SourceInterface::new` — 事件到达时创建 Source 写入 DeviceManager，并注册 D-Bus 对象
//! - `SourceInterface::update` — 事件到达时更新 Source
//! - `SourceInterface::delete` — 事件到达时回收资源并注销 D-Bus 对象
//! - D-Bus 属性从 `DeviceManager` 读取，操作委托给 `backend::pulse::source`

use std::sync::Arc;

use parking_lot::RwLock;
use zbus::interface;

use crate::backend::pulse::PulseManager;
use crate::backend::pulse::source as pulse_source;
use super::device_manager::DeviceManager;
/// Source（输入设备）状态。
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize, zbus::zvariant::Type)]
pub struct Source {
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
    pub ports: Vec<super::sink::Port>,
    pub active_port: super::sink::Port,
    pub card: u32,
}

impl From<crate::backend::pulse::source::BackendSource> for Source {
    fn from(b: crate::backend::pulse::source::BackendSource) -> Self {
        let from_port = |p: crate::backend::pulse::sink::BackendPort| super::sink::Port {
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


/// Source D-Bus 对象。
pub struct SourceInterface {
    index: u32,
    pulse: Arc<PulseManager>,
    device_manager: Arc<RwLock<DeviceManager>>,
    connection: zbus::blocking::Connection,
    /// 配置持久化（音量/静音/端口状态）。
    config: Arc<super::config::AudioConfig>,
    /// 音量变化反馈音（`SetVolume`/`SetBalance` 的 `isPlay` 为真时触发）。
    sound_effect: Arc<super::sound_effect::SoundEffect>,
}

impl SourceInterface {
    /// 构造 Source D-Bus 对象实例（关联函数，由注册逻辑调用）。
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
    /// 生成 Source 的 D-Bus 对象路径。
    pub fn path(index: u32) -> String {
        format!("/org/deepin/dde/Audio1/Source{index}")
    }

    fn state(&self) -> Option<Source> {
        let reg = self.device_manager.read();
        reg.sources.get(&self.index).cloned()
    }

    /// 当前卡名与活动端口名（持久化键）。
    fn config_key(&self) -> Option<(String, String)> {
        let reg = self.device_manager.read();
        let s = reg.sources.get(&self.index)?;
        let card = reg.cards.get(&s.card)?;
        if s.active_port.name.is_empty() {
            return None;
        }
        Some((card.name.clone(), s.active_port.name.clone()))
    }
}

/// Source 设备生命周期（事件处理入口，由 event_loop 调用）。
impl SourceInterface {
    /// Source 新增：查询状态写入 DeviceManager，注册 D-Bus 对象。
    pub fn new(
        pulse: &Arc<PulseManager>,
        device_manager: &Arc<RwLock<DeviceManager>>,
        connection: &zbus::blocking::Connection,
        config: Arc<super::config::AudioConfig>,
        sound_effect: Arc<super::sound_effect::SoundEffect>,
        index: u32,
    ) -> Result<(), String> {
        let state: Source = pulse_source::query_info(pulse, index)?.into();
        device_manager.write().add_source(index, state);
        eprintln!("[dde-audio] source new: {index}");
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
            .map_err(|e| format!("register source {index} failed: {e}"))?;
        Ok(())
    }


    /// Source 更新：查询最新状态写入 DeviceManager。
    pub fn update(
        pulse: &Arc<PulseManager>,
        device_manager: &Arc<RwLock<DeviceManager>>,
        index: u32,
    ) -> Result<(), String> {
        let state: Source = pulse_source::query_info(pulse, index)?.into();
        device_manager.write().update_source(index, state);
        eprintln!("[dde-audio] source update: {index}");
        Ok(())
    }

    /// Source 删除：回收资源，注销 D-Bus 对象。
    pub fn delete(
        device_manager: &Arc<RwLock<DeviceManager>>,
        connection: &zbus::blocking::Connection,
        index: u32,
    ) {
        device_manager.write().remove_source(index);
        // 清理该设备的 meter（含 D-Bus 对象）。
        // 先从 DeviceManager 取出并释放写锁，再注销 D-Bus 对象：
        // Meter 被移除后可能触发 SourceMeter 析构，后者要锁定 PulseAudio
        // mainloop；若仍持有 DeviceManager 写锁，会扩大锁竞争/引发潜在死锁。
        let meter_id = format!("source{index}");
        let removed_meter = {
            let mut dm = device_manager.write();
            dm.meters.remove(&meter_id)
        };
        if removed_meter.is_some() {
            use super::meter::Meter;
            let _ = connection.object_server().remove::<Meter, _>(Meter::path(index, false));
        }
        let _ = connection
            .object_server()
            .remove::<SourceInterface, _>(Self::path(index));
        eprintln!("[dde-audio] source delete: {index}");
    }
}

#[interface(name = "org.deepin.dde.Audio1.Source")]
impl SourceInterface {
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
    pub fn card(&self) -> u32 {
        self.state().map(|s| s.card).unwrap_or_default()
    }

    #[zbus(property)]
    pub fn active_port(&self) -> (String, String, u8) {
        self.state()
            .map(|s| (s.active_port.name, s.active_port.description, s.active_port.available))
            .unwrap_or_default()
    }

    // ========== 方法 ==========

    fn get_meter(&self) -> zbus::fdo::Result<zbus::zvariant::OwnedObjectPath> {
        use super::meter::{Meter, ZbusMeterCleanup};

        let id = format!("source{}", self.index);
        // 已存在则直接返回
        if self.device_manager.read().meters.contains_key(&id) {
            return zbus::zvariant::ObjectPath::try_from(Meter::path(self.index, false))
                .map(Into::into)
                .map_err(|e| zbus::fdo::Error::Failed(e.to_string()));
        }

        // 创建真实峰值检测 stream
        let backend: Arc<dyn crate::backend::pulse::meter::MeterBackend> = self
            .pulse
            .create_source_meter(self.index)
            .map_err(|e| zbus::fdo::Error::Failed(format!("create source meter failed: {e}")))?;
        let meter = Meter::new(
            id.clone(),
            self.index,
            false,
            Some(backend),
            self.device_manager.clone(),
            ZbusMeterCleanup::new(self.connection.clone()),
        );
        let path = Meter::path(self.index, false);
        self.connection
            .object_server()
            .at(path.clone(), meter.as_ref().clone())
            .map_err(|e| zbus::fdo::Error::Failed(format!("register meter failed: {e}")))?;
        // 登记到 DeviceManager
        self.device_manager.write().meters.insert(id, meter);
        zbus::zvariant::ObjectPath::try_from(path)
            .map(Into::into)
            .map_err(|e| zbus::fdo::Error::Failed(e.to_string()))
    }

    /// 设置左右声道平衡。`is_play` 为真时播放反馈音（对齐 Go）。
    fn set_balance(&self, value: f64, is_play: bool) -> zbus::fdo::Result<()> {
        pulse_source::set_balance(&self.pulse, self.index, value, is_play)
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
        pulse_source::set_fade(&self.pulse, self.index, value)
            .map_err(|e| zbus::fdo::Error::Failed(e))?;
        self.sound_effect.play_volume_change();
        Ok(())
    }

    /// 设置静音。仅取消静音时播放反馈音（对齐 Go `if !value`）。
    fn set_mute(&self, value: bool) -> zbus::fdo::Result<()> {
        pulse_source::set_mute(&self.pulse, self.index, value)
            .map_err(|e| zbus::fdo::Error::Failed(e))?;
        self.config.set_mute(true, value);
        if !value {
            self.sound_effect.play_volume_change();
        }
        Ok(())
    }

    fn set_port(&self, name: &str) -> zbus::fdo::Result<()> {
        pulse_source::set_port(&self.pulse, self.index, name)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    /// 设置音量。`is_play` 为真时播放反馈音（前沿节流，见 sound_effect）。
    fn set_volume(&self, value: f64, is_play: bool) -> zbus::fdo::Result<()> {
        pulse_source::set_volume(&self.pulse, self.index, value, is_play)
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
