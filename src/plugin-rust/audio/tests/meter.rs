// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! Meter 业务逻辑集成测试。
//!
//! 使用 mock 注入 `MeterBackend`/`MeterCleanup`，不依赖真实 PulseAudio / D-Bus，
//! 可在任何环境运行（CI、无音频服务机器）。

use std::sync::Arc;
use std::time::Duration;

use parking_lot::RwLock;

use plugin_rust_audio::manager::device_manager::DeviceManager;
use plugin_rust_audio::manager::meter::{Meter, MeterCleanup};
use plugin_rust_audio::backend::pulse::meter::MeterBackend;

/// mock 峰值来源。
struct MockBackend {
    peak: f32,
}
impl MeterBackend for MockBackend {
    fn peak(&self) -> f32 {
        self.peak
    }
}

/// mock 清理动作，记录被注销的路径。
struct MockCleanup {
    removed: parking_lot::Mutex<Vec<String>>,
}
impl MeterCleanup for MockCleanup {
    fn remove(&self, path: &str) {
        self.removed.lock().push(path.to_owned());
    }
}

fn mock_cleanup() -> (Arc<MockCleanup>, Arc<dyn MeterCleanup>) {
    let c = Arc::new(MockCleanup {
        removed: parking_lot::Mutex::new(Vec::new()),
    });
    (c.clone(), c)
}

/// source meter：Volume 来自 backend 峰值。
#[test]
fn volume_from_backend_peak() {
    let dm = Arc::new(RwLock::new(DeviceManager::default()));
    let (_, cleanup) = mock_cleanup();
    let backend: Arc<dyn MeterBackend> = Arc::new(MockBackend { peak: 0.35 });

    let meter = Meter::new(
        "source7".into(),
        7,
        false,
        Some(backend),
        dm,
        cleanup,
    );
    // f32 峰值转 f64 有精度损失，用宽松容差
    assert!((meter.volume() - 0.35).abs() < 1e-6);
}

/// sink meter（无 backend）：Volume 回退读 DeviceManager 静态音量。
#[test]
fn volume_fallback_to_sink_state() {
    use plugin_rust_audio::manager::sink::{Port, Sink};

    let dm = Arc::new(RwLock::new(DeviceManager::default()));
    dm.write().sinks.insert(
        3,
        Sink {
            index: 3,
            name: "alsa".into(),
            description: "x".into(),
            base_volume: 1.0,
            mute: false,
            volume: 0.8,
            balance: 0.0,
            support_balance: true,
            fade: 0.0,
            support_fade: true,
            ports: vec![],
            active_port: Port {
                name: String::new(),
                description: String::new(),
                direction: 1,
                available: 0,
            },
            card: 1,
        },
    );
    let (_, cleanup) = mock_cleanup();
    let meter = Meter::new(
        "sink3".into(),
        3,
        true,
        None,
        dm,
        cleanup,
    );
    assert!((meter.volume() - 0.8).abs() < f64::EPSILON);
}

/// 清理线程：超时未 tick 后从 DeviceManager 移除并调用 cleanup.remove。
#[test]
fn cleanup_after_timeout() {
    let dm = Arc::new(RwLock::new(DeviceManager::default()));
    let (mock_cleanup, cleanup) = mock_cleanup();

    let backend: Arc<dyn MeterBackend> = Arc::new(MockBackend { peak: 0.1 });
    let meter = Meter::new_with_interval(
        "source1".into(),
        1,
        false,
        Some(backend),
        dm.clone(),
        cleanup,
        Duration::from_millis(50),
    );
    dm.write().meters.insert("source1".into(), meter.clone());

    // 不调用 keep_alive，等待超过轮询间隔，清理线程应移除
    std::thread::sleep(Duration::from_millis(200));

    assert!(!dm.read().meters.contains_key("source1"), "meter should be cleaned up");
    let removed = mock_cleanup.removed.lock();
    assert_eq!(removed.len(), 1, "cleanup.remove should be called once");
    assert!(removed[0].contains("MeterSource1"));
}

/// 续命：持续 keep_alive 则 meter 不被清理。
#[test]
fn keep_alive_stays() {
    let dm = Arc::new(RwLock::new(DeviceManager::default()));
    let (_, cleanup) = mock_cleanup();
    let backend: Arc<dyn MeterBackend> = Arc::new(MockBackend { peak: 0.0 });
    let meter = Meter::new_with_interval(
        "source2".into(),
        2,
        false,
        Some(backend),
        dm.clone(),
        cleanup,
        Duration::from_millis(30),
    );
    dm.write().meters.insert("source2".into(), meter.clone());

    // 在清理线程判定超时前持续 keep_alive 续命
    for _ in 0..5 {
        std::thread::sleep(Duration::from_millis(20));
        meter.keep_alive();
    }

    assert!(dm.read().meters.contains_key("source2"), "meter should stay alive");
}
