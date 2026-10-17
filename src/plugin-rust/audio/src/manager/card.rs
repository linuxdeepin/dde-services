// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! 声卡（Card）业务逻辑。
//!
//! 处理 Card 的 new/update/delete，更新 DeviceManager。
//! Card 无独立 D-Bus 对象（信息通过 Audio 主接口的 cards 属性暴露），
//! 因此不需要注册/注销，只需更新状态。

use std::sync::Arc;

use parking_lot::RwLock;

use crate::backend::pulse::card as pulse_card;
use crate::backend::pulse::PulseManager;

use super::device_manager::DeviceManager;

/// 声卡端口信息。
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize, zbus::zvariant::Type)]
pub struct PortInfo {
    pub name: String,
    pub enabled: bool,
    pub bluetooth: bool,
    pub description: String,
    pub direction: u32,
    /// 该端口关联的 profile（含优先级/可用性，用于选最优）。
    pub profiles: Vec<Profile>,
    /// 端口权重（越高越适合作为默认）。
    pub priority: u32,
}

impl PortInfo {
    /// 选择该端口最合适的 profile。
    ///
    /// 对齐 Go 版 `ProfileInfos2.SelectProfile`：
    /// 可用（available）的 profile 中选 priority 最高者；无可用则取第一个。
    pub fn select_profile(&self) -> Option<&str> {
        let best = self
            .profiles
            .iter()
            .filter(|p| p.available)
            .max_by_key(|p| p.priority);
        match best {
            Some(p) => Some(p.name.as_str()),
            None => self.profiles.first().map(|p| p.name.as_str()),
        }
    }
}
/// 声卡支持的 profile。
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize, zbus::zvariant::Type)]
pub struct Profile {
    pub name: String,
    pub description: String,
    /// 越高越适合作为默认 profile。
    pub priority: u32,
    /// 是否可用（unavailable 的 profile 无意义）。
    pub available: bool,
}

impl From<crate::backend::pulse::card::BackendCardProfile> for Profile {
    fn from(p: crate::backend::pulse::card::BackendCardProfile) -> Self {
        Self {
            name: p.name,
            description: p.description,
            priority: p.priority,
            available: p.available,
        }
    }
}
/// 声卡（Card）状态。
///
/// `status`/`change` 是运行时状态。`change` 含 Mutex 不可序列化，
/// 用 `#[serde(skip)]` 跳过（反序列化时为 None）。
#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct Card {
    pub index: u32,
    pub name: String,
    pub active_profile: String,
    pub ports: Vec<PortInfo>,
    /// 该声卡支持的所有 profile。
    pub profiles: Vec<Profile>,
    /// 生命周期状态。
    pub status: CardStatus,
    /// 进行中的状态变更（同步手柄）。
    #[serde(skip)]
    pub change: Option<Arc<StatusChange>>,
}

impl From<crate::backend::pulse::card::BackendCard> for Card {
    fn from(b: crate::backend::pulse::card::BackendCard) -> Self {
        Self {
            index: b.index,
            name: b.name,
            active_profile: b.active_profile,
            ports: b.ports
                .into_iter()
                .map(|p| PortInfo {
                    name: p.name,
                    enabled: p.available,
                    bluetooth: false,
                    description: p.description,
                    direction: p.direction,
                    profiles: p.profiles.into_iter().map(Profile::from).collect(),
                    priority: p.priority,
                })
                .collect(),
            profiles: b.profiles.into_iter().map(Profile::from).collect(),
            status: CardStatus::Ready,
            change: None,
        }
    }
}

/// 方向掩码：输出。
pub const DIRECTION_SINK: u32 = 1 << 0;
/// 方向掩码：输入。
pub const DIRECTION_SOURCE: u32 = 1 << 1;
/// Card 生命周期状态。
#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum CardStatus {
    /// 正常可用。
    Ready,
    /// profile 切换中。
    Pending,
    /// 删除中。
    Removing,
}

/// 一次状态变更（如 profile 切换）的最终结果。
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ChangeResult {
    /// 完成（设备重建齐全）。
    Complete,
    /// 声卡被移除，操作终止。
    Removed,
    /// 操作被取消（如被手动操作打断）。
    Cancelled,
    /// 失败（当前无生产者，为四态协议预留）。
    #[allow(dead_code)]
    Failed(String),
}

