// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

use super::PulseManager;

/// 设置声卡 profile。
#[allow(dead_code)]
pub fn set_card_profile(
    pulse: &PulseManager,
    card_id: u32,
    profile_name: &str,
) -> Result<(), String> {
    let profile_name = profile_name.to_owned();
    let ok: bool = pulse.execute(|ctx, tx| {
        let mut intro = ctx.introspect();
        intro.set_card_profile_by_index(card_id, &profile_name, Some(Box::new(move |ok| {
            let _ = tx.send(ok);
        })));
        true
    })?;
    if !ok {
        return Err(format!("set card profile failed for card {card_id}"));
    }
    Ok(())
}

/// 设置端口启用/禁用。
///
/// libpulse 无直接 API，通过查询声卡端口所属 profile 并切换实现。
/// 端口启用状态（enabled 持久化）由上层 DeviceManager 管理（TODO）。
#[allow(dead_code)]
pub fn set_port_enabled(
    pulse: &PulseManager,
    card_id: u32,
    port_name: &str,
    _enabled: bool,
) -> Result<(), String> {
    use libpulse_binding::callbacks::ListResult;

    let port_name = port_name.to_owned();

    // 查询声卡，找到端口对应的 profile 名称
    let profile_name: Option<String> = pulse.execute(|ctx, tx| {
        let intro = ctx.introspect();
        let mut profile: Option<String> = None;
        let port = port_name.clone();
        intro.get_card_info_by_index(card_id, move |res| {
            match res {
                ListResult::Item(info) => {
                    for p in &info.ports {
                        if p.name.as_deref() == Some(port.as_str()) {
                            if let Some(prof) = p.profiles.first() {
                                profile = prof.name.as_deref().map(|n| n.to_owned());
                            }
                            break;
                        }
                    }
                }
                ListResult::End | ListResult::Error => {
                    let _ = tx.send(profile.take());
                }
            }
        });
        true
    })?;

    match profile_name {
        Some(name) => set_card_profile(pulse, card_id, &name),
        None => Err(format!("port {port_name} not found on card {card_id}")),
    }
}

/// 查询端口是否启用。
#[allow(dead_code)]
pub fn is_port_enabled(
    pulse: &PulseManager,
    card_id: u32,
    port_name: &str,
) -> Result<bool, String> {
    use libpulse_binding::callbacks::ListResult;

    let port_name = port_name.to_owned();

    pulse.execute(|ctx, tx| {
        let intro = ctx.introspect();
        let mut enabled = false;
        intro.get_card_info_by_index(card_id, move |res| {
            match res {
                ListResult::Item(info) => {
                    for port in &info.ports {
                        if port.name.as_deref() == Some(port_name.as_str()) {
                            enabled = port.available != libpulse_binding::def::PortAvailable::No;
                            break;
                        }
                    }
                }
                ListResult::End | ListResult::Error => {
                    let _ = tx.send(enabled);
                }
            }
        });
        true
    })
}

/// 声卡端口信息（backend 表示，与 manager 解耦）。
#[derive(Clone, Debug)]
pub struct BackendCardPort {
    pub name: String,
    pub description: String,
    pub direction: u32,
    /// 端口关联的 profile（含优先级/可用性，用于选最优）。
    pub profiles: Vec<BackendCardProfile>,
    /// 端口权重（越高越适合作为默认）。
    pub priority: u32,
    /// 端口是否可用。
    pub available: bool,
}

/// 声卡 profile 信息（backend 表示，与 manager 解耦）。
#[derive(Clone, Debug)]
pub struct BackendCardProfile {
    pub name: String,
    pub description: String,
    /// 越高越适合作为默认。
    pub priority: u32,
    /// 是否可用。
    pub available: bool,
}

/// 声卡信息（backend 表示，与 manager 解耦）。
#[derive(Clone, Debug)]
pub struct BackendCard {
    pub index: u32,
    pub name: String,
    pub active_profile: String,
    pub ports: Vec<BackendCardPort>,
    pub profiles: Vec<BackendCardProfile>,
}

/// 查询单个声卡信息。
pub fn query_info(
    pulse: &PulseManager,
    index: u32,
) -> Result<BackendCard, String> {
    use libpulse_binding::callbacks::ListResult;

    pulse.execute(|ctx, tx| {
        let mut state: Option<BackendCard> = None;
        ctx.introspect().get_card_info_by_index(index, move |res| {
            match res {
                ListResult::Item(info) => {
                    state = Some(state_from_info(info));
                }
                ListResult::End | ListResult::Error => {
                    let _ = tx.send(state.take());
                }
            }
        });
        true
    })?
    .ok_or_else(|| format!("card {index} not found"))
}

/// 查询所有声卡信息。
pub fn query_list(pulse: &PulseManager) -> Result<Vec<BackendCard>, String> {
    use libpulse_binding::callbacks::ListResult;

    pulse.execute(|ctx, tx| {
        let mut list: Vec<BackendCard> = Vec::new();
        ctx.introspect().get_card_info_list(move |res| {
            match res {
                ListResult::Item(info) => {
                    list.push(state_from_info(info));
                }
                ListResult::End => {
                    let _ = tx.send(std::mem::take(&mut list));
                }
                ListResult::Error => {
                    let _ = tx.send(Vec::new());
                }
            }
        });
        true
    })
}

fn state_from_info(info: &libpulse_binding::context::introspect::CardInfo) -> BackendCard {
    use libpulse_binding::def::PortAvailable;

    let ports = info
        .ports
        .iter()
        .map(|p| BackendCardPort {
            name: p.name.as_deref().unwrap_or("").to_owned(),
            description: p.description.as_deref().unwrap_or("").to_owned(),
            // D-Bus 契约：直接取 pulse 方向位掩码（1=输出，2=输入，3=双向），
            // 与 Go 版 `Direction: int(c.direction)`、控制中心 Port::Out=1/In=2 一致。
            direction: p.direction.bits() as u32,
            profiles: p.profiles.iter()
                .map(|prof| BackendCardProfile {
                    name: prof.name.as_deref().unwrap_or("").to_owned(),
                    description: prof.description.as_deref().unwrap_or("").to_owned(),
                    priority: prof.priority,
                    available: prof.available,
                })
                .collect(),
            priority: p.priority,
            available: p.available != PortAvailable::No,
        })
        .collect();

    let profiles = info
        .profiles
        .iter()
        .map(|p| BackendCardProfile {
            name: p.name.as_deref().unwrap_or("").to_owned(),
            description: p.description.as_deref().unwrap_or("").to_owned(),
            priority: p.priority,
            available: p.available,
        })
        .collect();

    BackendCard {
        index: info.index,
        name: info.name.as_deref().unwrap_or("").to_owned(),
        active_profile: info.active_profile.as_ref()
            .and_then(|p| p.name.as_deref())
            .unwrap_or("")
            .to_owned(),
        ports,
        profiles,
    }
}
