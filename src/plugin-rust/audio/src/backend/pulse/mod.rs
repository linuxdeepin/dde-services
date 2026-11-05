// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! libpulse 连接管理。
//!
//! 持有 threaded mainloop 和 context，所有 Card/Sink/Source/SinkInput
//! 共用同一个 context 连接。mainloop 在独立线程上自动 poll，
//! 回调只做事件转发，不处理业务逻辑。
//!
//! 各对象的设置操作和事件处理在对应子模块中实现，通过 `PulseManager::execute`
//! 复用 lock/submit/unlock/wait 模式：
//! - [`card`] — 声卡状态、端口/profile 管理
//! - [`sink`] — 输出设备状态、音量/静音/平衡/端口操作
//! - [`source`] — 输入设备状态、音量/静音/平衡/端口操作
//! - [`sink_input`] — 播放流状态、音量/静音/平衡操作

pub mod card;
pub mod meter;
pub mod module;
pub mod sink;
pub mod sink_input;
pub mod source;

use std::sync::{Arc, Condvar, Mutex};
use std::time::Duration;

use libpulse_binding::context::{FlagSet, State};
use libpulse_binding::context::subscribe::{Facility, InterestMaskSet, Operation};
use libpulse_binding::context::Context;
use libpulse_binding::mainloop::threaded::Mainloop;
use libpulse_binding::stream::Stream;

use crossbeam_channel::{Receiver, bounded};

/// PulseAudio 事件，从 mainloop 回调线程发送给消费线程。
#[derive(Clone, Debug)]
pub enum PulseEvent {
    CardAdded { index: u32 },
    CardRemoved { index: u32 },
    CardChanged { index: u32 },
    SinkAdded { index: u32 },
    SinkRemoved { index: u32 },
    SinkChanged { index: u32 },
    SourceAdded { index: u32 },
    SourceRemoved { index: u32 },
    SourceChanged { index: u32 },
    SinkInputAdded { index: u32 },
    SinkInputRemoved { index: u32 },
    SinkInputChanged { index: u32 },
    #[allow(dead_code)]
    DefaultSinkChanged { name: String },
    #[allow(dead_code)]
    DefaultSourceChanged { name: String },
    Server,
}

/// libpulse 内部状态。
///
/// `Mainloop` 内部使用 `Rc`，类型系统标记为 `!Send`。
/// 但 threaded mainloop 设计上就是跨线程使用的（通过内部 mutex 同步），
/// `Context` 也已标记为 `Send + Sync`。
/// 因此这里用 `unsafe` 为整体实现 `Send`，使 `PulseManager` 可放入 `Arc`。
struct PulseInner {
    ml: Mainloop,
    ctx: Context,
}

// SAFETY: threaded::Mainloop 内部通过自己的 mutex 实现线程安全，
// lock/unlock 是唯一访问入口。Context 已是 Send + Sync。
// 实际跨线程访问都经过 Mutex 保护和 mainloop lock/unlock 同步。
unsafe impl Send for PulseInner {}

/// PulseAudio 连接管理器。
///
/// 持有 libpulse 的 threaded mainloop 和 context。
/// mainloop 在独立线程上自动 poll，context 回调在 mainloop 线程上触发。
/// 回调里只把事件塞进 channel，由 EventManager 消费处理。
pub struct PulseManager {
    inner: Mutex<PulseInner>,
}

