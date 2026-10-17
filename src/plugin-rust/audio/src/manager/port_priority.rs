// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! 端口优先级策略。
//!
//! 依据设备类型（`device_type::detect_port_type`）与端口权重，选择
//! 优先使用的端口。Output/Input 方向各有独立实例。
//!
//! 排序：类型优先（`type_order` 靠前者高）→ 同类型内端口 Priority（值大高）
//! → 卡名/端口名稳定排序。
//!
//! 来源：dde-daemon/audio1 的 PriorityPolicy，但简化为单一 Vec 实时排序，
//! 消除 Go 的双结构与 Position 指针。

use std::collections::{HashMap, HashSet};

use crate::manager::card::Card;
use crate::manager::device_type::{self, PortType};

/// 端口标识。
#[derive(Clone, Debug, PartialEq, Eq, Hash, serde::Serialize, serde::Deserialize)]
pub struct PortKey {
    pub card_name: String,
    pub port_name: String,
}

/// 一个候选端口。
#[derive(Clone, Debug)]
pub struct PrioritizedPort {
    pub card_id: u32,
    pub card_name: String,
    pub port_name: String,
    pub port_type: PortType,
    /// Pulse 端口权重（值大=高），作为同类型内排序键。
    pub priority: u32,
}

/// 方向：输出或输入。
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Direction {
    Output,
    Input,
}

impl From<u32> for Direction {
    /// `PortInfo.direction` 语义（D-Bus 契约）：1=输出(sink)，2=输入(source)。
    fn from(v: u32) -> Self {
        if v == 2 {
            Direction::Input
        } else {
            Direction::Output
        }
    }
}

/// 端口优先级策略（单一方向）。
pub struct PortPriority {
    direction: Direction,
    /// 类型优先级顺序（前=高）。可通过配置覆盖。
    type_order: Vec<PortType>,
    /// 候选端口，构建时按优先级排序。
    ports: Vec<PrioritizedPort>,
    /// 用户显式选定的端口（最高优先级）。
    user_prefer: Option<PortKey>,
}

impl PortPriority {
    /// 新建空策略，用默认类型顺序。
    pub fn new(direction: Direction) -> Self {
        Self {
            direction,
            type_order: default_type_order(direction),
            ports: Vec::new(),
            user_prefer: None,
        }
    }

    /// 配置类型优先级顺序（列表顺序即优先级，前=高）。
    pub fn set_type_order(&mut self, order: Vec<PortType>) {
        self.type_order = order;
        self.resort();
    }

    /// 用卡片列表刷新候选端口。
    ///
    /// `disabled` 为用户禁用的 (card_id, port_name) 集合，排除在候选外。
    pub fn refresh(&mut self, cards: &HashMap<u32, Card>, disabled: &HashSet<(u32, String)>) {
        self.ports = cards
            .values()
            .flat_map(|card| {
                card.ports.iter().filter_map(|p| {
                    let d: Direction = p.direction.into();
                    if d != self.direction
                        || !p.enabled
                        || disabled.contains(&(card.index, p.name.clone()))
                    {
                        return None;
                    }
                    Some(PrioritizedPort {
                        card_id: card.index,
                        card_name: card.name.clone(),
                        port_name: p.name.clone(),
                        port_type: device_type::detect_port_type(&card.name, &p.name),
                        priority: p.priority,
                    })
                })
            })
            .collect();
        self.resort();
    }

    /// 返回最高优先级且可用的端口。
    ///
    /// `is_enabled` 回调判断端口当前是否可用（如 profile 支持情况）。
    pub fn prefer_port(&self, is_enabled: impl Fn(&PortKey) -> bool) -> Option<&PrioritizedPort> {
        // 用户首选优先（若可用）
        if let Some(ukey) = &self.user_prefer {
            if let Some(p) = self.ports.iter().find(|p| {
                p.card_name == ukey.card_name && p.port_name == ukey.port_name
            }) {
                let key = PortKey {
                    card_name: p.card_name.clone(),
                    port_name: p.port_name.clone(),
                };
                if is_enabled(&key) {
                    return Some(p);
                }
            }
        }
        self.ports.iter().find(|p| {
            let key = PortKey {
                card_name: p.card_name.clone(),
                port_name: p.port_name.clone(),
            };
            is_enabled(&key)
        })
    }

    /// 返回从指定端口之后的下一个可用端口（自动切换枚举）。
    pub fn next_avail(
        &self,
        from: Option<&PortKey>,
        is_enabled: impl Fn(&PortKey) -> bool,
    ) -> Option<&PrioritizedPort> {
        let start = match from {
            None => 0,
            Some(fk) => self
                .ports
                .iter()
                .position(|p| p.card_name == fk.card_name && p.port_name == fk.port_name)
                .map(|i| i + 1)
                .unwrap_or(0),
        };
        self.ports[start..].iter().find(|p| {
            let key = PortKey {
                card_name: p.card_name.clone(),
                port_name: p.port_name.clone(),
            };
            is_enabled(&key)
        })
    }

