// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! 音频配置持久化。
//!
//! 持久化用户可跨重启保留的音频设置：禁用的端口、手动选择的端口偏好。
//! 键统一用卡名（`card_name`）而非 Pulse 索引 —— 索引跨重启/热插拔不稳定，
//! 加载时再把卡名映射回当前 `card_id`。
//!
//! 文件位置：`$XDG_CONFIG_HOME/deepin/dde-daemon/audio-config.json`
//! （默认 `~/.config/deepin/dde-daemon/audio-config.json`），与 Go 版 dde-daemon
//! 使用同一目录。写入采用「临时文件 + rename」原子替换，避免崩溃损坏配置。

use std::collections::BTreeMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use parking_lot::RwLock;

use super::device_manager::DeviceManager;
use super::port_priority::{Direction, PortKey};

/// 配置文件相对路径（位于用户配置目录下）。
const CONFIG_FILE: &str = "deepin/dde-daemon/audio-config.json";

/// 配置持久化数据的顶层结构。
///
/// 全部字段带 `#[serde(default)]`，保证旧版本配置缺字段时仍可解析。
#[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
pub struct PersistData {
    /// 输出静音（全局）。
    #[serde(default)]
    pub mute_output: bool,
    /// 输入静音（全局）。
    #[serde(default)]
    pub mute_input: bool,
    /// 端口音量与平衡：卡名 → 端口名 → (volume, balance)。
    /// 与 Go 版 ConfigKeeper 对齐，端口切换/重启后恢复。
    #[serde(default)]
    pub port_state: BTreeMap<String, BTreeMap<String, PortState>>,
    /// 用户禁用的端口：卡名 → 端口名列表。
    #[serde(default)]
    pub disabled_ports: BTreeMap<String, Vec<String>>,
    /// 输出方向用户首选端口。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub user_prefer_output: Option<PortKey>,
    /// 输入方向用户首选端口。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub user_prefer_input: Option<PortKey>,
    /// 输出方向类型优先级顺序（PortType 的 u32 编码，前=高）。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub type_order_output: Option<Vec<u32>>,
    /// 输入方向类型优先级顺序（PortType 的 u32 编码，前=高）。
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub type_order_input: Option<Vec<u32>>,
}

/// 单个端口的音量/平衡持久化状态。
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct PortState {
    /// 相对音量（0.0~1.0，UI 值）。
    pub volume: f64,
    /// 左右平衡（-1.0~1.0）。
    pub balance: f64,
}

impl Default for PortState {
    fn default() -> Self {
        Self { volume: 0.5, balance: 0.0 }
    }
}

/// 音频配置：内存态 + 文件路径。
pub struct AudioConfig {
    data: RwLock<PersistData>,
    path: PathBuf,
}

/// 计算用户配置目录（`$XDG_CONFIG_HOME` 或 `~/.config`）。
fn user_config_dir() -> PathBuf {
    if let Ok(dir) = std::env::var("XDG_CONFIG_HOME") {
        if !dir.is_empty() {
            return PathBuf::from(dir);
        }
    }
    if let Ok(home) = std::env::var("HOME") {
        if !home.is_empty() {
            return PathBuf::from(home).join(".config");
        }
    }
    PathBuf::from(".config")
}

impl AudioConfig {
    /// 新建配置，使用默认文件路径（启动时加载）。
    pub fn new() -> Self {
        Self::with_path(user_config_dir().join(CONFIG_FILE))
    }

    /// 新建配置，指定文件路径（测试用）。
    pub fn with_path(path: PathBuf) -> Self {
        Self {
            data: RwLock::new(PersistData::default()),
            path,
        }
    }

    /// 配置文件路径。
    pub fn path(&self) -> &Path {
        &self.path
    }

    /// 从文件加载配置；文件不存在或解析失败时保持默认值。
    pub fn load(&self) {
        let raw = match fs::read_to_string(&self.path) {
            Ok(raw) => raw,
            Err(_) => return,
        };
        match serde_json::from_str::<PersistData>(&raw) {
            Ok(data) => {
                *self.data.write() = data;
            }
            Err(e) => {
                eprintln!("[dde-audio] config parse error: {e}");
            }
        }
    }

    /// 将当前内存配置原子写回文件。
    ///
    /// 先写临时文件再 rename，避免进程崩溃留下半截 JSON。
    pub fn save(&self) {
        let data = self.data.read();
        let json = match serde_json::to_string_pretty(&*data) {
            Ok(json) => json,
            Err(e) => {
                eprintln!("[dde-audio] config serialize error: {e}");
                return;
            }
        };
        drop(data);

        let dir = match self.path.parent() {
            Some(dir) => dir,
            None => {
                eprintln!("[dde-audio] config path has no parent: {}", self.path.display());
                return;
            }
        };
        if let Err(e) = fs::create_dir_all(dir) {
            eprintln!("[dde-audio] create config dir failed: {e}");
            return;
        }

        let tmp = self.path.with_extension("json.tmp");
        let write_result = (|| -> std::io::Result<()> {
            let mut f = fs::File::create(&tmp)?;
            f.write_all(json.as_bytes())?;
            f.sync_all()?;
            fs::rename(&tmp, &self.path)?;
            Ok(())
        })();

        if let Err(e) = write_result {
            eprintln!("[dde-audio] write config failed: {e}");
            let _ = fs::remove_file(&tmp);
        }
    }

