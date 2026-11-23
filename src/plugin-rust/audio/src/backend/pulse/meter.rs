// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! Source 音量计量（meter）后端。
//!
//! 仿 go-lib/pulse/meter.go：为指定 source 创建 record stream，
//! 用 `PA_STREAM_PEAK_DETECT` 标志让服务端做峰值检测，read callback
//! 里 `peek` 取最后一个 float 作为峰值。纯 libpulse-binding 实现，无 C 代码。

use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Mutex};

use libpulse_binding::context::Context;
use libpulse_binding::def::BufferAttr;
use libpulse_binding::sample::{Format, Spec};
use libpulse_binding::stream::{FlagSet, PeekResult, Stream};

use super::PulseManager;

/// 采样规格：FLOAT32 单声道，25 Hz。
///
/// 与 Go 版 `createMonitorStreamForSource` 一致（rate=25, channels=1）。
const SAMPLE_RATE: u32 = 25;

/// 音量计量器抽象。
///
/// manager 通过此 trait 读取实时峰值，与具体 libpulse stream 解耦；
/// 测试可注入 mock 实现，无需连接真实 PulseAudio。
pub trait MeterBackend: Send + Sync {
    /// 最近一次采样的峰值（0.0~1.0）。
    fn peak(&self) -> f32;
}

/// Source 音量计量器。
///
/// 持有 record stream，`peak()` 返回最近一次采样的峰值（0.0~1.0）。
/// Drop 时 stream 的 Arc 释放，`Stream::drop` 自动 disconnect + unref。
pub struct SourceMeter {
    source_index: u32,
    /// 持有 record stream 引用以保活。
    ///
    /// Drop 时必须先 `take()` 出来，并在 PulseAudio mainloop 锁内释放，
    /// 保证 libpulse-binding 的 `Stream::drop`（disconnect + unref）不会与
    /// mainloop 线程并发。
    stream: Option<Arc<Mutex<Stream>>>,
    peak: Arc<AtomicU32>,
    /// 所属连接，用于保证释放 stream 时 PulseAudio mainloop 仍存活，
    /// 并可在 mainloop 锁内注销 read callback。
    pulse: Arc<PulseManager>,
}

impl SourceMeter {
    /// 创建 source 的峰值检测 stream。
    ///
    /// 必须在 mainloop lock 下调用（`Stream::new` 需要 `&mut Context`）。
    pub fn create(
        ctx: &mut Context,
        source_index: u32,
        pulse: Arc<PulseManager>,
    ) -> Result<Arc<Self>, String> {
        let spec = Spec {
            format: Format::FLOAT32NE,
            rate: SAMPLE_RATE,
            channels: 1,
        };

        let stream = Stream::new(ctx, "Peak detect", &spec, None)
            .ok_or_else(|| format!("create monitor stream failed for source {source_index}"))?;
        let stream = Arc::new(Mutex::new(stream));

        // 峰值检测流：短小缓冲区，足够放下一个 float 即触发回调
        let attr = BufferAttr {
            maxlength: u32::MAX,
            fragsize: std::mem::size_of::<f32>() as u32,
            ..Default::default()
        };

        let peak = Arc::new(AtomicU32::new(0));
        let flags = FlagSet::DONT_MOVE | FlagSet::PEAK_DETECT | FlagSet::ADJUST_LATENCY;

        {
            let mut s = stream.lock().map_err(|e| format!("stream mutex poisoned: {e}"))?;
            // 回调捕获 Weak，避免 Arc 循环导致 stream 永不释放
            let stream_weak = Arc::downgrade(&stream);
            let peak_clone = peak.clone();
            s.set_read_callback(Some(Box::new(move |_len| {
                if let Some(s) = stream_weak.upgrade() {
                    let Ok(mut s) = s.lock() else { return };
                    if let Ok(PeekResult::Data(data)) = s.peek() {
                        if data.len() >= 4 {
                            // 取最后一个 float（单声道峰值）
                            let v = unsafe {
                                std::ptr::read_unaligned(
                                    data.as_ptr().add(data.len() - 4) as *const f32,
                                )
                            };
                            let v = v.clamp(0.0, 1.0);
                            peak_clone.store(v.to_bits(), Ordering::Relaxed);
                        }
                        let _ = s.discard();
                    }
                }
            })));

            if let Err(e) = s.connect_record(Some(&source_index.to_string()), Some(&attr), flags) {
                // Stream 即将因错误路径释放；必须在连接失败时同步注销回调，
                // 否则 PulseAudio 仍持有请求回调中的闭包裸指针。
                s.set_read_callback(None);
                return Err(format!(
                    "connect monitor stream failed for source {source_index}: {e}"
                ));
            }
        }

        Ok(Arc::new(Self {
            source_index,
            stream: Some(stream),
            peak,
            pulse,
        }))
    }

}

impl Drop for SourceMeter {
    fn drop(&mut self) {
        // Stream::drop 会释放 read callback 中的闭包，但 PulseAudio
        // mainloop 可能仍持有该闭包指针。必须在 mainloop 锁内先清除
        // callback，再让 Stream 执行 disconnect/unref。
        let Some(stream) = self.stream.take() else {
            return;
        };
        self.pulse.destroy_source_meter(stream);
    }
}

impl MeterBackend for SourceMeter {
    fn peak(&self) -> f32 {
        f32::from_bits(self.peak.load(Ordering::Relaxed))
    }
}

impl std::fmt::Debug for SourceMeter {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SourceMeter")
            .field("source_index", &self.source_index)
            .field("peak", &self.peak())
            .finish()
    }
}
