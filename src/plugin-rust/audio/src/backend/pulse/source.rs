// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

use super::PulseManager;

/// 设置 Source 音量。
///
/// value == 0 时静音，否则取消静音（与 Go 版行为一致）。
pub fn set_volume(
    pulse: &PulseManager,
    index: u32,
    value: f64,
    _is_play: bool,
) -> Result<(), String> {
    use libpulse_binding::volume::Volume;
    use libpulse_binding::volume::ChannelVolumes;

    fn volume_from_float(v: f64) -> Volume {
        Volume((v * Volume::NORMAL.0 as f64) as u32)
    }
    fn cvolume_from_float(v: f64, ch: u8) -> ChannelVolumes {
        let mut cv = ChannelVolumes::default();
        cv.set_len(ch);
        cv.set(ch, volume_from_float(v));
        cv
    }

    let ok: bool = pulse.execute(|ctx, tx| {
        let mut intro = ctx.introspect();
        let cv = cvolume_from_float(value, 2);
        intro.set_source_volume_by_index(index, &cv, Some(Box::new(move |ok| {
            let _ = tx.send(ok);
        })));
        true
    })?;
    if !ok {
        return Err(format!("set volume failed for source {index}"));
    }
    Ok(())
}

/// 设置 Source 静音。
pub fn set_mute(pulse: &PulseManager, index: u32, value: bool) -> Result<(), String> {
    let ok: bool = pulse.execute(|ctx, tx| {
        let mut intro = ctx.introspect();
        intro.set_source_mute_by_index(index, value, Some(Box::new(move |ok| {
            let _ = tx.send(ok);
        })));
        true
    })?;
    if !ok {
        return Err(format!("set mute failed for source {index}"));
    }
    Ok(())
}

/// 设置 Source 左右声道平衡。
///
/// 查询设备当前的 ChannelVolumes 和 ChannelMap，调整后再设置。
pub fn set_balance(
    pulse: &PulseManager,
    index: u32,
    value: f64,
    _is_play: bool,
) -> Result<(), String> {
    use libpulse_binding::callbacks::ListResult;
    use libpulse_binding::volume::ChannelVolumes;

    let (volume, map) = pulse.execute(|ctx, tx| {
        let intro = ctx.introspect();
        let mut volume: Option<ChannelVolumes> = None;
        let mut map: Option<libpulse_binding::channelmap::Map> = None;
        intro.get_source_info_by_index(index, move |res| {
            match res {
                ListResult::Item(info) => {
                    volume = Some(info.volume.clone());
                    map = Some(info.channel_map.clone());
                }
                ListResult::End | ListResult::Error => {
                    let _ = tx.send((volume.take(), map.take()));
                }
            }
        });
        true
    })?;

    let (mut volume, map) = match (volume, map) {
        (Some(v), Some(m)) => (v, m),
        _ => return Err("get source volume info failed".into()),
    };
    volume.set_balance(&map, value as f32);

    let result: bool = pulse.execute(|ctx, tx| {
        let mut intro = ctx.introspect();
        intro.set_source_volume_by_index(index, &volume, Some(Box::new(move |ok| {
            let _ = tx.send(ok);
        })));
        true
    })?;
    if !result {
        return Err("set balance failed".into());
    }
    Ok(())
}

/// 设置 Source 前后声道平衡。
pub fn set_fade(pulse: &PulseManager, index: u32, value: f64) -> Result<(), String> {
    use libpulse_binding::callbacks::ListResult;
    use libpulse_binding::volume::ChannelVolumes;

    let (volume, map) = pulse.execute(|ctx, tx| {
        let intro = ctx.introspect();
        let mut volume: Option<ChannelVolumes> = None;
        let mut map: Option<libpulse_binding::channelmap::Map> = None;
        intro.get_source_info_by_index(index, move |res| {
            match res {
                ListResult::Item(info) => {
                    volume = Some(info.volume.clone());
                    map = Some(info.channel_map.clone());
                }
                ListResult::End | ListResult::Error => {
                    let _ = tx.send((volume.take(), map.take()));
                }
            }
        });
        true
    })?;

    let (mut volume, map) = match (volume, map) {
        (Some(v), Some(m)) => (v, m),
        _ => return Err("get source volume info failed".into()),
    };
    volume.set_fade(&map, value as f32);

    let result: bool = pulse.execute(|ctx, tx| {
        let mut intro = ctx.introspect();
        intro.set_source_volume_by_index(index, &volume, Some(Box::new(move |ok| {
            let _ = tx.send(ok);
        })));
        true
    })?;
    if !result {
        return Err("set fade failed".into());
    }
    Ok(())
}

