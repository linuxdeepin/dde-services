// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! 音量变化反馈音。
//!
//! # 职责边界
//!
//! 本模块只决定**是否该出声**，播放交给
//! `org.deepin.dde.SoundEffect1`（dde-daemon `soundeffect1`）。
//! 音频插件不接触 PCM、不管音效主题、不查音效开关——那些是音效
//! 服务的职责，重复实现只会让两处逻辑漂移。
//!
//! # 为什么不自己播
//!
//! 旧 Go 版 audio1 自己播（`util.go:34` → go-lib `sound_effect` →
//! 每次新建 `pa_simple` 流），实测单次 320–600ms，快速调音量时
//! 并发短流在服务端排队、越拖越远，因此加了 500ms `time.AfterFunc`
//! 后沿去抖（`sink.go:403`）掩盖——代价是单按一次音量键也要等
//! 半秒才出声。
//!
//! 而 `SoundEffect1.PlaySound` 内部是 `go func()`（`manager.go:96`），
//! D-Bus 方法立即返回，实测调用方开销中位数 0.37ms。播放实现的
//! 优化（如采样缓存）属于音效服务，在那里改一处，通知音、电源音效、
//! 音量反馈三条路径同时受益。
//!
//! # 节流
//!
//! 仍需前沿节流，但语义与 Go 相反：
//! - Go：后沿去抖，停手后再等 500ms 才响
//! - 本实现：**第一次变化立即出声**，[`THROTTLE`] 窗口内的后续变化
//!   直接丢弃，不排队也不延后
//!
//! 窗口取值小于音效时长（音量变化音效 136ms）以避免同一声音叠放，
//! 同时防止滑条拖动把 D-Bus 刷爆——`SoundEffect1` 并发超过 3 会
//! 直接丢弃请求（`manager.go:33` `allowPlaySoundMaxCount`）。

use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

use parking_lot::Mutex;

/// 音量变化音效的事件名（`SoundEffect1.PlaySound` 的参数，
/// 同时是音效文件名与 dconfig 键名）。
const EVENT_VOLUME_CHANGE: &str = "audio-volume-change";

const SOUND_EFFECT_DEST: &str = "org.deepin.dde.SoundEffect1";
const SOUND_EFFECT_PATH: &str = "/org/deepin/dde/SoundEffect1";
const SOUND_EFFECT_IFACE: &str = "org.deepin.dde.SoundEffect1";

/// 前沿节流窗口。
///
/// 必须**不小于**音效自身时长，否则前后两声会重叠——实测音量变化
/// 音效为 136ms（`audio-volume-change.wav`，44.1kHz/2ch/16bit，
/// 23992 字节 PCM）。取 150ms 留一点余量。
///
/// 窗口过窄的代价是实测出来的：60ms 时连打 20 次会向服务端发出 20
/// 个请求，`SoundEffect1` 并发上限为 3（`manager.go:33`
/// `allowPlaySoundMaxCount`），有 8 次被直接丢弃。既叠音又丢音。
///
/// 150ms 对应最多约 6.7 次/秒，仍远高于人耳能分辨的提示音密度，
/// 且第一次变化**立即**出声（前沿触发），不是 Go 那种停手后才响。
const THROTTLE: Duration = Duration::from_millis(150);

/// 音效反馈发送器。
///
/// 会话总线连接在构造时建立并复用（每次播放新建连接要几毫秒且
/// 可能失败）。播放为异步单向调用，不等回复。
pub struct SoundEffect {
    /// 会话总线连接。`None` 表示不可用，播放静默跳过。
    connection: Option<zbus::blocking::Connection>,
    /// 最近一次提交播放的时刻，用于前沿节流。
    last_play: Mutex<Option<Instant>>,
    /// 音效服务是否可达。首次调用失败后置 false，避免每次调音量
    /// 都撞一次 D-Bus 超时（服务被禁用/卸载的场景）。
    available: AtomicBool,
}

impl SoundEffect {
    /// 构造发送器，复用调用方已有的会话总线连接。
    pub fn new(connection: zbus::blocking::Connection) -> Self {
        Self {
            connection: Some(connection),
            last_play: Mutex::new(None),
            available: AtomicBool::new(true),
        }
    }

    /// 构造一个不播放任何音效的实例（会话总线不可用时）。
    pub fn disabled() -> Self {
        Self {
            connection: None,
            last_play: Mutex::new(None),
            available: AtomicBool::new(false),
        }
    }

