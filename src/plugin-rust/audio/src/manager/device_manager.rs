// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! 设备管理器。
//!
//! 管理音频设备状态的内存存储。event_loop 通过各子模块的 new/update/delete
//! 处理事件后，调用 DeviceManager 的方法增删改四张表。
//! D-Bus 接口层通过 `Arc<RwLock<DeviceManager>>` 读取状态。
//!
//! 职责分工：
//! - DeviceManager — 管理四张 HashMap 表，提供增删查入口
//! - 子 device（sink.rs/source.rs/card.rs/sink_input.rs）— 处理 new/update
//!   的具体逻辑（查 pulse 构造状态），delete 有回收也在子 device 处理

use std::collections::HashMap;
use std::sync::Arc;

use super::meter;
// 各设备状态结构体定义在对应子模块，DeviceManager 直接引用子模块类型。
use super::card;
use super::sink;
use super::sink_input;
use super::source;
use super::device_type;
use super::port_priority::{Direction, PortPriority};
/// Cards 属性 JSON 序列化结构，字段名与 Go 版兼容。
#[derive(serde::Serialize)]
struct CardExport<'a> {
    #[serde(rename = "Id")]
    id: u32,
    #[serde(rename = "Name")]
    name: &'a str,
    #[serde(rename = "Ports")]
    ports: Vec<CardPortExport>,
}

/// Cards 属性端口序列化结构。
#[derive(serde::Serialize)]
struct CardPortExport {
    #[serde(rename = "Name")]
    name: String,
    #[serde(rename = "Enabled")]
    enabled: bool,
    #[serde(rename = "Bluetooth")]
    bluetooth: bool,
    #[serde(rename = "Description")]
    description: String,
    #[serde(rename = "Direction")]
    direction: u32,
    #[serde(rename = "PortType")]
    port_type: u32,
}

/// 设备管理器。
///
/// 持有四张 HashMap 表管理设备状态，通过 `Arc<RwLock<DeviceManager>>` 共享。
///
/// 各子模块状态结构体不能 `#[derive(Default)]`（含运行时状态），
/// 因此手动实现 Default。
pub struct DeviceManager {
    pub sinks: HashMap<u32, sink::Sink>,
    pub sources: HashMap<u32, source::Source>,
    pub sink_inputs: HashMap<u32, sink_input::SinkInput>,
    pub cards: HashMap<u32, card::Card>,
    pub default_sink: Option<String>,
    pub default_source: Option<String>,
    /// PulseAudio 模块状态：module 名 → 状态。
    pub modules: HashMap<String, crate::backend::pulse::module::ModuleState>,
    /// 活跃的音量计量器：id（如 "source3"）→ Meter。
    pub meters: HashMap<String, Arc<meter::Meter>>,
    /// 输出端口优先级策略。
    pub output_priority: PortPriority,
    /// 输入端口优先级策略。
    pub input_priority: PortPriority,
    /// 用户禁用的端口：(card_id, port_name)。
    pub disabled_ports: std::collections::HashSet<(u32, String)>,
}

impl Default for DeviceManager {
    fn default() -> Self {
        Self {
            sinks: HashMap::new(),
            sources: HashMap::new(),
            sink_inputs: HashMap::new(),
            cards: HashMap::new(),
            default_sink: None,
            default_source: None,
            modules: HashMap::new(),
            meters: HashMap::new(),
            output_priority: PortPriority::new(Direction::Output),
            input_priority: PortPriority::new(Direction::Input),
            disabled_ports: std::collections::HashSet::new(),
        }
    }
}

impl DeviceManager {
    // ===== Sink =====

    pub fn add_sink(&mut self, index: u32, state: sink::Sink) {
        self.sinks.insert(index, state);
    }

    pub fn update_sink(&mut self, index: u32, state: sink::Sink) {
        self.sinks.insert(index, state);
    }

    pub fn remove_sink(&mut self, index: u32) -> Option<sink::Sink> {
        self.sinks.remove(&index)
    }

#[allow(dead_code)]
    pub fn get_sink(&self, index: u32) -> Option<&sink::Sink> {
        self.sinks.get(&index)
    }

    // ===== Source =====

    pub fn add_source(&mut self, index: u32, state: source::Source) {
        self.sources.insert(index, state);
    }

    pub fn update_source(&mut self, index: u32, state: source::Source) {
        self.sources.insert(index, state);
    }

    pub fn remove_source(&mut self, index: u32) -> Option<source::Source> {
        self.sources.remove(&index)
    }

#[allow(dead_code)]
    pub fn get_source(&self, index: u32) -> Option<&source::Source> {
        self.sources.get(&index)
    }

