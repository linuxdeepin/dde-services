# IPwatchD Plugin

IP 冲突检测插件，基于 ipwatchd 实现，提供 D-Bus 接口用于检测和通知 IP 地址冲突。

## 目录结构

```
plugin-ipwatchd/
├── upstream/              # 原始 ipwatchd 源码
│   ├── ipwatchd.c        # 主程序
│   ├── ipwatchd.h        # 核心头文件
│   ├── analyse.c         # ARP 包分析
│   ├── config.c          # 配置解析
│   ├── devinfo.c         # 设备信息获取
│   ├── genarp.c          # ARP 包生成
│   ├── message.c         # 日志消息
│   ├── signal.c          # 信号处理
│   └── daemonize.c       # 守护进程化
│
├── hooks/                 # 钩子接口层
│   ├── ipwatchd_hooks.h  # 钩子函数声明
│   └── ipwatchd_hooks.c  # 默认空实现
│
├── plugin/                # 插件实现层
│   ├── plugin_adapter.c  # 插件适配器
│   ├── plugin_adapter.h  # 适配器头文件
│   ├── service.c         # D-Bus 服务
│   ├── service.h         # 服务头文件
│   └── plugin.c          # 插件入口
│
├── CMakeLists.txt         # 构建配置
├── ipwatchd.conf          # 配置文件
├── org.deepin.ipwatchd.conf  # D-Bus 配置
└── plugin-ipwatchd.json   # 插件元数据
```

## 功能特性

### 1. IP 冲突检测
- 被动监听网络中的 ARP 包，检测 IP 地址冲突
- 主动探测指定 IP 是否存在冲突
- 支持主动模式（发送 ARP 响应）和被动模式（仅记录）

### 2. 冲突解除检测
- 自动检测冲突是否已解除
- 定期探测机制（每 5 秒）
- 多重解除判断：IP 变化、连续无冲突响应、超时解除

### 3. D-Bus 接口
- 提供方法调用进行主动检测
- 发送信号通知冲突和解除事件

## 架构设计