impl PulseManager {
    /// 创建 PulseManager，连接 daemon，注册事件订阅。
    ///
    /// 返回 `(PulseManager, Receiver<PulseEvent>)`，消费线程从 receiver
    /// 读取设备增删改事件。
    pub fn new() -> Result<(Self, Receiver<PulseEvent>), String> {
        let mut ml = Mainloop::new().ok_or("failed to create mainloop")?;
        let mut ctx = Context::new(&ml, "dde-audio").ok_or("failed to create context")?;

        // 创建事件 channel
        let (event_tx, event_rx) = bounded::<PulseEvent>(256);

        // ===== 1. 注册 state 回调，通过 Condvar 通知等待线程 =====
        let notify = Arc::new((Mutex::new(()), Condvar::new()));
        let notify_clone = notify.clone();
        ctx.set_state_callback(Some(Box::new(move || {
            let (lock, cvar) = &*notify_clone;
            let _guard = lock.lock();
            cvar.notify_all();
        })));

        ctx.connect(None, FlagSet::NOAUTOSPAWN, None)
            .map_err(|e| format!("failed to connect: {e:?}"))?;

        ml.start().map_err(|e| format!("failed to start mainloop: {e:?}"))?;

        // 等待 context ready
        let (lock, cvar) = &*notify;
        let mut guard = lock.lock().map_err(|e| format!("mutex poisoned: {e}"))?;
        loop {
            ml.lock();
            let state = ctx.get_state();
            ml.unlock();

            match state {
                State::Ready => break,
                State::Failed => return Err("context connection failed".into()),
                State::Terminated => return Err("context connection terminated".into()),
                _ => {
                    let (guard2, _timeout) = cvar
                        .wait_timeout(guard, Duration::from_secs(5))
                        .map_err(|e| format!("mutex poisoned: {e}"))?;
                    guard = guard2;
                }
            }
        }

        // ===== 2. 注册 subscribe 回调 =====
        // 回调在 mainloop 线程上触发，根据 facility+operation 映射到 PulseEvent，
        // 塞进 channel。不做业务逻辑，不阻塞 mainloop。
        let event_tx_clone = event_tx.clone();
        ctx.set_subscribe_callback(Some(Box::new(move |facility, operation, index| {
            let event = match (facility, operation) {
                (Some(Facility::Card), Some(Operation::New)) => PulseEvent::CardAdded { index },
                (Some(Facility::Card), Some(Operation::Removed)) => PulseEvent::CardRemoved { index },
                (Some(Facility::Card), Some(Operation::Changed)) => PulseEvent::CardChanged { index },
                (Some(Facility::Sink), Some(Operation::New)) => PulseEvent::SinkAdded { index },
                (Some(Facility::Sink), Some(Operation::Removed)) => PulseEvent::SinkRemoved { index },
                (Some(Facility::Sink), Some(Operation::Changed)) => PulseEvent::SinkChanged { index },
                (Some(Facility::Source), Some(Operation::New)) => PulseEvent::SourceAdded { index },
                (Some(Facility::Source), Some(Operation::Removed)) => PulseEvent::SourceRemoved { index },
                (Some(Facility::Source), Some(Operation::Changed)) => PulseEvent::SourceChanged { index },
                (Some(Facility::SinkInput), Some(Operation::New)) => PulseEvent::SinkInputAdded { index },
                (Some(Facility::SinkInput), Some(Operation::Removed)) => PulseEvent::SinkInputRemoved { index },
                (Some(Facility::SinkInput), Some(Operation::Changed)) => PulseEvent::SinkInputChanged { index },
                (Some(Facility::Server), Some(Operation::Changed)) => PulseEvent::Server,
                _ => return,
            };
            // send 失败说明消费端已关闭，忽略即可
            let _ = event_tx_clone.send(event);
        })));

        // ===== 3. 订阅事件 =====
        // subscribe 是异步操作，但不需要等回调确认——订阅成功后事件自然到达。
        ml.lock();
        ctx.subscribe(
            InterestMaskSet::SINK
                | InterestMaskSet::SOURCE
                | InterestMaskSet::SINK_INPUT
                | InterestMaskSet::CARD
                | InterestMaskSet::SERVER,
            |_| {},
        );
        ml.unlock();

        Ok((
            Self {
                inner: Mutex::new(PulseInner { ml, ctx }),
            },
            event_rx,
        ))
    }

    /// 提交操作到 context，同步等回调结果。
    ///
    /// 各子模块通过此方法复用 lock/submit/unlock/wait 模式：
    /// 1. lock mainloop（通过 Mutex）
    /// 2. 闭包内通过 context 提交异步请求，用 `tx` 发送回调结果
    /// 3. unlock mainloop，让 mainloop 线程处理请求
    /// 4. 阻塞等 `rx` 回调结果，返回给调用者
    ///
    /// 调用者（DBus 线程）阻塞等待，对 D-Bus 客户端来说同步返回。
    pub fn execute<R: Send + 'static, F>(&self, op: F) -> Result<R, String>
    where
        F: FnOnce(&Context, crossbeam_channel::Sender<R>) -> bool,
    {
        let (tx, rx) = crossbeam_channel::bounded(1);

        let mut inner = self.inner.lock().map_err(|e| format!("mutex poisoned: {e}"))?;
        inner.ml.lock();
        let sent = op(&inner.ctx, tx);
        inner.ml.unlock();

        if !sent {
            return Err("pulse request rejected".into());
        }

        drop(inner);

        rx.recv().map_err(|e| format!("callback dropped: {e}"))
    }

    /// 同 `execute`，但闭包可拿到 `&mut Context`（部分 API 需要可变借用，
    /// 如 `set_default_sink`）。execute 内持有 mainloop 锁，同刻唯一访问。
    pub fn execute_mut<R: Send + 'static, F>(&self, op: F) -> Result<R, String>
    where
        F: FnOnce(&mut Context, crossbeam_channel::Sender<R>) -> bool,
    {
        let (tx, rx) = crossbeam_channel::bounded(1);

        let mut inner = self.inner.lock().map_err(|e| format!("mutex poisoned: {e}"))?;
        inner.ml.lock();
        let sent = op(&mut inner.ctx, tx);
        inner.ml.unlock();

        if !sent {
            return Err("pulse request rejected".into());
        }

        drop(inner);

        rx.recv().map_err(|e| format!("callback dropped: {e}"))
    }

    /// 提交列表查询操作，收集多次回调结果直到 End。
    ///
    /// 与 `execute` 不同：列表查询回调会触发多次（每个 item 一次），
    /// 用 `ListResult::End` 标记结束。闭包内收到 Item 时累积，End 时 send。
#[allow(dead_code)]
    pub fn execute_list<T, R>(&self, op: R) -> Result<Vec<T>, String>
    where
        T: Send + 'static,
        R: FnOnce(&Context, crossbeam_channel::Sender<Vec<T>>) -> bool,
    {
        self.execute(|ctx, tx| op(ctx, tx))
    }