    /// 启用/禁用端口（内存 + 持久化）。
    pub fn set_port_enabled(&self, card_name: &str, port_name: &str, enabled: bool) {
        let mut data = self.data.write();
        let ports = data.disabled_ports.entry(card_name.to_owned()).or_default();
        if enabled {
            ports.retain(|p| p != port_name);
            if ports.is_empty() {
                data.disabled_ports.remove(card_name);
            }
        } else if !ports.iter().any(|p| p == port_name) {
            ports.push(port_name.to_owned());
        }
        drop(data);
        self.save();
    }

    /// 记录用户首选端口（内存 + 持久化）。
    pub fn set_user_prefer(&self, direction: Direction, key: PortKey) {
        let mut data = self.data.write();
        match direction {
            Direction::Output => data.user_prefer_output = Some(key),
            Direction::Input => data.user_prefer_input = Some(key),
        }
        drop(data);
        self.save();
    }
    /// 设置类型优先级顺序（内存 + 持久化）。
    ///
    /// `order` 为 PortType 列表（前=高）。时序：用于未来 dconfig/手动配置覆盖默认。
    pub fn set_type_order(&self, direction: Direction, order: &[crate::manager::device_type::PortType]) {
        let codes: Vec<u32> = order.iter().map(|t| t.as_u32()).collect();
        let mut data = self.data.write();
        match direction {
            Direction::Output => data.type_order_output = Some(codes),
            Direction::Input => data.type_order_input = Some(codes),
        }
        drop(data);
        self.save();
    }

    /// 设置全局静音（内存 + 持久化）。
    /// `is_input` 为 true 表示输入静音，false 表示输出静音。
    pub fn set_mute(&self, is_input: bool, mute: bool) {
        let mut data = self.data.write();
        if is_input {
            data.mute_input = mute;
        } else {
            data.mute_output = mute;
        }
        drop(data);
        self.save();
    }

    /// 读取全局静音状态。
    pub fn mute(&self, is_input: bool) -> bool {
        let data = self.data.read();
        if is_input {
            data.mute_input
        } else {
            data.mute_output
        }
    }

    /// 设置端口音量（内存 + 持久化）。
    pub fn set_port_volume(&self, card_name: &str, port_name: &str, volume: f64) {
        let mut data = self.data.write();
        let ports = data.port_state.entry(card_name.to_owned()).or_default();
        let st = ports.entry(port_name.to_owned()).or_default();
        st.volume = volume;
        drop(data);
        self.save();
    }

    /// 设置端口平衡（内存 + 持久化）。
    pub fn set_port_balance(&self, card_name: &str, port_name: &str, balance: f64) {
        let mut data = self.data.write();
        let ports = data.port_state.entry(card_name.to_owned()).or_default();
        let st = ports.entry(port_name.to_owned()).or_default();
        st.balance = balance;
        drop(data);
        self.save();
    }

    /// 读取端口持久化状态（无则默认）。
    pub fn port_state(&self, card_name: &str, port_name: &str) -> PortState {
        let data = self.data.read();
        data.port_state
            .get(card_name)
            .and_then(|m| m.get(port_name))
            .cloned()
            .unwrap_or_default()
    }
    /// 将持久化配置应用到 DeviceManager（启动时调用）。
    ///
    /// 类型顺序直接设置；卡名映射回当前 `card_id`：只有当前存在的卡才应用。
    pub fn apply_to(&self, dm: &mut DeviceManager) {
        let data = self.data.read();

        // 类型优先级顺序（过滤无效编码）
        use crate::manager::device_type::PortType;
        if let Some(codes) = &data.type_order_output {
            let order: Vec<PortType> = codes
                .iter()
                .map(|c| PortType::from_u32(*c))
                .filter(|t| *t != PortType::Unknown)
                .collect();
            dm.output_priority.set_type_order(order);
        }
        if let Some(codes) = &data.type_order_input {
            let order: Vec<PortType> = codes
                .iter()
                .map(|c| PortType::from_u32(*c))
                .filter(|t| *t != PortType::Unknown)
                .collect();
            dm.input_priority.set_type_order(order);
        }

        // 禁用端口：按卡名找当前 card_id
        for (card_name, ports) in &data.disabled_ports {
            let Some((id, _)) = dm.cards.iter().find(|(_, c)| &c.name == card_name) else {
                continue;
            };
            for port in ports {
                dm.disabled_ports.insert((*id, port.clone()));
            }
        }

        // 用户首选端口
        if let Some(k) = &data.user_prefer_output {
            dm.output_priority.set_user_prefer(&k.card_name, &k.port_name);
        }
        if let Some(k) = &data.user_prefer_input {
            dm.input_priority.set_user_prefer(&k.card_name, &k.port_name);
        }

        dm.refresh_priority();
    }
}