    // ===== SinkInput =====

    pub fn add_sink_input(&mut self, index: u32, state: sink_input::SinkInput) {
        self.sink_inputs.insert(index, state);
    }

    pub fn update_sink_input(&mut self, index: u32, state: sink_input::SinkInput) {
        self.sink_inputs.insert(index, state);
    }

    pub fn remove_sink_input(&mut self, index: u32) -> Option<sink_input::SinkInput> {
        self.sink_inputs.remove(&index)
    }

#[allow(dead_code)]
    pub fn get_sink_input(&self, index: u32) -> Option<&sink_input::SinkInput> {
        self.sink_inputs.get(&index)
    }

    /// 应暴露给客户端的播放流索引（升序）。
    ///
    /// 过滤系统音效、通知音、事件音（对齐 Go `getSinkInputVisible`）。
    /// `Audio.SinkInputs` 属性与其变更信号都必须走这里，否则音效
    /// 反馈产生的瞬时流会让控制中心的应用音量列表闪烁。
    pub fn visible_sink_input_indices(&self) -> Vec<u32> {
        let mut v: Vec<u32> = self
            .sink_inputs
            .iter()
            .filter(|(_, s)| s.visible)
            .map(|(i, _)| *i)
            .collect();
        v.sort_unstable();
        v
    }

    pub fn add_card(&mut self, index: u32, state: card::Card) {
        self.cards.insert(index, state);
        self.refresh_priority();
    }

    pub fn update_card(&mut self, index: u32, state: card::Card) {
        self.cards.insert(index, state);
        self.refresh_priority();
    }

    pub fn remove_card(&mut self, index: u32) -> Option<card::Card> {
        let removed = self.cards.remove(&index);
        self.refresh_priority();
        removed
    }

    /// 用户禁用端口（enabled=false），供优先级优选排除。
    pub fn set_port_enabled(&mut self, card_id: u32, port_name: &str, enabled: bool) {
        if enabled {
            self.disabled_ports.remove(&(card_id, port_name.to_owned()));
        } else {
            self.disabled_ports.insert((card_id, port_name.to_owned()));
        }
        self.refresh_priority();
    }

    /// 查询端口是否被用户启用（不在禁用集合）。
    pub fn is_port_enabled(&self, card_id: u32, port_name: &str) -> bool {
        !self.disabled_ports.contains(&(card_id, port_name.to_owned()))
    }

    /// 用当前声卡列表刷新输出/输入端口优先级策略。
    pub fn refresh_priority(&mut self) {
        self.output_priority.refresh(&self.cards, &self.disabled_ports);
        self.input_priority.refresh(&self.cards, &self.disabled_ports);
    }

#[allow(dead_code)]
    pub fn get_card(&self, index: u32) -> Option<&card::Card> {
        self.cards.get(&index)
    }

    // ===== Default =====

    pub fn set_default_sink(&mut self, name: String) {
        self.default_sink = Some(name);
    }

    pub fn set_default_source(&mut self, name: String) {
        self.default_source = Some(name);
    }

    // ===== 查询辅助 =====

    /// 按名称查找 Sink 索引。
    pub fn find_sink_index_by_name(&self, name: &str) -> Option<u32> {
        self.sinks.values().find(|s| s.name == name).map(|s| s.index)
    }

    /// 按名称查找 Source 索引。
    pub fn find_source_index_by_name(&self, name: &str) -> Option<u32> {
        self.sources.values().find(|s| s.name == name).map(|s| s.index)
    }

    /// 序列化声卡列表为 JSON 字符串。
    ///
    /// 格式与 Go 版 Cards 属性兼容：
    /// `[{"Id":52,"Name":"...","Ports":[...]}]`
    pub fn cards_json(&self) -> String {
        let list: Vec<CardExport> = self
            .cards
            .values()
            .map(|c| card_to_export(c, false))
            .collect();
        serde_json::to_string(&list).unwrap_or_else(|_| "[]".into())
    }

    /// 序列化声卡列表为 JSON（不含不可用端口）。
    pub fn cards_without_unavailable_json(&self) -> String {
        let list: Vec<CardExport> = self
            .cards
            .values()
            .map(|c| card_to_export(c, true))
            .collect();
        serde_json::to_string(&list).unwrap_or_else(|_| "[]".into())
    }


    // ===== Module =====

    /// 获取模块状态。
    pub fn module_state(&self, name: &str) -> crate::backend::pulse::module::ModuleState {
        self.modules
            .get(name)
            .cloned()
            .unwrap_or_default()
    }