/// 一次状态变更的同步手柄。
///
/// 变更发起方（如 set_profile）阻塞等待，event_loop 检测到
/// 变更完成（设备重建齐全）后通知。
///
/// `required_directions` 记录变更前该声卡存在的设备方向（bit0=输出，bit1=输入），
/// 设备重建后所有方向齐全才认为完成。
///
/// 结果是广播的（`notify_all`），多个等待者可同时收到完成/移除/失败。
pub struct StatusChange {
    result: std::sync::Mutex<Option<ChangeResult>>,
    cond: std::sync::Condvar,
    /// 需要重建的方向掩码：bit0=输出(sink)，bit1=输入(source)。
    required_directions: u32,
}

impl std::fmt::Debug for StatusChange {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("StatusChange")
            .field("required_directions", &self.required_directions)
            .field(
                "result",
                &self.result.lock().map(|r| r.as_ref().cloned()).unwrap_or(None),
            )
            .finish()
    }
}

impl StatusChange {
    /// 创建等待项，指定需要重建的方向。
    pub fn new(required_directions: u32) -> Arc<Self> {
        Arc::new(Self {
            result: std::sync::Mutex::new(None),
            cond: std::sync::Condvar::new(),
            required_directions,
        })
    }

    /// 需要重建的方向掩码。
    pub fn required_directions(&self) -> u32 {
        self.required_directions
    }

    /// 阻塞等待结果，超时返回错误。
    pub fn wait(&self, timeout: std::time::Duration) -> Result<ChangeResult, String> {
        let mut result = self
            .result
            .lock()
            .map_err(|e| format!("mutex poisoned: {e}"))?;
        while result.is_none() {
            let (guard, timeout_result) = self
                .cond
                .wait_timeout(result, timeout)
                .map_err(|e| format!("mutex poisoned: {e}"))?;
            result = guard;
            if timeout_result.timed_out() {
                return Err("profile switch timed out".into());
            }
        }
        Ok(result.clone().unwrap())
    }

    /// 阻塞等待结果；期间轮询外部 `cancel` 标志，置位则返回 `Cancelled`。
    ///
    /// 供 coordinator 抢占终止使用：新操作到来置 cancel，本等待点
    /// 在 ~100ms 内醒来返回 Cancelled，让出锁。
    pub fn wait_cancellable(
        &self,
        cancel: &std::sync::atomic::AtomicBool,
        timeout: std::time::Duration,
    ) -> Result<ChangeResult, String> {
        use std::sync::atomic::Ordering;
        use std::time::{Duration as StdDur, Instant};

        let poll = StdDur::from_millis(100);
        let deadline = Instant::now() + timeout;
        let mut result = self
            .result
            .lock()
            .map_err(|e| format!("mutex poisoned: {e}"))?;
        loop {
            if let Some(r) = result.as_ref() {
                return Ok(r.clone());
            }
            if cancel.load(Ordering::SeqCst) {
                return Ok(ChangeResult::Cancelled);
            }
            let now = Instant::now();
            if now >= deadline {
                return Err("profile switch timed out".into());
            }
            let wait = poll.min(deadline - now);
            let (g, _) = self
                .cond
                .wait_timeout(result, wait)
                .map_err(|e| format!("mutex poisoned: {e}"))?;
            result = g;
        }
    }

    /// 广播完成。
    pub fn signal_complete(&self) {
        self.signal(ChangeResult::Complete);
    }

    /// 广播声卡被移除。
    pub fn signal_removed(&self) {
        self.signal(ChangeResult::Removed);
    }

    /// 广播操作被取消（如被手动端口设置打断）。
    pub fn signal_cancelled(&self) {
        self.signal(ChangeResult::Cancelled);
    }

    /// 广播失败。
    #[allow(dead_code)]
    pub fn signal_failed(&self, error: String) {
        self.signal(ChangeResult::Failed(error));
    }

    fn signal(&self, result: ChangeResult) {
        if let Ok(mut guard) = self.result.lock() {
            if guard.is_none() {
                *guard = Some(result);
                self.cond.notify_all();
            }
        }
    }
}


/// Card 新增：查询状态写入 DeviceManager。
pub fn new(
    pulse: &Arc<PulseManager>,
    device_manager: &Arc<RwLock<DeviceManager>>,
    index: u32,
) -> Result<(), String> {
    let state: Card = pulse_card::query_info(pulse, index)?.into();
    device_manager.write().add_card(index, state);
    eprintln!("[dde-audio] card new: {index}");
    Ok(())
}