impl Default for AudioConfig {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::manager::card::Card;
    use crate::manager::card::CardStatus;
    use crate::manager::card::PortInfo;

    fn tmp_path(tag: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("dde-audio-config-test-{tag}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir.join("audio-config.json")
    }

    fn mk_card(index: u32, name: &str) -> Card {
        Card {
            index,
            name: name.to_owned(),
            active_profile: String::new(),
            ports: vec![PortInfo {
                name: "analog-output".into(),
                enabled: true,
                bluetooth: false,
                description: String::new(),
                direction: 1,
                profiles: vec![],
                priority: 0,
            }],
            profiles: vec![],
            status: CardStatus::Ready,
            change: None,
        }
    }

    #[test]
    fn roundtrip() {
        let path = tmp_path("roundtrip");
        let cfg = AudioConfig::with_path(path.clone());
        cfg.set_port_enabled("alsa.1", "hdmi-output", false);
        cfg.set_port_enabled("alsa.1", "analog-output", false);
        cfg.set_user_prefer(
            Direction::Output,
            PortKey {
                card_name: "alsa.1".into(),
                port_name: "analog-output".into(),
            },
        );

        // 重新加载应还原全部状态
        let cfg2 = AudioConfig::with_path(path);
        cfg2.load();
        let data = cfg2.data.read();
        assert_eq!(
            data.disabled_ports.get("alsa.1").map(|v| v.as_slice()),
            Some(&["hdmi-output".to_owned(), "analog-output".to_owned()][..])
        );
        assert_eq!(
            data.user_prefer_output.as_ref().map(|k| k.port_name.as_str()),
            Some("analog-output")
        );
    }

    #[test]
    fn save_is_atomic() {
        let path = tmp_path("atomic");
        let cfg = AudioConfig::with_path(path.clone());
        cfg.set_port_enabled("usb.2", "usb-output", false);

        // 写完后不应残留 tmp 文件
        assert!(!path.with_extension("json.tmp").exists());
        // 文件内容为合法 JSON
        let raw = fs::read_to_string(&path).unwrap();
        serde_json::from_str::<PersistData>(&raw).unwrap();
    }

    #[test]
    fn apply_maps_card_name_to_id() {
        let path = tmp_path("apply");
        let cfg = AudioConfig::with_path(path.clone());
        cfg.set_port_enabled("alsa.1", "hdmi-output", false);

        let mut dm = DeviceManager::default();
        dm.cards.insert(7, mk_card(7, "alsa.1"));
        dm.cards.insert(9, mk_card(9, "usb.2"));
        dm.refresh_priority();

        cfg.apply_to(&mut dm);
        assert!(dm.disabled_ports.contains(&(7, "hdmi-output".to_owned())));
        // 不存在的卡不产生孤儿项
        assert_eq!(dm.disabled_ports.len(), 1);
    }

    #[test]
    fn type_order_roundtrip() {
        use crate::manager::device_type::PortType;

        let path = tmp_path("typeorder");
        let cfg = AudioConfig::with_path(path.clone());
        cfg.set_type_order(
            Direction::Output,
            &[PortType::Usb, PortType::Hdmi, PortType::Builtin],
        );

        // 重新加载应还原类型顺序
        let cfg2 = AudioConfig::with_path(path);
        cfg2.load();
        let data = cfg2.data.read();
        assert_eq!(
            data.type_order_output.as_deref(),
            Some(&[2u32, 4, 3][..]) // Usb=2, Hdmi=4, Builtin=3
        );
        assert!(data.type_order_input.is_none());
    }

    #[test]
    fn apply_type_order_to_priority() {
        use crate::manager::device_type::PortType;

        let path = tmp_path("typeorder-apply");
        let cfg = AudioConfig::with_path(path.clone());
        // 配置：Usb 排最前（默认 Builtin 才排前）
        cfg.set_type_order(Direction::Output, &[PortType::Usb, PortType::Builtin]);

        let mut dm = DeviceManager::default();
        dm.cards.insert(1, mk_card(1, "alsa.1"));
        // USB 卡：端口名含 usb → detect_port_type = Usb
        dm.cards.insert(2, mk_card(2, "usb.2"));
        dm.refresh_priority();
        cfg.apply_to(&mut dm);

        // 配置生效后 Usb 类型优先于 Builtin
        let p = dm
            .output_priority
            .prefer_port(|_| true)
            .expect("should have a port");
        // usb.2 的列举顺序无关，类型排序 Usb>Builtin 应使 usb 卡端口胜出
        assert!(p.card_name.contains("usb"), "Usb type wins, got {}", p.card_name);
    }

}
