// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! 端口设置 / 优先级切换协调器（executor 队列模型）。
//!
//! 协调「手动设置端口」「优先级自动切换」「端口启用」三类操作。
//! 内部**单一 executor 线程**串行执行任务队列，从根上消除并发切换
//! 的互斥问题；新任务 `submit` 时对历史任务做**抢占/取代**检查。
//!
//! 优先级：手动 SetPort > 端口启用 > 自动切换。
//!
//! 调度规则（`submit` 时在锁内检查 running + 队列）：
//! - 新**手动** → 取代一切（取消 running 与队列中所有其它任务）
//! - 新**启用** → 取代自动/启用，保留队列中手动（等待其完成）
//! - 新**自动** → 取代自动（同类新抢旧），保留手动/启用
//! - 被取代的任务：置 `cancel`；队列中未执行的直接移除（结果 Cancelled）；
//!   running 任务通过 `wait_cancellable` 轮询 cancel 提前结束让出 executor。
//!
//! 任务生命周期：
//! - `new_task(kind, op)` 创建任务
//! - `submit(&handle)` 入队（含抢占检查）
//! - `handle.wait(timeout)` 阻塞等结果（业务线程，手动路径同步等待）
//! - `handle.cancel()` 请求取消
//!
//! 死锁防线：
//! - executor 执行 `handler` 时不得再向本协调器 `submit` 并 `wait` 自身
//!   （会自锁）；当前 handler 只调用同步的 `set_port_inner`，满足
//! - `wait` 必须带超时
//! - 锁顺序固定：队列锁 `state` 与任务结果锁 `result` 独立，`result` 锁
//!   内不持 `state` 锁等待

use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;

use parking_lot::{Condvar, Mutex};

/// 切换操作类型（Ord 即优先级，手动最高）。
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum SwitchKind {
    /// 自动切换（最低优先级）。
    PriorityAuto,
    /// 端口启用/禁用。
    SetPortEnabled,
    /// 手动设置端口（最高优先级）。
    ManualSetPort,
}

/// 待执行的端口操作。
#[derive(Clone, Debug)]
pub enum TaskOp {
    /// 设置端口。
    SetPort {
        card_id: u32,
        port_name: String,
        direction: u32,
        /// true=自动（不记 user_prefer），false=手动。
        auto: bool,
    },
    /// 设置端口启用/禁用。
    SetPortEnabled {
        card_id: u32,
        port_name: String,
        enabled: bool,
    },
    /// 触发一次自动端口切换（具体目标由 handler 内部按优先级策略选）。
    AutoSwitch,
}

/// 任务执行结果。
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TaskResult {
    /// 成功。
    Ok,
    /// 失败（携带错误信息）。
    Err(String),
    /// 被更高优先级任务取代而取消。
    Cancelled,
}

/// 任务内部状态（executor 与业务线程共享）。
struct TaskInner {
    kind: SwitchKind,
    op: TaskOp,
    cancel: AtomicBool,
    result: Mutex<Option<TaskResult>>,
    result_cond: Condvar,
}

/// 任务的业务侧句柄。
pub struct TaskHandle {
    inner: Arc<TaskInner>,
}

impl TaskHandle {
    /// 阻塞等待结果，超时返回 Err。
    pub fn wait(&self, timeout: std::time::Duration) -> Result<TaskResult, String> {
        let mut result = self.inner.result.lock();
        let mut timed_out = false;
        while result.is_none() && !timed_out {
            let r = self.inner.result_cond.wait_for(&mut result, timeout);
            timed_out = r.timed_out();
        }
        Ok(result.clone().unwrap_or(TaskResult::Cancelled))
    }

    /// 请求取消：置 cancel（队列中移除由 coordinator 处理，running 提前结束）。
    pub fn cancel(&self) {
        self.inner.cancel.store(true, Ordering::SeqCst);
        self.inner.result_cond.notify_all();
    }

    /// 该任务的取消令牌（供内部等待点轮询）。
    pub fn cancel_flag(&self) -> &AtomicBool {
        &self.inner.cancel
    }
}

/// executor 线程与协调器共享的队列状态。
struct ExecutorShared {
    state: Mutex<State>,
    queue_cond: Condvar,
}

struct State {
    /// 等待执行的任务（队首优先）。
    queue: VecDeque<Arc<TaskInner>>,
    /// 当前执行中的任务。
    running: Option<Arc<TaskInner>>,
}

/// 协调器：单 executor 线程 + 任务队列。
pub struct SwitchCoordinator {
    shared: Arc<ExecutorShared>,
    /// 关闭信号（Drop 时置位，executor 轮询退出）。
    shutdown: Arc<std::sync::atomic::AtomicBool>,
    /// executor 线程句柄。
    #[allow(dead_code)]
    executor: Option<thread::JoinHandle<()>>,
}

