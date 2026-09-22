// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

use super::PulseManager;

/// 设置 SinkInput 音量。
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
        intro.set_sink_input_volume(index, &cv, Some(Box::new(move |ok| {
            let _ = tx.send(ok);
        })));
        true
    })?;
    if !ok {
        return Err(format!("set volume failed for sink input {index}"));
    }
    Ok(())
}

/// 设置 SinkInput 静音。
pub fn set_mute(pulse: &PulseManager, index: u32, value: bool) -> Result<(), String> {
    let ok: bool = pulse.execute(|ctx, tx| {
        let mut intro = ctx.introspect();
        intro.set_sink_input_mute(index, value, Some(Box::new(move |ok| {
            let _ = tx.send(ok);
        })));
        true
    })?;
    if !ok {
        return Err(format!("set mute failed for sink input {index}"));
    }
    Ok(())
}

/// 设置 SinkInput 左右声道平衡。
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
        intro.get_sink_input_info(index, move |res| {
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
        _ => return Err("get sink input volume info failed".into()),
    };
    volume.set_balance(&map, value as f32);

    let result: bool = pulse.execute(|ctx, tx| {
        let mut intro = ctx.introspect();
        intro.set_sink_input_volume(index, &volume, Some(Box::new(move |ok| {
            let _ = tx.send(ok);
        })));
        true
    })?;
    if !result {
        return Err("set balance failed".into());
    }
    Ok(())
}

/// 设置 SinkInput 前后声道平衡。
pub fn set_fade(pulse: &PulseManager, index: u32, value: f64) -> Result<(), String> {
    use libpulse_binding::callbacks::ListResult;
    use libpulse_binding::volume::ChannelVolumes;

    let (volume, map) = pulse.execute(|ctx, tx| {
        let intro = ctx.introspect();
        let mut volume: Option<ChannelVolumes> = None;
        let mut map: Option<libpulse_binding::channelmap::Map> = None;
        intro.get_sink_input_info(index, move |res| {
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
        _ => return Err("get sink input volume info failed".into()),
    };
    volume.set_fade(&map, value as f32);

    let result: bool = pulse.execute(|ctx, tx| {
        let mut intro = ctx.introspect();
        intro.set_sink_input_volume(index, &volume, Some(Box::new(move |ok| {
            let _ = tx.send(ok);
        })));
        true
    })?;
    if !result {
        return Err("set fade failed".into());
    }
    Ok(())
}

/// SinkInput 信息（backend 表示，与 manager 解耦）。
#[derive(Clone, Debug)]
pub struct BackendSinkInput {
    pub index: u32,
    pub name: String,
    pub mute: bool,
    pub volume: f64,
    pub balance: f64,
    pub fade: f64,
    /// 是否应暴露给客户端（对齐 Go `getSinkInputVisible`）。
    ///
    /// 系统音效、通知音、事件音等不是"应用播放流"，不应出现在
    /// 控制中心的应用音量列表里。音效反馈用采样缓存播放时同样会
    /// 产生瞬时 sink-input，若不过滤会导致列表闪烁。
    pub visible: bool,
}

/// 查询单个 SinkInput 信息。
pub fn query_info(
    pulse: &PulseManager,
    index: u32,
) -> Result<BackendSinkInput, String> {
    use libpulse_binding::callbacks::ListResult;

    pulse.execute(|ctx, tx| {
        let mut state: Option<BackendSinkInput> = None;
        ctx.introspect().get_sink_input_info(index, move |res| {
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
    .ok_or_else(|| format!("sink input {index} not found"))
}

/// 查询所有 SinkInput 信息。
pub fn query_list(pulse: &PulseManager) -> Result<Vec<BackendSinkInput>, String> {
    use libpulse_binding::callbacks::ListResult;

    pulse.execute(|ctx, tx| {
        let mut list: Vec<BackendSinkInput> = Vec::new();
        ctx.introspect().get_sink_input_info_list(move |res| {
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

/// 系统音效播放进程的 `application.name`。
///
/// `org.deepin.dde.SoundEffect1` 服务（go-lib `sound_effect` 的
/// pulse 后端，`pulse_backend.go:34`）用此名建流。音量变化反馈音
/// 经该服务播放，因此其瞬时 sink-input 必须过滤。
const SOUND_EFFECT_APP_NAME: &str = "com.deepin.SoundEffect";

/// 判断播放流是否应暴露给客户端。
///
/// 对齐 Go `audio1/sinkinput.go` 的 `getSinkInputVisible`：
/// 先按 `application.name` 排除系统音效/通知进程，再按
/// `media.role` 排除事件音、无障碍提示、测试与滤镜流。
fn is_visible(proplist: &libpulse_binding::proplist::Proplist) -> bool {
    use libpulse_binding::proplist::properties;

    if matches!(
        proplist.get_str(properties::APPLICATION_NAME).as_deref(),
        Some(SOUND_EFFECT_APP_NAME | "deepin-notifications")
    ) {
        return false;
    }

    !matches!(
        proplist.get_str(properties::MEDIA_ROLE).as_deref(),
        Some("event" | "a11y" | "test" | "filter")
    )
}

fn state_from_info(info: &libpulse_binding::context::introspect::SinkInputInfo) -> BackendSinkInput {
    use libpulse_binding::volume::Volume;

    let vol = info.volume.avg();

    BackendSinkInput {
        index: info.index,
        name: info.name.as_deref().unwrap_or("").to_owned(),
        mute: info.mute,
        volume: vol.0 as f64 / Volume::NORMAL.0 as f64,
        balance: info.volume.get_balance(&info.channel_map) as f64,
        fade: info.volume.get_fade(&info.channel_map) as f64,
        visible: is_visible(&info.proplist),
    }
}