    /// 更新模块状态。
    pub fn set_module_state(
        &mut self,
        name: &str,
        state: crate::backend::pulse::module::ModuleState,
    ) {
        self.modules.insert(name.to_owned(), state);
    }

    /// 移除模块状态。
    #[allow(dead_code)]
    pub fn remove_module(&mut self, name: &str) {
        self.modules.remove(name);
    }

}


/// 将 Card 转换为 CardExport。
/// `filter_unavailable` 为 true 时过滤 enabled=false 的端口。
fn card_to_export(card: &card::Card, filter_unavailable: bool) -> CardExport<'_> {
    let ports: Vec<CardPortExport> = card
        .ports
        .iter()
        .filter(|p| !filter_unavailable || p.enabled)
        .map(|p| CardPortExport {
            name: p.name.clone(),
            enabled: p.enabled,
            bluetooth: p.bluetooth,
            description: p.description.clone(),
            direction: p.direction,
            port_type: device_type::detect_port_type(&card.name, &p.name).as_u32(),
        })
        .collect();
    CardExport {
        id: card.index,
        name: &card.name,
        ports,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::manager::card::{Card, CardStatus, PortInfo};

    fn mk_card(name: &str, ports: Vec<(String, bool)>) -> Card {
        // (port_name, direction) 仅输出端口
        Card {
            index: 0,
            name: name.to_owned(),
            active_profile: String::new(),
            ports: ports
                .into_iter()
                .map(|(n, en)| PortInfo {
                    name: n,
                    enabled: en,
                    bluetooth: false,
                    description: String::new(),
                    direction: 1,
                    profiles: vec![],
                    priority: 0,
                })
                .collect(),
            profiles: vec![],
            status: CardStatus::Ready,
            change: None,
        }
    }

    fn always_enabled(_: &crate::manager::port_priority::PortKey) -> bool {
        true
    }

    /// 声卡增删应刷新端口优先级。
    #[test]
    fn card_change_refreshes_priority() {
        let mut dm = DeviceManager::default();
        // 初始无卡，无候选
        assert_eq!(dm.output_priority.len(), 0);

        // 添加内置扬声器卡
        dm.add_card(1, mk_card("alsa.1", vec![("speaker".into(), true)]));
        assert_eq!(dm.output_priority.len(), 1);
        let p = dm.output_priority.prefer_port(always_enabled).unwrap();
        assert_eq!(p.port_name, "speaker");

        // 添加更高优先的 HDMI 卡 → 优先级刷新，HDMI 优先
        dm.add_card(2, mk_card("hdmi.2", vec![("hdmi-output".into(), true)]));
        assert_eq!(dm.output_priority.len(), 2);
        let p = dm.output_priority.prefer_port(always_enabled).unwrap();
        assert_eq!(p.port_name, "hdmi-output");

        // 移除 HDMI 卡 → 候选减一，回退到 speaker
        dm.remove_card(2);
        assert_eq!(dm.output_priority.len(), 1);
        let p = dm.output_priority.prefer_port(always_enabled).unwrap();
        assert_eq!(p.port_name, "speaker");
    }

    fn mk_sink_input(index: u32, visible: bool) -> sink_input::SinkInput {
        sink_input::SinkInput {
            index,
            name: format!("stream{index}"),
            mute: false,
            volume: 1.0,
            balance: 0.0,
            support_balance: true,
            fade: 0.0,
            support_fade: true,
            visible,
        }
    }

    /// `SinkInputs` 只暴露可见流：系统音效/通知音产生的瞬时流必须
    /// 被过滤，否则控制中心的应用音量列表会闪（对齐 Go
    /// `updatePropSinkInputs` 的 `if sinkInput.visible`）。
    #[test]
    fn hides_invisible_sink_inputs() {
        let mut dm = DeviceManager::default();
        dm.add_sink_input(10, mk_sink_input(10, true));
        dm.add_sink_input(11, mk_sink_input(11, false));
        dm.add_sink_input(12, mk_sink_input(12, true));

        // 全部三条都在内存表里（子对象仍按 index 注册）
        assert_eq!(dm.sink_inputs.len(), 3);
        // 但只有两条对外暴露，且按 index 升序稳定输出
        assert_eq!(dm.visible_sink_input_indices(), vec![10, 12]);
    }

    /// 全部不可见时返回空列表，不是 panic 也不是全量回退。
    #[test]
    fn all_invisible_yields_empty() {
        let mut dm = DeviceManager::default();
        dm.add_sink_input(1, mk_sink_input(1, false));
        dm.add_sink_input(2, mk_sink_input(2, false));
        assert!(dm.visible_sink_input_indices().is_empty());
    }
}
