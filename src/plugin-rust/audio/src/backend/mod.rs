// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

//! 音频后端。
//!
//! 通过 libpulse-binding 连接 PulseAudio / pipewire-pulse 兼容层。
//! 只管连接和操作，不保存业务状态。状态管理在 `manager` 模块。

pub mod pulse;