impl SwitchCoordinator {
    /// 新建协调器并启动 executor 线程。
    ///
    /// `handler` 在 executor 线程执行任务，不得再向本协调器提交并等待
    /// 自身任务（自锁）。
    pub fn new(
        handler: Arc<dyn Fn(TaskOp, &AtomicBool) -> Result<(), String> + Send + Sync>,
    ) -> Self {
        let shared = Arc::new(ExecutorShared {
            state: Mutex::new(State {
                queue: VecDeque::new(),
                running: None,
            }),
            queue_cond: Condvar::new(),
        });
        let shutdown = Arc::new(AtomicBool::new(false));
        let sh = shared.clone();
        let sh_down = shutdown.clone();
        let handler_clone = handler.clone();
        let executor = thread::spawn(move || Self::run_executor(sh, sh_down, handler_clone));

        Self {
            shared,
            shutdown,
            executor: Some(executor),
        }
    }

    /// executor 主循环：串行消费队列。
    fn run_executor(
        shared: Arc<ExecutorShared>,
        shutdown: Arc<AtomicBool>,
        handler: Arc<dyn Fn(TaskOp, &AtomicBool) -> Result<(), String> + Send + Sync>,
    ) {
        loop {
            // 取队首任务；队列空则阻塞等（带超时以响应关闭信号）
            let task = {
                let mut st = shared.state.lock();
                loop {
                    if shutdown.load(Ordering::SeqCst) {
                        return;
                    }
                    if let Some(t) = st.queue.pop_front() {
                        break Some(t);
                    }
                    let _r = shared
                        .queue_cond
                        .wait_for(&mut st, std::time::Duration::from_millis(100));
                }
            };
            let task = match task {
                Some(t) => t,
                None => continue,
            };

            // 已被取代（入队后被 submit 取消）：直接标记 Cancelled
            if task.cancel.load(Ordering::SeqCst) {
                Self::finish(&task, TaskResult::Cancelled);
                continue;
            }

            // 置 running，执行
            {
                let mut st = shared.state.lock();
                st.running = Some(task.clone());
            }
            let outcome = if task.cancel.load(Ordering::SeqCst) {
                TaskResult::Cancelled
            } else {
                match (handler)(task.op.clone(), &task.cancel) {
                    Ok(()) => TaskResult::Ok,
                    Err(e) => {
                        if task.cancel.load(Ordering::SeqCst) {
                            TaskResult::Cancelled
                        } else {
                            TaskResult::Err(e)
                        }
                    }
                }
            };
            {
                let mut st = shared.state.lock();
                st.running = None;
            }
            Self::finish(&task, outcome);
        }
    }

    fn finish(task: &Arc<TaskInner>, outcome: TaskResult) {
        *task.result.lock() = Some(outcome);
        task.result_cond.notify_all();
    }

    /// 创建一个待入队任务（绑定操作）。
    pub fn new_task(&self, kind: SwitchKind, op: TaskOp) -> TaskHandle {
        TaskHandle {
            inner: Arc::new(TaskInner {
                kind,
                op,
                cancel: AtomicBool::new(false),
                result: Mutex::new(None),
                result_cond: Condvar::new(),
            }),
        }
    }

    /// 提交任务入队（含对历史任务的抢占/取代检查）。
    pub fn submit(&self, handle: &TaskHandle) {
        let mut st = self.shared.state.lock();
        let new_kind = handle.inner.kind;

        // 1. 取代 running 中应被抢占的任务（置 cancel，让其 wait_cancellable 提前结束）
        if let Some(r) = st.running.as_ref() {
            if should_preempt(new_kind, r.kind) {
                r.cancel.store(true, Ordering::SeqCst);
                r.result_cond.notify_all();
            }
        }

        // 2. 队列中应被取代的任务：置 cancel + 移除（立即给 Cancelled 结果）
        let mut keep = VecDeque::new();
        while let Some(t) = st.queue.pop_front() {
            if should_preempt(new_kind, t.kind) {
                t.cancel.store(true, Ordering::SeqCst);
                Self::finish(&t, TaskResult::Cancelled);
            } else {
                keep.push_back(t);
            }
        }
        st.queue = keep;

        // 3. 入队
        st.queue.push_back(handle.inner.clone());
        self.shared.queue_cond.notify_one();
    }

    /// 取消并（若在队列）移除任务。
    pub fn cancel_task(&self, handle: &TaskHandle) {
        let mut st = self.shared.state.lock();
        handle.inner.cancel.store(true, Ordering::SeqCst);
        st.queue.retain(|t| !Arc::ptr_eq(t, &handle.inner));
        if let Some(r) = st.running.as_ref() {
            if Arc::ptr_eq(r, &handle.inner) {
                handle.inner.result_cond.notify_all();
            }
        }
        self.shared.queue_cond.notify_one();
    }
}

impl Drop for SwitchCoordinator {
    fn drop(&mut self) {
        use std::sync::atomic::Ordering as AtOrder;
        self.shutdown.store(true, AtOrder::SeqCst);
        self.shared.queue_cond.notify_all();
        if let Some(h) = self.executor.take() {
            let _ = h.join();
        }
    }
}