### 钩子机制
使用 GCC 弱符号实现可选钩子，保持上游代码的独立性：
- **upstream/**: ipwatchd 核心逻辑，最小化修改
- **hooks/**: 钩子接口定义
- **plugin/**: 插件实现（D-Bus、冲突追踪、定期探测）

## 编译安装

```bash
mkdir -p build
cd build
cmake ..
make
sudo make install
```

生成 `libplugin-ipwatchd.so`，由 deepin-service-manager 加载。

### 权限配置

ipwatchd 插件通过 deepin-service-group@deepin-daemon.service 框架常驻启动，以 `deepin-daemon` 用户运行，通过 systemd capabilities 获取网络权限。

**前提条件**

确保 `deepin-daemon` 用户存在（通常由 deepin-daemon 包创建）：

```bash
# 检查用户是否存在
id deepin-daemon

# 如果不存在，创建用户
sudo useradd -r -s /sbin/nologin -d /var/lib/deepin-daemon -c "Deepin Daemon" deepin-daemon
```

**安装后配置**

```bash
# 设置配置目录权限
sudo mkdir -p /var/lib/ipwatchd
sudo chown deepin-daemon:deepin-daemon /var/lib/ipwatchd

# 重新加载 systemd
sudo systemctl daemon-reload

# 重启 deepin-daemon 服务组（插件会随框架常驻启动）
sudo systemctl restart deepin-service-group@deepin-daemon.service
```

**启动服务**

插件通过 deepin-service-group@deepin-daemon.service 框架常驻启动，无需单独启动：

```bash
# 查看服务组状态
systemctl status deepin-service-group@deepin-daemon.service

# 测试 D-Bus 调用
busctl call org.deepin.dde.IPWatchD1 \
    /org/deepin/dde/IPWatchD1 \
    org.deepin.dde.IPWatchD1 \
    RequestIPConflictCheck ss "192.168.1.100" ""
```

**验证权限**

```bash
# 查看进程用户
ps aux | grep deepin-service-manager | grep deepin-daemon

# 查看进程 capabilities
cat /proc/$(pgrep -f "deepin-service-group@deepin-daemon")/status | grep Cap

# 查看 override 配置
systemctl cat deepin-service-group@deepin-daemon.service
```

## 钩子接口

插件通过以下钩子扩展 ipwatchd 核心功能：

| 钩子函数 | 触发时机 | 用途 |
|---------|---------|------|
| `ipwd_hook_on_arp_packet()` | 解析完 ARP 包后 | 处理 D-Bus 检测请求 |
| `ipwd_hook_on_conflict()` | 检测到 IP 冲突时 | 发送冲突信号，追踪冲突 |
| `ipwd_hook_on_conflict_resolved()` | 收到非冲突包时 | 更新冲突状态，发送解除信号 |
| `ipwd_hook_on_ip_changed()` | IP 地址变化时 | 清理旧 IP 的冲突记录 |
| `ipwd_hook_on_config_loaded()` | 配置加载完成后 | 初始化数据结构 |
| `ipwd_hook_on_pcap_ready()` | pcap 初始化完成后 | 保存句柄用于主动探测 |

## 使用方法

### 监听 D-Bus 信号
```bash
dbus-monitor --system "type='signal',interface='org.deepin.dde.IPWatchD1'"
```

### 主动检测 IP 冲突
```bash
busctl call org.deepin.dde.IPWatchD1 \
    /org/deepin/dde/IPWatchD1 \
    org.deepin.dde.IPWatchD1 \
    RequestIPConflictCheck ss "192.168.1.100" ""
```

### 查看日志
```bash
journalctl -u deepin-service-group@deepin-daemon.service -f | grep ipwatch
```

## D-Bus 接口

**服务名**: `org.deepin.dde.IPWatchD1`  
**对象路径**: `/org/deepin/dde/IPWatchD1`  
**接口**: `org.deepin.dde.IPWatchD1`

### 方法

#### RequestIPConflictCheck
主动检测指定 IP 是否存在冲突。

**签名**: `RequestIPConflictCheck(ss) → s`

**参数**:
- `ip` (string): 要检测的 IP 地址
- `device` (string): 网卡名称，空字符串表示自动选择

**返回**:
- `mac` (string): 冲突的 MAC 地址，空字符串表示无冲突

### 信号

#### IPConflict
检测到 IP 冲突时发送。

**签名**: `IPConflict(sss)`

**参数**:
- `ip` (string): 冲突的 IP 地址
- `local_mac` (string): 本地 MAC 地址
- `remote_mac` (string): 远程冲突设备的 MAC 地址

#### IPConflictReslove
IP 冲突解除时发送。

**签名**: `IPConflictReslove(sss)`

**参数**:
- `ip` (string): 解除冲突的 IP 地址
- `local_mac` (string): 本地 MAC 地址
- `remote_mac` (string): 远程设备的 MAC 地址

## 配置文件

配置文件位于 `/etc/ipwatchd.conf`：

```ini
# Syslog facility
syslog_facility daemon

# Defend interval (seconds) - 防御间隔
defend_interval 10

# Interface configuration mode - 接口配置模式
iface_configuration automatic

# 或手动模式：
# iface_configuration manual
# iface eth0 active    # 主动模式：发送 ARP 响应
# iface wlan0 passive  # 被动模式：仅记录冲突
```

## 冲突解除机制

插件实现了多重冲突解除检测机制：

1. **IP 变化检测**: 本地或远程 IP 变化时立即解除
2. **定期探测**: 每 5 秒主动探测冲突状态
3. **去抖机制**: 连续 3 次收到非冲突包后解除
4. **超时解除**: 5 分钟未见冲突自动解除

## 技术特点

- 最小化修改上游代码，便于维护和升级
- 使用弱符号实现钩子，保持核心代码独立性
- 独立线程进行定期探测，不阻塞主流程
- 完整的冲突追踪和状态管理

## 许可证

- upstream/: GPLv2 (原始 ipwatchd 许可证)
- hooks/, plugin/: LGPL-3.0-or-later (UnionTech)