    /// 前沿节流闸门：允许本次播放则占用时间窗并返回 true。
    ///
    /// 判定与时间戳更新在同一临界区，避免并发调用都判定为"可播放"
    /// （多个 D-Bus 线程可能同时进 SetVolume）。
    fn try_acquire_slot(&self) -> bool {
        let mut last = self.last_play.lock();
        if last.is_some_and(|t| t.elapsed() < THROTTLE) {
            return false;
        }
        *last = Some(Instant::now());
        true
    }

    /// 播放音量变化反馈音。
    ///
    /// 前沿节流：距上次提交不足 [`THROTTLE`] 则丢弃。
    ///
    /// 静默失败：音效是辅助反馈，任何环节出错都不应影响音量设置
    /// 本身，只记一次日志。
    pub fn play_volume_change(&self) {
        if !self.available.load(Ordering::Relaxed) {
            return;
        }
        let Some(conn) = &self.connection else {
            return;
        };
        if !self.try_acquire_slot() {
            return;
        }

        // 单向调用：不等回复。服务端 PlaySound 内部就是 go func()
        // 立即返回，但 call_method 仍会等一个空回复（约 0.37ms）；
        // 用 no-reply 省掉这次往返，且服务不可用时不会阻塞。
        let msg = zbus::message::Message::method_call(SOUND_EFFECT_PATH, "PlaySound")
            .and_then(|b| b.destination(SOUND_EFFECT_DEST))
            .and_then(|b| b.interface(SOUND_EFFECT_IFACE))
            .and_then(|b| b.with_flags(zbus::message::Flags::NoReplyExpected))
            .and_then(|b| b.build(&(EVENT_VOLUME_CHANGE,)));

        let msg = match msg {
            Ok(m) => m,
            Err(e) => {
                eprintln!("[dde-audio] build sound effect message failed: {e}");
                return;
            }
        };

        if let Err(e) = conn.send(&msg) {
            // 发送失败通常意味着服务不存在（被禁用/卸载）。
            // 标记不可用，避免每次调音量都重试。
            eprintln!("[dde-audio] sound effect unavailable, feedback disabled: {e}");
            self.available.store(false, Ordering::Relaxed);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 连接不可用时播放是空操作，不 panic。
    #[test]
    fn disabled_play_is_noop() {
        let se = SoundEffect::disabled();
        se.play_volume_change();
        se.play_volume_change();
        assert!(!se.available.load(Ordering::Relaxed));
    }

    /// 前沿触发：第一次调用立即获得播放槽位，不像 Go 那样先等 500ms。
    #[test]
    fn first_call_plays_immediately() {
        let se = SoundEffect::disabled();
        assert!(se.try_acquire_slot(), "首次调用必须立即允许播放");
    }

    /// 窗口内的后续调用被丢弃（不排队、不延后）。
    #[test]
    fn throttles_within_window() {
        let se = SoundEffect::disabled();
        assert!(se.try_acquire_slot());
        // 紧接着的连打全部丢弃
        for _ in 0..20 {
            assert!(!se.try_acquire_slot(), "窗口内的调用必须被丢弃");
        }
    }

    /// 窗口过后重新放行。
    #[test]
    fn allows_again_after_window() {
        let se = SoundEffect::disabled();
        assert!(se.try_acquire_slot());
        assert!(!se.try_acquire_slot());
        std::thread::sleep(THROTTLE + Duration::from_millis(20));
        assert!(se.try_acquire_slot(), "窗口过后必须重新放行");
    }

    /// 节流窗口必须覆盖音效自身时长，否则前后两声重叠。
    ///
    /// 音量变化音效实测 136ms（44.1kHz/2ch/16bit，23992 字节 PCM）。
    /// 早先取 60ms 时，实测连打 20 次会向 `SoundEffect1` 发出 20 个
    /// 请求，其并发上限 3（`allowPlaySoundMaxCount`）导致 8 次被丢弃。
    #[test]
    fn throttle_covers_effect_duration() {
        const EFFECT_DURATION: Duration = Duration::from_millis(136);
        assert!(
            THROTTLE >= EFFECT_DURATION,
            "节流窗口 {THROTTLE:?} 必须不小于音效时长 {EFFECT_DURATION:?}，否则叠音"
        );
        // 也不能过大：150ms 对应约 6.7 次/秒，仍是"即时反馈"
        assert!(THROTTLE <= Duration::from_millis(300));
    }
}