    /// 用户手动选择某端口，提到最高优先级。
    pub fn set_user_prefer(&mut self, card_name: &str, port_name: &str) {
        self.user_prefer = Some(PortKey {
            card_name: card_name.to_owned(),
            port_name: port_name.to_owned(),
        });
    }

    /// 端口列表长度（测试/调试）。
    pub fn len(&self) -> usize {
        self.ports.len()
    }

    fn resort(&mut self) {
        // 预计算每个端口的类型排序键，避免闭包内借用 self
        let type_order = self.type_order.clone();
        self.ports.sort_by(|a, b| {
            let rank = |t: &PortType| -> usize {
                type_order
                    .iter()
                    .position(|x| x == t)
                    .unwrap_or(usize::MAX)
            };
            rank(&a.port_type)
                .cmp(&rank(&b.port_type))
                .then_with(|| b.priority.cmp(&a.priority))
                .then_with(|| a.card_name.cmp(&b.card_name))
                .then_with(|| a.port_name.cmp(&b.port_name))
        });
    }
}

/// 默认类型优先级顺序（配置未提供时）。
fn default_type_order(direction: Direction) -> Vec<PortType> {
    match direction {
        Direction::Output => vec![
            PortType::Hdmi,
            PortType::Builtin,
            PortType::LineIO,
            PortType::Headset,
            PortType::Usb,
            PortType::Bluetooth,
            PortType::MultiChannel,
        ],
        Direction::Input => vec![
            PortType::Builtin,
            PortType::Headset,
            PortType::LineIO,
            PortType::Usb,
            PortType::Bluetooth,
            PortType::Hdmi,
            PortType::MultiChannel,
        ],
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::manager::card::PortInfo;

    fn mk_card(index: u32, name: &str, ports: Vec<(String, u32, u32, bool)>) -> Card {
        // (port_name, direction, priority, enabled)
        Card {
            index,
            name: name.to_owned(),
            active_profile: String::new(),
            ports: ports
                .into_iter()
                .map(|(n, d, p, en)| PortInfo {
                    name: n,
                    enabled: en,
                    bluetooth: false,
                    description: String::new(),
                    direction: d,
                    profiles: vec![],
                    priority: p,
                })
                .collect(),
            profiles: vec![],
            status: crate::manager::card::CardStatus::Ready,
            change: None,
        }
    }

    fn always_enabled(_: &PortKey) -> bool {
        true
    }

    #[test]
    fn prefer_output_type_order() {
        let mut pm = PortPriority::new(Direction::Output);
        let mut cards = HashMap::new();
        // 内置扬声器端口 priority 低，但类型 Builtin 高于 Usb
        cards.insert(1, mk_card(1, "alsa.1", vec![
            ("analog-output-speaker".into(), 1, 10, true),
        ]));
        cards.insert(2, mk_card(2, "usb.2", vec![
            ("usb-output".into(), 1, 100, true),
        ]));
        pm.refresh(&cards, &HashSet::new());
        let p = pm.prefer_port(always_enabled).unwrap();
        // Builtin(扬声器) 应优先于 Usb，尽管 Usb priority 高
        assert_eq!(p.port_name, "analog-output-speaker");
    }

    #[test]
    fn user_prefer_overrides() {
        let mut pm = PortPriority::new(Direction::Input);
        let mut cards = HashMap::new();
        cards.insert(1, mk_card(1, "alsa.1", vec![
            ("input-mic".into(), 2, 0, true),
            ("linein".into(), 2, 10, true),
        ]));
        pm.refresh(&cards, &HashSet::new());
        // 默认 Builtin 优先
        assert_eq!(pm.prefer_port(always_enabled).unwrap().port_name, "input-mic");
        // 用户选 linein
        pm.set_user_prefer("alsa.1", "linein");
        assert_eq!(pm.prefer_port(always_enabled).unwrap().port_name, "linein");
    }

    #[test]
    fn same_type_priority_weight() {
        let mut pm = PortPriority::new(Direction::Output);
        let mut cards = HashMap::new();
        cards.insert(1, mk_card(1, "alsa.1", vec![
            ("analog-output-a".into(), 1, 5, true),
            ("analog-output-b".into(), 1, 50, true),
        ]));
        pm.refresh(&cards, &HashSet::new());
        // 同类型 Builtin：priority 大者优先
        assert_eq!(pm.prefer_port(always_enabled).unwrap().port_name, "analog-output-b");
    }

    #[test]
    fn disabled_excluded() {
        let mut pm = PortPriority::new(Direction::Output);
        let mut cards = HashMap::new();
        cards.insert(1, mk_card(1, "alsa.1", vec![
            ("hdmi-output".into(), 1, 10, true),
            ("analog-output".into(), 1, 5, false),  // 禁用
        ]));
        pm.refresh(&cards, &HashSet::new());
        assert_eq!(pm.prefer_port(always_enabled).unwrap().port_name, "hdmi-output");
    }

}