/// Card 更新：查询最新状态写入 DeviceManager。
///
/// 若声卡正处于 profile 切换（Pending）中，保留其 `status`/`change`，
/// 否则 CardChanged 事件会把 Pending 重置为 Ready、丢弃等待句柄，
/// 导致后续设备创建完成判定（`on_device_created`）漏判而超时。
pub fn update(
    pulse: &Arc<PulseManager>,
    device_manager: &Arc<RwLock<DeviceManager>>,
    index: u32,
) -> Result<(), String> {
    let mut state: Card = pulse_card::query_info(pulse, index)?.into();

    // 正在切换中：保留 Pending 与 change 句柄
    if let Some(existing) = device_manager.read().cards.get(&index) {
        if existing.status == CardStatus::Pending {
            state.status = CardStatus::Pending;
            state.change = existing.change.clone();
        }
    }

    device_manager.write().update_card(index, state);
    eprintln!("[dde-audio] card update: {index}");
    Ok(())
}

/// Card 删除：从 DeviceManager 移除。
///
/// 若有进行中的 profile 切换操作，广播 Removed 让等待线程结束。
pub fn delete(device_manager: &Arc<RwLock<DeviceManager>>, index: u32) {
    let removed = device_manager.write().remove_card(index);
    if let Some(card) = removed {
        if let Some(op) = card.change {
            eprintln!("[dde-audio] card removed during profile switch: card {index}");
            op.signal_removed();
        }
    }
    // TODO: 可能触发 default sink/source 重选
    eprintln!("[dde-audio] card delete: {index}");
}

/// 设备创建后检查 profile 切换是否完成。
///
/// 若设备所属声卡处于 Pending 状态，检查所需方向（切换前的 sink/source）
/// 是否都已重建，齐全则回 Ready 并通知等待线程。
pub fn on_device_created(
    device_manager: &Arc<RwLock<DeviceManager>>,
    device_index: u32,
    is_sink: bool,
) {
    let card_id = {
        let dm = device_manager.read();
        if is_sink {
            dm.sinks.get(&device_index).map(|s| s.card)
        } else {
            dm.sources.get(&device_index).map(|s| s.card)
        }
    };

    let card_id = match card_id {
        Some(cid) => cid,
        None => return,
    };

    // 非 Pending 状态则忽略
    let all_ready = {
        let dm = device_manager.read();
        let card = match dm.cards.get(&card_id) {
            Some(c) => c,
            None => return,
        };
        if card.status != CardStatus::Pending {
            return;
        }
        let op = match &card.change {
            Some(op) => op,
            None => return,
        };
        let required = op.required_directions();
        let sink_ok = required & DIRECTION_SINK == 0
            || dm.sinks.values().any(|s| s.card == card_id);
        let source_ok = required & DIRECTION_SOURCE == 0
            || dm.sources.values().any(|s| s.card == card_id);
        sink_ok && source_ok
    };

    if all_ready {
        eprintln!("[dde-audio] complete pending profile: card {card_id}");
        let op = device_manager.write().cards.get_mut(&card_id).and_then(|c| c.change.take());
        if let Some(op) = op {
            op.signal_complete();
        }
        if let Some(card) = device_manager.write().cards.get_mut(&card_id) {
            card.status = CardStatus::Ready;
        }
    }
}

/// 检查声卡仍存在（未被移除）。声卡移除是可靠取消信号源（R2）。
///
/// 在 set_port 的各操作阶段（直接设端口/切profile/最终设端口）前调用，
/// 确保任何阶段的卡移除都能终止操作。
pub fn ensure_card_alive(
    device_manager: &Arc<RwLock<DeviceManager>>,
    card_id: u32,
) -> Result<(), String> {
    if !device_manager.read().cards.contains_key(&card_id) {
        return Err(format!("card {card_id} removed"));
    }
    Ok(())
}

