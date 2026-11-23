// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! backend/pulse 集成测试。
//!
//! 这些测试连接真实 PulseAudio / PipeWire-pulse 守护进程，
//! 依赖本机音频环境，默认忽略。有音频服务时用
//! `cargo test -- --ignored` 显式运行。

use std::sync::Arc;

use plugin_rust_audio::backend::pulse::meter::MeterBackend;
use plugin_rust_audio::backend::pulse::{card, sink, sink_input, source, PulseManager};

/// 连接 pulse daemon，查询所有设备列表。
#[test]
#[ignore]
fn query_all_devices() {
    let (pulse, _rx) = PulseManager::new().expect("connect to pulse daemon");

    let cards = card::query_list(&pulse).expect("query cards");
    let sinks = sink::query_list(&pulse).expect("query sinks");
    let sources = source::query_list(&pulse).expect("query sources");
    let sink_inputs = sink_input::query_list(&pulse).expect("query sink inputs");

    eprintln!("cards: {}", cards.len());
    for c in &cards {
        eprintln!("  card {}: {} profile={}", c.index, c.name, c.active_profile);
    }
    eprintln!("sinks: {}", sinks.len());
    for s in &sinks {
        eprintln!(
            "  sink {}: {} desc={} vol={} mute={} card={}",
            s.index, s.name, s.description, s.volume, s.mute, s.card
        );
    }
    eprintln!("sources: {}", sources.len());
    for s in &sources {
        eprintln!(
            "  source {}: {} desc={} vol={} mute={} card={}",
            s.index, s.name, s.description, s.volume, s.mute, s.card
        );
    }
    eprintln!("sink inputs: {}", sink_inputs.len());
    for si in &sink_inputs {
        eprintln!(
            "  sink input {}: {} vol={} mute={}",
            si.index, si.name, si.volume, si.mute
        );
    }

    // 至少应该有 card 和 sink（物理音频设备）
    assert!(!cards.is_empty(), "should have at least one card");
}

/// 验证默认 sink/source 查询。
#[test]
#[ignore]
fn query_default_sink_source() {
    let (pulse, _rx) = PulseManager::new().expect("connect to pulse daemon");
    let (sink, source) = pulse.default_sink_source().expect("query default sink/source");
    eprintln!("default sink: {sink}");
    eprintln!("default source: {source}");
}

/// 验证 source 音量计量：创建 meter，采集后读峰值。
#[test]
#[ignore]
fn source_meter_peak() {
    let (pulse, _rx) = PulseManager::new().expect("connect to pulse daemon");
    let pulse = Arc::new(pulse);
    let (_, source_name) = pulse.default_sink_source().expect("query default source");
    assert!(!source_name.is_empty(), "should have a default source");

    let source_index = source::query_list(&pulse)
        .expect("query sources")
        .into_iter()
        .find(|s| s.name == source_name)
        .map(|s| s.index)
        .expect("default source should exist");

    let meter = pulse
        .create_source_meter(source_index)
        .expect("create source meter");

    // 短暂采集（meter 回调在 mainloop 线程，25Hz 采样）
    std::thread::sleep(std::time::Duration::from_millis(500));
    let peak = meter.peak();
    eprintln!("source {source_index} ({source_name}) peak: {peak}");
    // 峰值应落在合法范围 [0, 1]
    assert!((0.0..=1.0).contains(&peak), "peak out of range: {peak}");
}