/// 设置 Source 端口。
pub fn set_port(pulse: &PulseManager, index: u32, name: &str) -> Result<(), String> {
    let ok: bool = pulse.execute(|ctx, tx| {
        let mut intro = ctx.introspect();
        intro.set_source_port_by_index(index, &name, Some(Box::new(move |ok| {
            let _ = tx.send(ok);
        })));
        true
    })?;
    if !ok {
        return Err(format!("set port failed for source {index}"));
    }
    Ok(())
}

/// 获取 Source 音量计量器。
#[allow(dead_code)]
pub fn get_meter(_pulse: &PulseManager, _index: u32) -> Result<u32, String> {
    // TODO: 创建 record stream 作为 meter
    Err("unimplemented".into())
}

/// Source 信息（backend 表示，与 manager 解耦）。
#[derive(Clone, Debug)]
pub struct BackendSource {
    pub index: u32,
    pub name: String,
    pub description: String,
    /// 相对 Volume::NORMAL 的基准音量。
    pub base_volume: f64,
    pub mute: bool,
    pub volume: f64,
    pub balance: f64,
    pub fade: f64,
    pub ports: Vec<super::sink::BackendPort>,
    pub active_port: super::sink::BackendPort,
    pub card: u32,
}

/// 查询单个 Source 信息。
pub fn query_info(
    pulse: &PulseManager,
    index: u32,
) -> Result<BackendSource, String> {
    use libpulse_binding::callbacks::ListResult;

    pulse.execute(|ctx, tx| {
        let mut state: Option<BackendSource> = None;
        ctx.introspect().get_source_info_by_index(index, move |res| {
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
    .ok_or_else(|| format!("source {index} not found"))
}

/// 查询所有 Source 信息。
pub fn query_list(pulse: &PulseManager) -> Result<Vec<BackendSource>, String> {
    use libpulse_binding::callbacks::ListResult;

    pulse.execute(|ctx, tx| {
        let mut list: Vec<BackendSource> = Vec::new();
        ctx.introspect().get_source_info_list(move |res| {
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

fn state_from_info(info: &libpulse_binding::context::introspect::SourceInfo) -> BackendSource {
    use libpulse_binding::volume::Volume;
    use super::sink::BackendPort;

    let ports = info
        .ports
        .iter()
        .map(|p| BackendPort {
            name: p.name.as_deref().unwrap_or("").to_owned(),
            description: p.description.as_deref().unwrap_or("").to_owned(),
            direction: 2, // source 方向固定为输入（D-Bus 契约：2=输入）
            available: p.available as u8,
        })
        .collect();

    let active_port = info.active_port.as_ref().map(|p| BackendPort {
        name: p.name.as_deref().unwrap_or("").to_owned(),
        description: p.description.as_deref().unwrap_or("").to_owned(),
        direction: 2,
        available: p.available as u8,
    }).unwrap_or_else(|| BackendPort {
        name: String::new(),
        description: String::new(),
        direction: 2,
        available: 0,
    });

    let vol = info.volume.avg();
    let base = info.base_volume;

    BackendSource {
        index: info.index,
        name: info.name.as_deref().unwrap_or("").to_owned(),
        description: info.description.as_deref().unwrap_or("").to_owned(),
        base_volume: base.0 as f64 / Volume::NORMAL.0 as f64,
        mute: info.mute,
        volume: vol.0 as f64 / Volume::NORMAL.0 as f64,
        balance: info.volume.get_balance(&info.channel_map) as f64,
        fade: info.volume.get_fade(&info.channel_map) as f64,
        ports,
        active_port,
        card: info.card.unwrap_or(u32::MAX),
    }
}