/// 切换声卡 profile 并等待设备重建完成。
///
/// 置 Pending + change → 提交切换 → 阻塞等待 event_loop 通知设备创建完成。
/// `cancel` 为该操作取消令牌，被更高优先级任务取代时提前结束。
pub fn switch_card_profile(
    pulse: &Arc<PulseManager>,
    device_manager: &Arc<RwLock<DeviceManager>>,
    card_id: u32,
    profile: &str,
    cancel: &std::sync::atomic::AtomicBool,
) -> Result<(), String> {
    use CardStatus;
    use ChangeResult;
    use StatusChange;
    use DIRECTION_SINK;
    use DIRECTION_SOURCE;

    // 记录切换前该声卡的设备方向（重建后需全部齐全）
    let required_directions = {
        let dm = device_manager.read();
        let mut dirs = 0u32;
        if dm.sinks.values().any(|s| s.card == card_id) {
            dirs |= DIRECTION_SINK;
        }
        if dm.sources.values().any(|s| s.card == card_id) {
            dirs |= DIRECTION_SOURCE;
        }
        dirs
    };

    let op = StatusChange::new(required_directions);
    {
        let mut dm = device_manager.write();
        if let Some(card) = dm.cards.get_mut(&card_id) {
            card.status = CardStatus::Pending;
            card.change = Some(op.clone());
        }
    }

    pulse_card::set_card_profile(pulse, card_id, profile)?;

    // 阻塞等待 event_loop 通知：完成/声卡移除/取消/失败/超时。
    // 用 wait_cancellable：被更高优先级任务取代时提前醒来让出锁。
    let result = op.wait_cancellable(cancel, std::time::Duration::from_secs(5))?;
    // 等待期间卡可能被移除（R2），wait 返回后再确认
    ensure_card_alive(device_manager, card_id)?;
    match result {
        ChangeResult::Complete => Ok(()),
        ChangeResult::Removed => Err(format!("card {card_id} removed during profile switch")),
        ChangeResult::Cancelled => Err(format!("profile switch cancelled for card {card_id}")),
        ChangeResult::Failed(e) => Err(format!("profile switch failed: {e}")),
    }
}

