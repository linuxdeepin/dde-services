// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! `org.deepin.dde.Audio1` 主接口。
//!
//! 属性和方法对应 Go 版 `Audio` 结构体的导出成员和 `exported_methods_auto.go`。
//! 设备列表从 `DeviceManager` 读取，Audio 级操作委托给 `AudioManager`。

use std::sync::Arc;

use parking_lot::RwLock;
use zbus::interface;

use super::device_manager::DeviceManager;
use super::AudioManager;

/// Audio D-Bus 对象。
///
/// 持有 `Arc<AudioManager>` 用于 Audio 级操作，
/// 持有 `Arc<RwLock<DeviceManager>>` 用于读取设备列表。
pub struct Audio {
    manager: Arc<AudioManager>,
    device_manager: Arc<RwLock<DeviceManager>>,
}

impl Audio {
    pub fn new(manager: Arc<AudioManager>, device_manager: Arc<RwLock<DeviceManager>>) -> Self {
        Self { manager, device_manager }
    }

    fn sink_paths(&self) -> Vec<zbus::zvariant::OwnedObjectPath> {
        let reg = self.device_manager.read();
        reg.sinks
            .keys()
            .map(|index| zbus::zvariant::ObjectPath::try_from(format!("/org/deepin/dde/Audio1/Sink{index}")).unwrap().into())
            .collect()
    }

    fn source_paths(&self) -> Vec<zbus::zvariant::OwnedObjectPath> {
        let reg = self.device_manager.read();
        reg.sources
            .keys()
            .map(|index| zbus::zvariant::ObjectPath::try_from(format!("/org/deepin/dde/Audio1/Source{index}")).unwrap().into())
            .collect()
    }

    fn sink_input_paths(&self) -> Vec<zbus::zvariant::OwnedObjectPath> {
        let reg = self.device_manager.read();
        reg.visible_sink_input_indices()
            .into_iter()
            .map(|index| zbus::zvariant::ObjectPath::try_from(format!("/org/deepin/dde/Audio1/SinkInput{index}")).unwrap().into())
            .collect()
    }

    fn default_sink_path(&self) -> zbus::zvariant::OwnedObjectPath {
        let reg = self.device_manager.read();
        let index = reg
            .default_sink
            .as_deref()
            .and_then(|name| reg.find_sink_index_by_name(name));
        match index {
            Some(i) => zbus::zvariant::ObjectPath::try_from(
                format!("/org/deepin/dde/Audio1/Sink{i}"),
            )
            .unwrap()
            .into(),
            None => zbus::zvariant::OwnedObjectPath::default(),
        }
    }

    fn default_source_path(&self) -> zbus::zvariant::OwnedObjectPath {
        let reg = self.device_manager.read();
        let index = reg
            .default_source
            .as_deref()
            .and_then(|name| reg.find_source_index_by_name(name));
        match index {
            Some(i) => zbus::zvariant::ObjectPath::try_from(
                format!("/org/deepin/dde/Audio1/Source{i}"),
            )
            .unwrap()
            .into(),
            None => zbus::zvariant::OwnedObjectPath::default(),
        }
    }
}

#[interface(name = "org.deepin.dde.Audio1")]
impl Audio {
    // ========== 属性 ==========

    #[zbus(property)]
    pub fn sink_inputs(&self) -> Vec<zbus::zvariant::OwnedObjectPath> {
        self.sink_input_paths()
    }

    #[zbus(property)]
    pub fn sinks(&self) -> Vec<zbus::zvariant::OwnedObjectPath> {
        self.sink_paths()
    }

    #[zbus(property)]
    pub fn sources(&self) -> Vec<zbus::zvariant::OwnedObjectPath> {
        self.source_paths()
    }

    #[zbus(property)]
    pub fn default_sink(&self) -> zbus::zvariant::OwnedObjectPath {
        self.default_sink_path()
    }

    #[zbus(property)]
    pub fn default_source(&self) -> zbus::zvariant::OwnedObjectPath {
        self.default_source_path()
    }

    #[zbus(property)]
    pub fn cards(&self) -> String {
        self.device_manager.read().cards_json()
    }

    #[zbus(property)]
    pub fn cards_without_unavailable(&self) -> String {
        self.device_manager.read().cards_without_unavailable_json()
    }

    #[zbus(property)]
    pub fn bluetooth_audio_mode(&self) -> String {
        self.manager.bluetooth_audio_mode()
    }

    #[zbus(property)]
    pub fn bluetooth_audio_mode_opts(&self) -> Vec<String> {
        self.manager.bluetooth_audio_mode_opts()
    }

    #[zbus(property)]
    pub fn current_audio_server(&self) -> String {
        self.manager.current_audio_server()
    }

    #[zbus(property)]
    pub fn audio_server_state(&self) -> bool {
        self.manager.audio_server_state()
    }

    #[zbus(property, name = "MaxUIVolume")]
    pub fn max_ui_volume(&self) -> f64 {
        self.manager.max_ui_volume()
    }

    #[zbus(property)]
    pub fn mono(&self) -> bool {
        self.manager.mono()
    }

    #[zbus(property)]
    pub fn increase_volume(&self) -> bool {
        self.manager.increase_volume()
    }

    #[zbus(property)]
    pub fn reduce_noise(&self) -> bool {
        self.manager.reduce_noise()
    }

    #[zbus(property)]
    pub fn pause_player(&self) -> bool {
        self.manager.pause_player()
    }

    #[zbus(property)]
    pub fn set_increase_volume(&self, enable: bool) -> zbus::fdo::Result<()> {
        self.manager
            .set_increase_volume(enable)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    #[zbus(property)]
    pub fn set_reduce_noise(&self, enable: bool) -> zbus::fdo::Result<()> {
        self.manager
            .set_reduce_noise(enable)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    #[zbus(property)]
    pub fn set_pause_player(&self, enable: bool) -> zbus::fdo::Result<()> {
        self.manager
            .set_pause_player(enable)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }
    // ========== 方法 ==========

    fn is_port_enabled(&self, card_id: u32, port_name: &str) -> zbus::fdo::Result<bool> {
        self.manager
            .is_port_enabled(card_id, port_name)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    fn no_restart_pulse_audio(&self) -> zbus::fdo::Result<()> {
        self.manager
            .no_restart_pulse_audio()
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    fn reset(&self) -> zbus::fdo::Result<()> {
        self.manager.reset().map_err(|e| zbus::fdo::Error::Failed(e))
    }

    fn set_bluetooth_audio_mode(&self, mode: &str) -> zbus::fdo::Result<()> {
        self.manager
            .set_bluetooth_audio_mode(mode)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    fn set_port(&self, card_id: u32, port_name: &str, direction: i32) -> zbus::fdo::Result<()> {
        // 控制中心/Go 原版方向参数为 int32（D-Bus 签名 i），内部统一按 u32 处理。
        let direction = direction as u32;
        self.manager
            .set_port(card_id, port_name, direction)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    fn set_port_enabled(&self, card_id: u32, port_name: &str, enabled: bool) -> zbus::fdo::Result<()> {
        self.manager
            .set_port_enabled(card_id, port_name, enabled)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    fn set_current_audio_server(&self, server_name: &str) -> zbus::fdo::Result<()> {
        self.manager
            .set_current_audio_server(server_name)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    fn set_mono(&self, enable: bool) -> zbus::fdo::Result<()> {
        self.manager
            .set_mono(enable)
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }

    fn stop_audio_service(&self) -> zbus::fdo::Result<()> {
        self.manager
            .stop_audio_service()
            .map_err(|e| zbus::fdo::Error::Failed(e))
    }
}
