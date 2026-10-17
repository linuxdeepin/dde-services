// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! `org.deepin.dde.Audio1.SinkInput` 接口与设备生命周期。
//!
//! - `SinkInputInterface::new` — 事件到达时创建 SinkInput 写入 DeviceManager，并注册 D-Bus 对象
//! - `SinkInputInterface::update` — 事件到达时更新 SinkInput
//! - `SinkInputInterface::delete` — 事件到达时回收资源并注销 D-Bus 对象
//! - D-Bus 属性从 `DeviceManager` 读取，操作委托给 `backend::pulse::sink_input`

use std::sync::Arc;

use parking_lot::RwLock;
use zbus::interface;

use crate::backend::pulse::PulseManager;
use crate::backend::pulse::sink_input as pulse_sink_input;
use super::device_manager::DeviceManager;
/// SinkInput（播放流）状态。
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize, zbus::zvariant::Type)]
pub struct SinkInput {
    pub index: u32,
    pub name: String,
    pub mute: bool,
    pub volume: f64,
    pub balance: f64,
    pub support_balance: bool,
    pub fade: f64,
    pub support_fade: bool,
    /// 是否暴露给客户端（系统音效/通知/事件音为 false）。
    ///
    /// 仅用于过滤 `Audio.SinkInputs` 列表，不影响子对象自身属性。
    pub visible: bool,
}

impl From<crate::backend::pulse::sink_input::BackendSinkInput> for SinkInput {
    fn from(b: crate::backend::pulse::sink_input::BackendSinkInput) -> Self {
        Self {
            index: b.index,
            name: b.name,
            mute: b.mute,
            volume: b.volume,
            balance: b.balance,
            support_balance: true,
            fade: b.fade,
            support_fade: true,
            visible: b.visible,
        }
    }
}


/// SinkInput D-Bus 对象。
pub struct SinkInputInterface {
    index: u32,
    pulse: Arc<PulseManager>,
    device_manager: Arc<RwLock<DeviceManager>>,
    #[allow(dead_code)]
    connection: zbus::blocking::Connection,
    /// 配置持久化（与 Sink/Source 接口对齐）。
    #[allow(dead_code)]
    config: Arc<super::config::AudioConfig>,
    /// 音量变化反馈音（`SetVolume` 的 `isPlay` 为真时触发）。
    sound_effect: Arc<super::sound_effect::SoundEffect>,
}

impl SinkInputInterface {
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

    /// 生成 SinkInput 的 D-Bus 对象路径。
    pub fn path(index: u32) -> String {
        format!("/org/deepin/dde/Audio1/SinkInput{index}")
    }

    fn state(&self) -> Option<SinkInput> {
        let reg = self.device_manager.read();
        reg.sink_inputs.get(&self.index).cloned()
    }
}

/// SinkInput 设备生命周期（事件处理入口，由 event_loop 调用）。
impl SinkInputInterface {
    /// SinkInput 新增：查询状态写入 DeviceManager，注册 D-Bus 对象。
    pub fn new(
        pulse: &Arc<PulseManager>,
        device_manager: &Arc<RwLock<DeviceManager>>,
        connection: &zbus::blocking::Connection,
        config: Arc<super::config::AudioConfig>,
        sound_effect: Arc<super::sound_effect::SoundEffect>,
        index: u32,
    ) -> Result<(), String> {
        let state: SinkInput = pulse_sink_input::query_info(pulse, index)?.into();
        device_manager.write().add_sink_input(index, state);
        eprintln!("[dde-audio] sink input new: {index}");
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
            .map_err(|e| format!("register sink input {index} failed: {e}"))?;
        Ok(())
    }


    /// SinkInput 更新：查询最新状态写入 DeviceManager。
    pub fn update(
        pulse: &Arc<PulseManager>,
        device_manager: &Arc<RwLock<DeviceManager>>,
        index: u32,
    ) -> Result<(), String> {
        let state: SinkInput = pulse_sink_input::query_info(pulse, index)?.into();
        device_manager.write().update_sink_input(index, state);
        eprintln!("[dde-audio] sink input update: {index}");
        Ok(())
    }

    /// SinkInput 删除：回收资源，注销 D-Bus 对象。
    pub fn delete(
        device_manager: &Arc<RwLock<DeviceManager>>,
        connection: &zbus::blocking::Connection,
        index: u32,
    ) {
        device_manager.write().remove_sink_input(index);
        // TODO: 回收资源
        let _ = connection
            .object_server()
            .remove::<SinkInputInterface, _>(Self::path(index));
        eprintln!("[dde-audio] sink input delete: {index}");
    }
}

#[interface(name = "org.deepin.dde.Audio1.SinkInput")]
impl SinkInputInterface {
    // ========== 属性 ==========

    #[zbus(property)]
    pub fn name(&self) -> String {
        self.state().map(|s| s.name).unwrap_or_default()
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

    // ========== 方法 ==========

    /// 设置左右声道平衡。`is_play` 为真时播放反馈音（对齐 Go）。
    fn set_balance(&self, value: f64, is_play: bool) -> zbus::fdo::Result<()> {
        pulse_sink_input::set_balance(&self.pulse, self.index, value, is_play)
            .map_err(|e| zbus::fdo::Error::Failed(e))?;
        if is_play {
            self.sound_effect.play_volume_change();
        }
        Ok(())
    }

    /// 设置前后声道平衡。Go 版无 isPlay 参数、总是播放反馈音。
    fn set_fade(&self, value: f64) -> zbus::fdo::Result<()> {
        pulse_sink_input::set_fade(&self.pulse, self.index, value)
            .map_err(|e| zbus::fdo::Error::Failed(e))?;
        self.sound_effect.play_volume_change();
        Ok(())
    }

    /// 设置静音。仅取消静音时播放反馈音（对齐 Go `if !value`）。
    fn set_mute(&self, value: bool) -> zbus::fdo::Result<()> {
        pulse_sink_input::set_mute(&self.pulse, self.index, value)
            .map_err(|e| zbus::fdo::Error::Failed(e))?;
        if !value {
            self.sound_effect.play_volume_change();
        }
        Ok(())
    }

    /// 设置音量。`is_play` 为真时播放反馈音（前沿节流，见 sound_effect）。
    fn set_volume(&self, value: f64, is_play: bool) -> zbus::fdo::Result<()> {
        pulse_sink_input::set_volume(&self.pulse, self.index, value, is_play)
            .map_err(|e| zbus::fdo::Error::Failed(e))?;
        if is_play {
            self.sound_effect.play_volume_change();
        }
        Ok(())
    }
}
