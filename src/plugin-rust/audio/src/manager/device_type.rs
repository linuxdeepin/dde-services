// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! 端口设备类型识别。
//!
//! 依据声卡名/端口名中的关键字，识别端口的设备类型（蓝牙/耳麦/USB/内置/HDMI…）。
//! 这是设备类型识别的单一来源，供 Cards 属性 PortType 序列化、
//! 以及后续的端口优先级策略共用。
//!
//! 来源：Go 版 `audio1/priority_old.go` 的 `GetIconPortType`（活跃的 D-Bus 图标识别）。

/// 端口设备类型。
///
/// 值对应 Go 版 `PortType` 常量（与 D-Bus Cards 属性 PortType 字段一致）。
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PortType {
    /// 蓝牙音频。
    Bluetooth,
    /// 3.5mm 耳机插孔。
    Headset,
    /// USB 设备。
    Usb,
    /// 内置扬声器/话筒。
    Builtin,
    /// HDMI。
    Hdmi,
    /// 线缆输入输出（Line In/Out）。
    LineIO,
    /// 多声道。
    MultiChannel,
    /// 其他/未知。
    Unknown,
}

impl PortType {
    /// 对应 D-Bus/Go 用整数编码（0~7）。
    pub fn as_u32(self) -> u32 {
        match self {
            PortType::Bluetooth => 0,
            PortType::Headset => 1,
            PortType::Usb => 2,
            PortType::Builtin => 3,
            PortType::Hdmi => 4,
            PortType::LineIO => 5,
            PortType::MultiChannel => 6,
            PortType::Unknown => 7,
        }
    }

    /// 从整数编码还原（D-Bus 属性读入时用）。
    pub fn from_u32(v: u32) -> PortType {
        match v {
            0 => PortType::Bluetooth,
            1 => PortType::Headset,
            2 => PortType::Usb,
            3 => PortType::Builtin,
            4 => PortType::Hdmi,
            5 => PortType::LineIO,
            6 => PortType::MultiChannel,
            _ => PortType::Unknown,
        }
    }
}

/// 识别规则：每类型与其匹配的关键字。
///
/// 顺序即优先级（先命中者胜），与 Go `GetIconPortType` 一致：
/// LineIO > Builtin > Headset > Hdmi > Bluetooth > Usb。
///
/// 注意：`Headset` 不含 `usb` 关键字 —— USB 耳机若非 HDSP 也命中 `Headset`
/// 关键字（headset/headphone），则归 Headset；否则归 Usb。这一归属规则
/// 后续随统一识别算法设计时再定，此处先按 Go 迁移保持行为一致。
pub fn detect_port_type(card_name: &str, port_name: &str) -> PortType {
    const RULES: &[(PortType, &[&str])] = &[
        (PortType::LineIO, &["linein", "lineout"]),
        (PortType::Builtin, &["speaker", "input-mic"]),
        (PortType::Headset, &["rear-mic", "front-mic", "headset", "headphone"]),
        (PortType::Hdmi, &["hdmi"]),
        (PortType::Bluetooth, &["bluez", "bluetooth"]),
        (PortType::Usb, &["usb"]),
    ];

    for &(t, keywords) in RULES {
        for &k in keywords {
            if contains_keyword(card_name, port_name, k) {
                return t;
            }
        }
    }
    PortType::Unknown
}

/// 判断声卡名/端口名是否包含某关键字（不区分大小写）。
fn contains_keyword(card_name: &str, port_name: &str, keyword: &str) -> bool {
    card_name.to_lowercase().contains(keyword)
        || port_name.to_lowercase().contains(keyword)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detect_lineio() {
        assert_eq!(detect_port_type("card-audio", "analog-lineout"), PortType::LineIO);
        assert_eq!(detect_port_type("card", "analog-linein"), PortType::LineIO);
    }

    #[test]
    fn detect_builtin() {
        // Builtin 关键字为 speaker / input-mic（与 Go 一致，analog-output 不匹配）
        assert_eq!(detect_port_type("alsa", "speaker"), PortType::Builtin);
        assert_eq!(detect_port_type("alsa", "input-mic"), PortType::Builtin);
    }

    #[test]
    fn detect_headset() {
        assert_eq!(detect_port_type("alsa", "front-headphone"), PortType::Headset);
        assert_eq!(detect_port_type("alsa", "analog-input-headset-mic"), PortType::Headset);
    }

    #[test]
    fn detect_hdmi() {
        assert_eq!(detect_port_type("hdmi-pci", "hdmi-output-0"), PortType::Hdmi);
    }

    #[test]
    fn detect_usb() {
        assert_eq!(detect_port_type("usb-card", "usb-output"), PortType::Usb);
    }

    #[test]
    fn detect_unknown() {
        assert_eq!(detect_port_type("nonsense", "unknown-port"), PortType::Unknown);
    }

    #[test]
    fn case_insensitive() {
        assert_eq!(detect_port_type("HDMI", "OUTPUT-HDMI"), PortType::Hdmi);
    }

    #[test]
    fn as_u32_roundtrip() {
        for t in [PortType::Bluetooth, PortType::Headset, PortType::Usb,
                  PortType::Builtin, PortType::Hdmi, PortType::LineIO,
                  PortType::MultiChannel, PortType::Unknown] {
            assert_eq!(PortType::from_u32(t.as_u32()), t);
        }
    }
}