    /// 查询默认 sink 和 source 名称。
    ///
    /// 内部走 `execute` 模式，同步等回调返回。
    /// `ServerInfo` 含引用不能跨线程传递，这里只提取需要的字段。
    #[allow(dead_code)]
    pub fn default_sink_source(&self) -> Result<(String, String), String> {
        self.execute(|ctx, tx| {
            ctx.introspect().get_server_info(move |info| {
                let sink = info.default_sink_name.as_deref().unwrap_or("").to_owned();
                let source = info.default_source_name.as_deref().unwrap_or("").to_owned();
                let _ = tx.send((sink, source));
            });
            true
        })
    }

    /// 加载模块，返回模块索引。
    pub fn load_module(&self, name: &str, argument: &str) -> Result<u32, String> {
        let name = name.to_owned();
        let argument = argument.to_owned();
        self.execute(|ctx, tx| {
            let mut intro = ctx.introspect();
            intro.load_module(&name, &argument, move |index| {
                let _ = tx.send(index);
            });
            true
        })
    }

    /// 设置默认 sink。
    ///
    /// 供单声道切换等场景使用：把 mono-sink 设为默认输出。
    pub fn set_default_sink(&self, name: &str) -> Result<(), String> {
        let name = name.to_owned();
        self.execute_mut(|ctx, tx| {
            ctx.set_default_sink(&name, move |ok| {
                let _ = tx.send(ok);
            });
            true
        })?;
        Ok(())
    }

    /// 卸载模块。
    #[allow(dead_code)]
    pub fn unload_module(&self, module_index: u32) -> Result<(), String> {
        let result: bool = self.execute(|ctx, tx| {
            let mut intro = ctx.introspect();
            intro.unload_module(module_index, move |ok| {
                let _ = tx.send(ok);
            });
            true
        })?;
        if result {
            Ok(())
        } else {
            Err(format!("unload module {module_index} failed"))
        }
    }

    /// 查询模块是否存在。
    #[allow(dead_code)]
    pub fn module_exists(&self, name: &str) -> Result<bool, String> {
        use libpulse_binding::callbacks::ListResult;

        let name = name.to_owned();
        self.execute(|ctx, tx| {
            let intro = ctx.introspect();
            let mut found = false;
            intro.get_module_info_list(move |res| {
                match res {
                    ListResult::Item(info) => {
                        if info.name.as_deref() == Some(name.as_str()) {
                            found = true;
                        }
                    }
                    ListResult::End | ListResult::Error => {
                        let _ = tx.send(found);
                    }
                }
            });
            true
        })
    }

    /// 创建 source 音量计量器（record stream）。
    ///
    /// `Stream::new` 需要 `&mut Context`，且创建必须在 mainloop lock 下完成。
    /// 与 `execute` 不同：meter stream 创建后由 `SourceMeter` 长期持有，
    /// 不通过 channel 等回调。
    pub fn create_source_meter(
        self: &Arc<Self>,
        source_index: u32,
    ) -> Result<Arc<meter::SourceMeter>, String> {
        let mut inner = self.inner.lock().map_err(|e| format!("mutex poisoned: {e}"))?;
        inner.ml.lock();
        let result = meter::SourceMeter::create(
            &mut inner.ctx,
            source_index,
            Arc::clone(self),
        );
        inner.ml.unlock();
        result
    }

    /// 在 PulseAudio mainloop 锁内注销 source meter 的 read callback。
    ///
    /// `Stream::drop` 会释放 libpulse-binding 保存在请求回调中的闭包对象；
    /// 如果 PulseAudio mainloop 仍持有该闭包指针，就会在后续 read 事件中
    /// 触发 use-after-free。因此 SourceMeter 释放前必须先在这里清空回调。
    pub(crate) fn destroy_source_meter(&self, stream: Arc<Mutex<Stream>>) {
        // 即使互斥锁被 panic 毒化也必须继续清理：否则 read callback 会保持
        // 已释放的闭包指针，下一次 mainloop 派发时再次触发 UAF。
        let mut inner = match self.inner.lock() {
            Ok(inner) => inner,
            Err(poisoned) => poisoned.into_inner(),
        };
        inner.ml.lock();
        {
            let mut stream = match stream.lock() {
                Ok(stream) => stream,
                Err(poisoned) => poisoned.into_inner(),
            };
            stream.set_read_callback(None);
            let _ = stream.disconnect();
        }
        // 最后一个 Arc 必须在 mainloop 锁内释放：Stream::drop 还会再次
        // disconnect/unref，不能与 mainloop 线程并发。
        drop(stream);
        inner.ml.unlock();
    }
}

impl Drop for PulseManager {
    fn drop(&mut self) {
        if let Ok(mut inner) = self.inner.lock() {
            inner.ml.lock();
            inner.ctx.disconnect();
            inner.ml.unlock();
            let _ = inner.ml.stop();
        }
    }
}