/// 设置声卡端口。
///
/// 由 AudioManager 的 executor 分发调用（串行，无并发）。`cancel` 为该
/// 任务取消令牌，被更高优先级任务取代时提前结束。
///
/// 流程：R1 Pending 等待 → 查目标端口/设备 → 设备已有端口则直接设置 →
/// 否则切换 profile 并等待重建 → 设置端口。
pub fn set_port(
    pulse: &Arc<PulseManager>,
    device_manager: &Arc<RwLock<DeviceManager>>,
    config: &Arc<super::config::AudioConfig>,
    card_id: u32,
    port_name: &str,
    direction: u32,
    cancel: &std::sync::atomic::AtomicBool,
) -> Result<(), String> {
    use crate::backend::pulse::sink as pulse_sink;
    use crate::backend::pulse::source as pulse_source;
    use CardStatus;
    use ChangeResult;

    // R1：声卡 Pending 时等待就绪；被更高优先级任务取代(cancel)则终止。
    loop {
        let status = {
            let dm = device_manager.read();
            dm.cards.get(&card_id).map(|c| c.status)
        };
        match status {
            Some(CardStatus::Ready) => break,
            Some(CardStatus::Pending) => {
                let change = {
                    let dm = device_manager.read();
                    dm.cards.get(&card_id).and_then(|c| c.change.clone())
                };
                match change {
                    Some(ch) => {
                        let r = ch.wait_cancellable(cancel, std::time::Duration::from_secs(5));
                        match r? {
                            ChangeResult::Complete => continue,
                            ChangeResult::Cancelled => {
                                return Err("port operation cancelled by a newer request".into())
                            }
                            ChangeResult::Removed => {
                                return Err(format!("card {card_id} removed while waiting for ready"))
                            }
                            ChangeResult::Failed(e) => return Err(e),
                        }
                    }
                    None => return Err(format!("card {card_id} pending but no change handle")),
                }
            }
            Some(CardStatus::Removing) | None => {
                return Err(format!("card {card_id} not available"));
            }
        }
    }

    // 从 DeviceManager 读取声卡状态
    let (active_profile, device_index, port_has_profile) = {
        let dm = device_manager.read();
        let card = dm.cards.get(&card_id).ok_or_else(|| {
            format!("card {card_id} not found")
        })?;

        // 查找目标端口
        let port = card.ports.iter().find(|p| p.name == port_name).ok_or_else(|| {
            format!("port {port_name} not found on card {card_id}")
        })?;

        // 查找该 card 的 sink/source 索引
        let device_index = if direction == 1 {
            dm.sinks.values().find(|s| s.card == card_id).map(|s| s.index)
        } else {
            dm.sources.values().find(|s| s.card == card_id).map(|s| s.index)
        };

        (
            card.active_profile.clone(),
            device_index,
            !port.profiles.is_empty(),
        )
    };

    // 1. 设备已存在且包含目标端口 → 直接设置
    if let Some(index) = device_index {
        let has_port = {
            let dm = device_manager.read();
            if direction == 1 {
                dm.sinks
                    .get(&index)
                    .map(|s| s.ports.iter().any(|p| p.name == port_name))
                    .unwrap_or(false)
            } else {
                dm.sources
                    .get(&index)
                    .map(|s| s.ports.iter().any(|p| p.name == port_name))
                    .unwrap_or(false)
            }
        };
        if has_port {
            eprintln!("[dde-audio] set_port: device {index} already has port {port_name}, set directly");
            let r = if direction == 1 {
                pulse_sink::set_port(pulse, index, port_name)
            } else {
                pulse_source::set_port(pulse, index, port_name)
            };
            r?;
            apply_saved_state(pulse, device_manager, config, card_id, port_name, index, direction);
            return Ok(());
        }
    }

    // 2. 设备不存在该端口，需要切 profile
    if !port_has_profile {
        return Err(format!("port {port_name} has no profile on card {card_id}"));
    }

    // 确定目标 profile（从 DeviceManager 读端口 select_profile 结果）
    let target_profile = {
        let dm = device_manager.read();
        let card = dm.cards.get(&card_id).ok_or_else(|| {
            format!("card {card_id} not found")
        })?;
        let port = card.ports.iter().find(|p| p.name == port_name).ok_or_else(|| {
            format!("port {port_name} not found on card {card_id}")
        })?;
        port.select_profile().map(|s| s.to_owned())
    };

    let target_profile = match target_profile {
        Some(p) if !p.is_empty() => p,
        _ => return Err(format!("no available profile for card {card_id} port {port_name}")),
    };

    if active_profile != target_profile {
        // 3. profile 不同：切换 profile 并等待设备重建完成
        eprintln!(
            "[dde-audio] set_port: switch card {card_id} profile {active_profile} -> {target_profile}"
        );
        switch_card_profile(pulse, device_manager, card_id, &target_profile, cancel)?;
    }

    // 4. 设备已重建（或未切换），从 DeviceManager 查该 card 的 sink/source 并设置端口
    let device_index = {
        let dm = device_manager.read();
        if direction == 1 {
            dm.sinks.values().find(|s| s.card == card_id).map(|s| s.index)
        } else {
            dm.sources.values().find(|s| s.card == card_id).map(|s| s.index)
        }
    };

    match device_index {
        Some(index) => {
            let r = if direction == 1 {
                pulse_sink::set_port(pulse, index, port_name)
            } else {
                pulse_source::set_port(pulse, index, port_name)
            };
            r?;
            apply_saved_state(pulse, device_manager, config, card_id, port_name, index, direction);
            Ok(())
        }
        None => Err(format!("no device for card {card_id}")),
    }
}

/// 端口设置成功后，恢复该端口持久化的音量与平衡（Go 版 GetConfigKeeper 语义）。
fn apply_saved_state(
    pulse: &Arc<PulseManager>,
    device_manager: &Arc<RwLock<DeviceManager>>,
    config: &Arc<super::config::AudioConfig>,
    card_id: u32,
    port_name: &str,
    device_index: u32,
    direction: u32,
) {
    use crate::backend::pulse::sink as pulse_sink;
    use crate::backend::pulse::source as pulse_source;
    // 卡名
    let card_name = {
        let dm = device_manager.read();
        dm.cards.get(&card_id).map(|c| c.name.clone())
    };
    let Some(card_name) = card_name else {
        return;
    };
    let st = config.port_state(&card_name, port_name);
    if direction == 1 {
        let _ = pulse_sink::set_volume(pulse, device_index, st.volume, false);
        let _ = pulse_sink::set_balance(pulse, device_index, st.balance, false);
    } else {
        let _ = pulse_source::set_volume(pulse, device_index, st.volume, false);
        let _ = pulse_source::set_balance(pulse, device_index, st.balance, false);
    }
}