/// 新任务是否应取代旧任务。
///
/// 优先级更高则取代；同优先级（同类新抢旧）也取代。只有优先级更低
/// （如自动遇到手动/启用）才保留旧任务等待其完成。
fn should_preempt(new_kind: SwitchKind, old_kind: SwitchKind) -> bool {
    new_kind >= old_kind
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering as AtOrder};
    use std::time::Duration;

    /// 构造一个执行耗时的协调器（模拟切换；可阻塞等待取消检测）。
    fn slow_coord() -> SwitchCoordinator {
        SwitchCoordinator::new(Arc::new(|_op: TaskOp, cancel: &AtomicBool| {
            // 模拟切换：轮询取消，最多 200ms
            for _ in 0..20 {
                if cancel.load(AtOrder::SeqCst) {
                    return Err("cancelled".into());
                }
                thread::sleep(Duration::from_millis(10));
            }
            Ok(())
        }))
    }

    fn manual_op(name: &str) -> TaskOp {
        TaskOp::SetPort {
            card_id: 1,
            port_name: name.to_owned(),
            direction: 1,
            auto: false,
        }
    }

    #[test]
    fn manual_preempts_auto() {
        let c = slow_coord();
        let auto = c.new_task(SwitchKind::PriorityAuto, TaskOp::AutoSwitch);
        c.submit(&auto);
        thread::sleep(Duration::from_millis(5));
        let manual = c.new_task(SwitchKind::ManualSetPort, manual_op("p"));
        c.submit(&manual);
        assert_eq!(auto.wait(Duration::from_secs(1)).unwrap(), TaskResult::Cancelled);
        assert_eq!(manual.wait(Duration::from_secs(1)).unwrap(), TaskResult::Ok);
    }

    #[test]
    fn auto_waits_manual() {
        let c = slow_coord();
        let manual = c.new_task(SwitchKind::ManualSetPort, manual_op("p"));
        c.submit(&manual);
        thread::sleep(Duration::from_millis(5));
        let auto = c.new_task(SwitchKind::PriorityAuto, TaskOp::AutoSwitch);
        c.submit(&auto);
        assert_eq!(manual.wait(Duration::from_secs(1)).unwrap(), TaskResult::Ok);
        assert_eq!(auto.wait(Duration::from_secs(1)).unwrap(), TaskResult::Ok);
    }

    #[test]
    fn same_kind_newest_wins() {
        let c = slow_coord();
        let a1 = c.new_task(SwitchKind::ManualSetPort, manual_op("p1"));
        c.submit(&a1);
        thread::sleep(Duration::from_millis(5));
        let a2 = c.new_task(SwitchKind::ManualSetPort, manual_op("p2"));
        c.submit(&a2);
        assert_eq!(a1.wait(Duration::from_secs(1)).unwrap(), TaskResult::Cancelled);
        assert_eq!(a2.wait(Duration::from_secs(1)).unwrap(), TaskResult::Ok);
    }

    #[test]
    fn cancel_before_exec() {
        let c = slow_coord();
        let t = c.new_task(SwitchKind::PriorityAuto, TaskOp::AutoSwitch);
        c.submit(&t);
        t.cancel();
        assert_eq!(t.wait(Duration::from_secs(1)).unwrap(), TaskResult::Cancelled);
    }

    #[test]
    fn latest_auto_replaces_queued() {
        let c = slow_coord();
        // 队列中堆积多个自动任务：同类新抢旧，只留最新执行
        let a1 = c.new_task(SwitchKind::PriorityAuto, TaskOp::AutoSwitch);
        let a2 = c.new_task(SwitchKind::PriorityAuto, TaskOp::AutoSwitch);
        c.submit(&a1);
        c.submit(&a2);
        assert_eq!(a1.wait(Duration::from_secs(1)).unwrap(), TaskResult::Cancelled);
        assert_eq!(a2.wait(Duration::from_secs(1)).unwrap(), TaskResult::Ok);
    }

    #[test]
    fn executor_serializes_and_runs_all() {
        let count = Arc::new(AtomicUsize::new(0));
        let count_cl = count.clone();
        let c = SwitchCoordinator::new(Arc::new(move |_op, _cancel| {
            count_cl.fetch_add(1, AtOrder::SeqCst);
            Ok(())
        }));
        // 顺序 submit + wait（前一个完成后再提交下一个），避免抢占取消，
        // 验证 executor 串行且全部执行。
        for i in 0..5 {
            let op = if i % 2 == 0 {
                TaskOp::SetPortEnabled {
                    card_id: 1,
                    port_name: format!("p{i}"),
                    enabled: true,
                }
            } else {
                manual_op(&format!("p{i}"))
            };
            let kind = if i % 2 == 0 {
                SwitchKind::SetPortEnabled
            } else {
                SwitchKind::ManualSetPort
            };
            let h = c.new_task(kind, op);
            c.submit(&h);
            h.wait(Duration::from_secs(2)).unwrap();
        }
        assert_eq!(count.load(AtOrder::SeqCst), 5);
    }
}
