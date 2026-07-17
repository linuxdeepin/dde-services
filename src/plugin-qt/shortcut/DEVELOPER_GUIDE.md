# Deepin 快捷键服务 - 应用适配开发者指南

## 目录

- [1. 快速开始](#1-快速开始)
- [2. 配置文件编写](#2-配置文件编写)
- [3. 国际化翻译](#3-国际化翻译)
- [4. 文件部署](#4-文件部署)
- [5. DBus 接口使用](#5-dbus-接口使用)
- [6. 测试与调试](#6-测试与调试)
- [7. 附录](#7-附录)

---

## 1. 快速开始

### 1.1 概述

Deepin 快捷键服务是一个基于 DConfig 的动态快捷键管理框架，支持：

- ✅ X11 和 Wayland 双平台
- ✅ X11 和 Wayland 下的键盘快捷键与触控板手势
- ✅ 动态配置加载和热更新
- ✅ 完整的国际化支持
- ✅ 冲突检测和用户自定义

### 1.2 适配流程概览

```
1. 编写 JSON 配置文件（定义快捷键行为）
   ↓
2. 编写 INI 注册文件（告诉服务去哪里找配置）
   ↓
3. 处理国际化翻译（可选，仅 modifiable=true 需要）
   ↓
4. 部署配置文件和翻译文件
   ↓
5. 测试和验证
```

### 1.3 运行架构

快捷键插件运行在 `deepin-service-manager` 的 `dde` 分组进程中，对外提供 session bus
接口 `org.deepin.dde.Keybinding1` 和 `org.deepin.dde.Gesture1`。配置、冲突检测、持久化
和本地化由公共管理层处理，输入后端按桌面环境选择：

- X11 使用 `X11KeyHandler` 抓取按键，并通过 system bus 手势采集服务接收触控板事件。
- Wayland 使用 Treeland shortcut 协议注册按键和手势。

应用使用相同的 DConfig 格式和 D-Bus 接口，不应自行维护 X11/Wayland 两套配置。

## 2. 配置文件编写

快捷键服务使用两种配置文件协同工作：

- **JSON 配置文件**：定义每个快捷键的具体行为（使用 DConfig 格式）
- **INI 注册文件**：告诉快捷键服务去哪里找 JSON 配置文件

### 2.1 JSON 配置文件（快捷键和手势）

快捷键配置文件名：

- 键盘快捷键：`org.deepin.shortcut.json`
- 手势：`org.deepin.gesture.json`

#### 2.1.1 配置文件结构

快捷键配置文件使用 DConfig 的 JSON 格式，每个快捷键对应一个独立的配置文件。

**基本结构：**

```json
{
  "magic": "dsg.config.meta",
  "version": "1.0",
  "contents": {
    "appId": { ... },
    "displayName": { ... },
    "displayOrder": { ... },
    "hotkeys": { ... },
    "keyEventFlags": { ... },
    "triggerType": { ... },
    "triggerValue": { ... },
    "category": { ... },
    "enabled": { ... },
    "modifiable": { ... }
  }
}
```

#### 2.1.2 字段详细说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `appId` | string | ✅ | 应用程序 ID，如 "deepin-editor" |
| `displayName` | string | ✅ | 快捷键显示名称，如果这个字段需要国际化，需要提供翻译 |
| `displayOrder` | int | ❌ | 分类内的显示顺序，数值越小越靠前；建议使用 10、20、30 递增，缺省或负数表示未指定 |
| `hotkeys` | array[string] | ✅ | 按键组合列表，如 ["Ctrl+Alt+T"]，支持多个不同按键组合触发一个操作 |
| `triggerType` | int | ✅ | 触发类型：1=命令, 2=应用, 3=动作 |
| `triggerValue` | array | ✅ | 触发值，元素类型根据 triggerType 不同而不同 |
| `category` | string | ✅ | 提供方拥有的逻辑分类键；应用可定义自己的分类，`Custom` 保留给服务创建的运行时自定义快捷键 |
| `enabled` | bool | ✅ | 是否启用，默认 true |
| `modifiable` | bool | ✅ | 用户是否可修改快捷键的hotKeys |
| `keyEventFlags` | int | ❌ | 触发时机标志位，默认 2（释放时） |

**hotkeys 字段说明：**

- 配置与冲突检测只保存逻辑键名；`KP_Delete` 等物理别名会在服务端归一成规范名（见 `PhysicalKeyAlias::canonicalize`）。
- Wayland 通过 Qt/合成器匹配逻辑键，一般无需再为小键盘单独注册。
- X11 按 keycode grab，需为逻辑键可选绑定物理别名（见 `PhysicalKeyAlias::expandX11Candidates`，表在 `physicalkeyalias.cpp`）。

支持两种格式：

1. **Qt 标准按键名称**（按键快捷键，必须使用Qt定义的标准按键名称）
   - 示例：`"Meta+D"`, `"Ctrl+Alt+T"`, `"Volume Mute"`
   - 参考：[Qt QKeySequence 文档](https://doc.qt.io/qt-6/qkeysequence.html)

2. **Linux 按键码**（用于特殊按键，Qt未有定义的按键或者适配厂商特殊按键）
   - 10进制：`"530"` (触摸板开关)
   - 16进制：`"0x212"` (触摸板开关)
   - 常用按键码见 [附录 7.2](#72-linux-按键码参考)

**triggerType 和 triggerValue：**

| triggerType | 说明 | triggerValue 格式 | 示例 |
|-------------|------|-------------------|------|
| 1 (Command) | 执行命令 | `["/path/to/exe", "arg1", "arg2"]` | `["/usr/bin/deepin-editor"]` |
| 2 (App) | 启动应用 | `["appId"]` | `["deepin-editor"]` |
| 3 (Action) | 平台动作，由当前后端或公共服务执行 | `[actionId]` | `[20]` (锁屏) |

运行时自定义快捷键接口接收一条命令行字符串，但服务端会在保存前将其解析为
`triggerValue` 参数数组，第一个元素为程序，其余元素为 argv。解析支持单双引号、
反斜杠转义和空参数；引号未闭合或末尾只有反斜杠时拒绝保存。执行时不会经过 shell，
管道、重定向或命令连接等 shell 语法需要显式写成 `/bin/sh -c '...'`。

为兼容旧版通过 Desktop Entry 启动自定义命令的行为，在无文件输入的快捷键触发场景中，
`%f`、`%F`、`%u`、`%U` 会被移除，`%%` 会还原为字面量 `%`。例如：

```text
/usr/bin/ll-cli run org.deepin.music --file %F -- -- deepin-music %%F
```

保存为独立参数后，触发时传递给程序的参数等价于：

```text
/usr/bin/ll-cli run org.deepin.music --file -- -- deepin-music %F
```

**keyEventFlags 标志位：**

| 值 | 说明 | 适用场景 |
|----|------|----------|
| 1 (0x1) | 按下时触发，key_press | 快速响应场景 |
| 2 (0x2) | 释放时触发（默认），key_release | 常规快捷键 |
| 4 (0x4) | 重复时触发（长按），repeat | 音量、亮度调节 |
| 5 (0x5) | 按下+重复，key_press \| repeat | repeat连续触发场景 |

**displayOrder 字段说明：**

- `displayOrder` 只在同一个 `category` 内比较；分类本身的顺序由快捷键服务统一管理。
- 建议按照 `10、20、30...` 编号，方便以后在已有配置之间插入新项目。
- 相同顺序值使用配置 ID 作为稳定的第二排序条件。
- 缺少该字段或值小于 0 的普通配置会排在所属分类末尾。
- 旧版运行时自定义快捷键是例外：未设置 `displayOrder` 的项目按用户级
  `SubPaths` 保存顺序排列，并整体位于带 `displayOrder` 的新项目之前。
- 用户级自定义快捷键的 `SubPaths` 必须保持追加顺序，读写时不得按配置 ID 排序。
- 服务端返回的全量列表、按应用列表、按分类列表和搜索结果采用同一排序规则。
- 运行时新增的自定义快捷键由服务端自动分配当前最大顺序值加 10，修改时保持原顺序。
- 普通内置配置的 `displayOrder` 应保持只读；运行时自定义快捷键模板必须设置为 `readwrite`，由服务端在创建时写入。
- 手势配置也应声明该字段，并按照整个手势列表全局编号；当前字段作为统一配置元数据保留。

#### 2.1.3 键盘快捷键配置示例

**示例 1：基础快捷键（完整示例）**

```json
{
    "magic": "dsg.config.meta",
    "version": "1.0",
    "contents": {
        "appId": {
            "value": "org.deepin.dde.keybinding",
            "serial": 0,
            "flags": [],
            "name": "appId",
            "name[zh_CN]": "提供快捷键应用Id",
            "description": "provide shortcut app id",
            "permissions": "",
            "visibility": "private"
        },
        "displayName": {
            "value": "terminal",
            "serial": 0,
            "flags": [],
            "name": "displayName",
            "name[zh_CN]": "终端",
            "description": "打开系统默认终端程序",
            "permissions": "",
            "visibility": "private"
        },
        "displayOrder": {
            "value": 10,
            "serial": 1,
            "flags": [],
            "name": "displayOrder",
            "name[zh_CN]": "显示顺序",
            "description": "Display order within the category",
            "permissions": "",
            "visibility": "private"
        },
        "hotkeys": {
            "value": [
                "Ctrl+Alt+T"
            ],
            "serial": 0,
            "flags": [],
            "name": "hotkeys",
            "name[zh_CN]": "快捷键组合",
            "description": "打开默认终端快捷键组合",
            "permissions": "readwrite",
            "visibility": "private"
        },
        "keyEventFlags": {
          "value": 2,
          "serial": 0,
          "flags": [],
          "name": "keyEventFlags",
          "name[zh_CN]": "按键事件标志",
          "description": "1=Press, 2=Release(default), 4=Repeat",
          "permissions": "",
          "visibility": "private"
        },
        "triggerType": {
            "value": 1,
            "serial": 0,
            "flags": [],
            "name": "triggerType",
            "name[zh_CN]": "快捷键触发动作类型",
            "description": "二进制command类型",
            "permissions": "",
            "visibility": "private"
        },
        "triggerValue": {
            "value": [
                "/usr/lib/deepin-daemon/default-terminal"
            ],
            "serial": 0,
            "flags": [],
            "name": "triggerValue",
            "name[zh_CN]": "快捷键触发动作",
            "description": "打开默认终端程序",
            "permissions": "",
            "visibility": "private"
        },
        "category": {
            "value": "System",
            "serial": 1,
            "flags": [],
            "name": "category",
            "name[zh_CN]": "快捷键类别（System）",
            "description": "系统快捷键",
            "permissions": "",
            "visibility": "private"
        },
        "enabled": {
            "value": true,
            "serial": 0,
            "flags": [],
            "name": "enabled",
            "name[zh_CN]": "使能",
            "description": "是否启用快捷键",
            "permissions": "",
            "visibility": "private"
        },
        "modifiable": {
            "value": true,
            "serial": 0,
            "flags": [],
            "name": "modifiable",
            "name[zh_CN]": "能否修改快捷键",
            "description": "不能修改快捷键",
            "permissions": "",
            "visibility": "private"
        }
    }
}
```

**示例 2：命令类型快捷键（仅展示关键字段）**

```json
{
  "magic": "dsg.config.meta",
  "version": "1.0",
  "contents": {
	...
    "hotkeys": {
      "value": ["Print"]
    },
    "triggerType": {
      "value": 1
    },
    "triggerValue": {
      "value": [
        "/usr/bin/dbus-send",
        "--print-reply",
        "--dest=com.deepin.Screenshot",
        "/com/deepin/Screenshot",
        "com.deepin.Screenshot.StartScreenshot"
      ]
    },
      ...
  }
}
```


**示例 3：应用启动类型快捷键（仅展示关键字段）**可以使用dde-am --list，查看系统已有的appId

```json
{
  "magic": "dsg.config.meta",
  "version": "1.0",
  "contents": {
	...
    "hotkeys": {
      "value": ["Meta+E"]
    },
    "triggerType": {
      "value": 2
    },
    "triggerValue": {
      "value": ["dde-file-manager"]
    },
   	...
  }
}
```

**示例 4：特殊按键码（仅展示关键字段）**

```json
{
  "magic": "dsg.config.meta",
  "version": "1.0",
	...
    "hotkeys": {
      "value": ["530"],
      "description": "KEY_TOUCHPAD_TOGGLE (0x212)"
    },
    "triggerType": {
      "value": 1
    },
    "triggerValue": {
      "value": ["/usr/bin/dde-shortcut-tool", "touchpad", "toggle"]
    },
    ...
  }
}
```

**示例 5：长按重复触发（仅展示关键字段）**

```json
{
  "magic": "dsg.config.meta",
  "version": "1.0",
  "contents": {
	...
    "hotkeys": {
      "value": ["Volume Up"]
    },
    "keyEventFlags": {
      "value": 5,
      "description": "Press(1) + Repeat(4) = 5, for continuous volume adjustment"
    },
    "triggerType": {
      "value": 1
    },
    "triggerValue": {
      "value": ["/usr/bin/dde-shortcut-tool", "audio", "up"]
    },
  	...  
  }
}
```


#### 2.1.4 手势快捷键配置

同一份手势配置同时用于 X11 和 Wayland。X11 后端订阅 system bus 的底层手势事件，
Wayland 后端通过 Treeland 协议注册；动作支持范围由服务返回的 `availableActions`
决定，调用方不应按会话类型硬编码动作列表。

**完整示例：三指向上滑动显示多任务视图**

```json
{
  "magic": "dsg.config.meta",
  "version": "1.0",
  "contents": {
    "appId": {
      "value": "treeland",
      "serial": 0,
      "flags": [],
      "name": "appId",
      "permissions": "readwrite",
      "visibility": "private"
    },
    "displayName": {
      "value": "Show Multitask View",
      "serial": 0,
      "flags": [],
      "name": "displayName",
      "permissions": "readwrite",
      "visibility": "private"
    },
    "displayOrder": {
      "value": 10,
      "serial": 1,
      "flags": [],
      "name": "displayOrder",
      "permissions": "",
      "visibility": "private"
    },
    "gestureType": {
      "value": 1,
      "serial": 0,
      "flags": [],
      "name": "gestureType",
      "description": "1=Swipe, 2=Hold",
      "permissions": "readwrite",
      "visibility": "private"
    },
    "fingerCount": {
      "value": 3,
      "serial": 0,
      "flags": [],
      "name": "fingerCount",
      "permissions": "readwrite",
      "visibility": "private"
    },
    "direction": {
      "value": 3,
      "serial": 0,
      "flags": [],
      "name": "direction",
      "description": "0=None, 1=Down, 2=Left, 3=Up, 4=Right",
      "permissions": "readwrite",
      "visibility": "private"
    },
    "triggerType": {
      "value": 3,
      "serial": 0,
      "flags": [],
      "name": "triggerType",
      "permissions": "readwrite",
      "visibility": "private"
    },
    "triggerValue": {
      "value": [16],
      "serial": 0,
      "flags": [],
      "name": "triggerValue",
      "description": "16 = toggle_multitask_view action",
      "permissions": "readwrite",
      "visibility": "private"
    },
    "category": {
      "value": "System",
      "serial": 1,
      "flags": [],
      "name": "category",
      "permissions": "",
      "visibility": "private"
    },
    "enabled": {
      "value": true,
      "serial": 0,
      "flags": [],
      "name": "enabled",
      "permissions": "",
      "visibility": "private"
    },
    "modifiable": {
      "value": false,
      "serial": 0,
      "flags": [],
      "name": "modifiable",
      "permissions": "",
      "visibility": "private"
    }
  }
}
```

#### 2.1.5 配置文件验证

在部署前，建议验证 JSON 格式：

```bash
# 验证 JSON 格式（根据类型使用对应文件名）
jq . org.deepin.shortcut.json   # 键盘快捷键
jq . org.deepin.gesture.json    # 手势

# 检查必填字段
jq '.contents | keys' org.deepin.shortcut.json
```

**必填字段检查清单：**

- ✅ appId
- ✅ displayName
- ✅ displayOrder（建议配置；不可修改且不会展示的系统快捷键可省略）
- ✅ hotkeys (或 gestureType/fingerCount/direction)
- ✅ triggerType
- ✅ triggerValue
- ✅ category
- ✅ enabled
- ✅ modifiable

### 2.2 INI 注册文件

为了让快捷键服务发现你的配置，需要创建 INI 格式的注册文件。

#### 2.2.1 注册文件的作用

INI 注册文件告诉快捷键服务：
- 你的应用提供了哪些快捷键配置
- 这些配置文件的 SubPath（子路径）是什么
- 快捷键服务在启动和重载时会扫描这些注册文件，自动加载对应的 JSON 配置

#### 2.2.2 注册文件路径

```
/usr/share/deepin/org.deepin.dde.keybinding/<appId>.ini
```

**示例：**

```
/usr/share/deepin/org.deepin.dde.keybinding/deepin-terminal.ini
/usr/share/deepin/org.deepin.dde.keybinding/deepin-editor.ini
```

#### 2.2.3 注册文件格式

**基本格式（INI）：**

```ini
[Config]
SubPaths=<appId>.shortcut.xxx;<appId>.shortcut.yyy;<appId>.gesture.zzz
```

**字段说明：**

- `SubPaths`: DConfig 子路径列表，使用分号（`;`）分隔
- 每个 SubPath 对应一个 JSON 配置文件
- SubPath 格式：`<appId>.<type>.<name>`
  - `appId`: 应用程序 ID
  - `type`: `shortcut`（键盘快捷键）或 `gesture`（手势）
  - `name`: 快捷键的唯一标识（建议使用 displayName 的英文形式）

#### 2.2.4 注册文件示例

**示例 1：deepin-terminal.ini**

```ini
[Config]
SubPaths=deepin-terminal.shortcut.openterminal;deepin-terminal.shortcut.quake
```

这个注册文件告诉快捷键服务加载两个配置：
- `/usr/share/dsg/configs/org.deepin.dde.keybinding/deepin-terminal.shortcut.openterminal/org.deepin.shortcut.json`
- `/usr/share/dsg/configs/org.deepin.dde.keybinding/deepin-terminal.shortcut.quake/org.deepin.shortcut.json`

**示例 2：混合键盘和手势**

```ini
[Config]
SubPaths=deepin-myapp.shortcut.action1;deepin-myapp.shortcut.action2;deepin-myapp.gesture.swipe3up
```

混合配置时，`shortcut` 类型加载 `org.deepin.shortcut.json`，`gesture` 类型加载 `org.deepin.gesture.json`。

#### 2.2.5 SubPath 命名规范

**格式要求：**

```
<appId>.<type>.<name>
```

**命名建议：**

- 使用小写字母
- 使用简短的英文单词
- 多个单词可以连写或使用连字符
- `name` 部分建议与 displayName 对应

**好的示例：**

```
deepin-editor.shortcut.newwindow
deepin-editor.shortcut.save
deepin-editor.shortcut.open-file
deepin-terminal.shortcut.openterminal
deepin-terminal.shortcut.quake
treeland.gesture.swipe3up
treeland.gesture.swipe4down
```

**不好的示例：**

```
editor.shortcut.1              // appId 不完整
deepin-editor.shortcut.        // name 为空
deepin-editor.NewWindow        // 缺少 type
deepin-editor.shortcut.新窗口   // 使用中文
```

#### 2.2.6 配置文件与注册文件的关系

```
注册文件（INI）
    ↓ 指向
SubPath（子路径）
    ↓ 对应
JSON 配置文件
```

**完整示例：**

1. **注册文件**：`/usr/share/deepin/org.deepin.dde.keybinding/deepin-myapp.ini`
   ```ini
   [Config]
   SubPaths=deepin-myapp.shortcut.action1;deepin-myapp.shortcut.action2
   ```

2. **JSON 配置文件 1**：
   ```
   /usr/share/dsg/configs/org.deepin.dde.keybinding/deepin-myapp.shortcut.action1/org.deepin.shortcut.json
   ```

3. **JSON 配置文件 2**：
   ```
   /usr/share/dsg/configs/org.deepin.dde.keybinding/deepin-myapp.shortcut.action2/org.deepin.shortcut.json
   ```

#### 2.2.7 热加载机制

当应用安装或卸载时，快捷键服务会自动重载配置：

1. **Debian Triggers**：监听 `/usr/share/deepin/org.deepin.dde.keybinding` 目录
2. **自动触发**：当 INI 文件变化时，触发 `postinst` 脚本
3. **DBus 通知**：脚本调用 `ReloadConfigs` 方法通知服务重载

**手动触发重载：**

```bash
dbus-send --session --type=method_call \
    --dest=org.deepin.dde.Keybinding1 \
    /org/deepin/dde/Keybinding1 \
    org.deepin.dde.Keybinding1.ReloadConfigs
```

---

## 3. 国际化翻译

### 3.1 翻译文件准备

只有 `modifiable` 为 `true` 的快捷键需要提供翻译。系统级不可修改的快捷键（`modifiable: false`）不需要翻译。还有一点是看是否需要在控制中心的快捷键页面显示，国际化翻译主要作用在此。

**翻译文件部署路径：**

```
/usr/share/deepin/org.deepin.dde.keybinding/translations/<appId>/<appId>_<lang>.qm
```

**示例：**

```
/usr/share/deepin/org.deepin.dde.keybinding/translations/deepin-editor/deepin-editor_zh_CN.qm
/usr/share/deepin/org.deepin.dde.keybinding/translations/deepin-editor/deepin-editor_de.qm
```

### 3.2 使用 CMake 集成工具

快捷键服务提供了 `DdeShortcutI18n` CMake 模块，可以自动化处理翻译流程。

#### 3.2.1 DdeShortcutI18n 模块

该模块提供 `dde_shortcut_add_translations` 函数，自动完成：

1. 从用户可见的 JSON 配置中提取 `displayName` 和字符串 `category` 字段
2. 生成 `.cpp` 文件供 `lupdate` 扫描
3. 生成 `.ts` 翻译模板文件
4. 编译 `.qm` 文件
5. 安装到正确的位置

#### 3.2.2 CMakeLists.txt 配置示例

**基本用法：**

```cmake
# 查找必要的 Qt 组件
find_package(Qt6 REQUIRED COMPONENTS Core LinguistTools)

# 查找 DdeShortcutI18n 模块
find_package(DdeShortcutI18n REQUIRED)

# 指定支持的语言
set(I18N_LANGUAGES 
    "zh_CN" "zh_HK" "zh_TW" "en_US" "ast" "az" "bg" "bo" "ca" "cs" "da" 
    "de" "el" "es" "et" "eu" "fa" "fi" "fr" "gl" "he" "hi" "hr" "hu" 
    "hy" "id" "it" "ja" "ka" "kk" "ko" "ky" "lt" "lv" "ms" "nb" "ne" 
    "nl" "pa" "pl" "pt_BR" "pt" "ro" "ru" "sk" "sl" "sq" "sr" "sv" "th" 
    "tr" "ug" "uk" "vi"
)

# 添加快捷键翻译支持（不需要提供 target）
dde_shortcut_add_translations(
    APP_ID "deepin-myapp"
    CONFIG_DIR "${CMAKE_SOURCE_DIR}/configs"
    TS_DIR "${CMAKE_SOURCE_DIR}/translations"
    LANGUAGES ${I18N_LANGUAGES}
)
```

**参数说明：**

- `APP_ID`: 应用程序 ID，必须与配置文件中的 `appId` 一致
- `CONFIG_DIR`: 配置文件所在目录（包含 `*.shortcut.*` 子目录）
- `TS_DIR`: 翻译文件输出目录（可选，默认为 `./translations`）
- `LANGUAGES`: 支持的语言列表

**生成的 CMake Target：**

- `update_shortcut_i18n`: 固定名称的 target，用于更新快捷键翻译文件

#### 3.2.3 已有应用项目处理翻译

建议快捷键单独一个翻译文件夹，翻译内容不与已有的项目翻译混在一起，快捷键的翻译需要安装特定目录，这样可以避免快捷键服务查找翻译效率低，和影响应用本身的翻译。

#### 3.2.4 翻译文件构建工作流程

`dde_shortcut_add_translations` 函数会自动处理翻译文件的生成和编译。

**完整构建流程：**

```bash
# 1. 配置项目
cmake -B build

# 2. 构建项目（自动生成 .ts 和 .qm 文件）
cmake --build build

# 3. 更新翻译文件（可选，用于更新 .ts 文件内容）
cmake --build build --target update_shortcut_i18n
```

**工作原理：**

1. **配置阶段** (`cmake -B build`)：
   - Qt 的 `qt6_add_translations` 自动创建空的 `.ts` 文件
   - 设置翻译相关的构建规则
   - 创建 `update_shortcut_i18n` target

2. **构建阶段** (`cmake --build build`)：
   - 运行 `extract_shortcuts_i18n.py` 从 JSON 提取 `displayName` 和 `category`
   - 生成包含 `QT_TRANSLATE_NOOP` 的 C++ 文件
   - `lupdate` 自动更新 `.ts` 文件
   - `lrelease` 编译 `.ts` 为 `.qm` 文件
   - 安装 `.qm` 文件到指定目录

3. **手动更新翻译** (`update_shortcut_i18n` target)：
   - 当修改了 JSON 配置中的 `displayName` 或 `category` 后
   - 运行此 target 重新提取并更新 `.ts` 文件

**日常开发流程：**

```bash
# 修改 JSON 配置后，更新翻译
cmake --build build --target update_shortcut_i18n

# 使用 Qt Linguist 编辑翻译
linguist translations/deepin-myapp_zh_CN.ts

# 重新构建以编译 .qm 文件
cmake --build build
```

**注意事项：**

- `.ts` 文件会自动创建，建议提交到 Git
- `update_shortcut_i18n` 是固定名称，所有应用共用
- 翻译文件包含用户可见配置的 `displayName`、`category` 和服务预留的 `None` 文本，不包含应用其他文本

### 3.3 手动翻译流程

如果不使用 CMake 模块，可以手动处理翻译：

**步骤 1：提取翻译字符串**

```bash
extract_shortcuts_i18n \
    configs/ \
    shortcut_i18n_strings.cpp \
    deepin-myapp
```

**步骤 2：生成 .ts 文件**

```bash
lupdate shortcut_i18n_strings.cpp -ts translations/deepin-myapp_zh_CN.ts
```

**步骤 3：翻译**

使用 Qt Linguist 或文本编辑器编辑 `.ts` 文件。

**步骤 4：编译 .qm 文件**

```bash
lrelease translations/deepin-myapp_zh_CN.ts -qm translations/deepin-myapp_zh_CN.qm
```

---

## 4. 文件部署

### 4.1 配置文件安装路径

所有快捷键配置文件必须安装到：

```
/usr/share/dsg/configs/org.deepin.dde.keybinding/
```

所有注册文件ini必须安装到：

```
/usr/share/deepin/org.deepin.dde.keybinding/
```

### 4.2 部署示例

假设你的应用是 `deepin-myapp`，有两个快捷键。

#### 4.2.1 目录结构

```
/usr/share/dsg/configs/org.deepin.dde.keybinding/
├── deepin-myapp.shortcut.action1/
│   └── org.deepin.shortcut.json
├── deepin-myapp.shortcut.action2/
│   └── org.deepin.shortcut.json
├── deepin-myapp.gesture.swipe3up/
│   └── org.deepin.gesture.json

/usr/share/deepin/org.deepin.dde.keybinding/
└── deepin-myapp.ini

/usr/share/deepin/org.deepin.dde.keybinding/translations/deepin-myapp/
├── deepin-myapp_zh_CN.qm
└── deepin-myapp_de.qm
```



#### 4.2.2 注册文件内容

**deepin-myapp.ini：**

```ini
[Config]
SubPaths=deepin-myapp.shortcut.action1;deepin-myapp.shortcut.action2;deepin-myapp.gesture.swipe3up
```

#### 4.2.3 配置文件关系图

```
deepin-myapp.ini (注册文件)
    ├─ SubPath: deepin-myapp.shortcut.action1
    │   └─ 对应: .../deepin-myapp.shortcut.action1/org.deepin.shortcut.json
    │
    ├─ SubPath: deepin-myapp.shortcut.action2
    │   └─ 对应: .../deepin-myapp.shortcut.action2/org.deepin.shortcut.json
    │
    └─ SubPath: deepin-myapp.gesture.swipe3up
        └─ 对应: .../deepin-myapp.gesture.swipe3up/org.deepin.gesture.json
```



#### 4.2.4 CMake 完整示例

```cmake
cmake_minimum_required(VERSION 3.16)
project(deepin-myapp)

# 查找必要的包
find_package(Qt6 REQUIRED COMPONENTS Core LinguistTools)
find_package(Dtk6 REQUIRED COMPONENTS Core DConfig)
find_package(DdeShortcutI18n REQUIRED)

# 指定支持的语言
set(I18N_LANGUAGES 
    "zh_CN" "zh_HK" "zh_TW" "en_US" "de" "fr" "es" "ja" "ko"
)

# 添加快捷键翻译支持
dde_shortcut_add_translations(
    APP_ID "deepin-myapp"
    CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/configs"
    TS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/translations"
    LANGUAGES ${I18N_LANGUAGES}
)

# 获取快捷键和手势配置子目录（分开处理，因为文件名不同）
file(GLOB SHORTCUT_CONFIG_SUBDIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/deepin-myapp.shortcut.*"
)
file(GLOB GESTURE_CONFIG_SUBDIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/deepin-myapp.gesture.*"
)

# 安装键盘快捷键配置（org.deepin.shortcut.json）
foreach(SUBDIR_PATH ${SHORTCUT_CONFIG_SUBDIRS})
    dtk_add_config_meta_files(
        APPID "org.deepin.dde.keybinding"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/configs"
        FILES "${SUBDIR_PATH}/org.deepin.shortcut.json"
    )
endforeach()

# 安装手势配置（org.deepin.gesture.json）
foreach(SUBDIR_PATH ${GESTURE_CONFIG_SUBDIRS})
    dtk_add_config_meta_files(
        APPID "org.deepin.dde.keybinding"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/configs"
        FILES "${SUBDIR_PATH}/org.deepin.gesture.json"
    )
endforeach()

# 安装注册文件
install(FILES configs/deepin-myapp.ini 
        DESTINATION share/deepin/org.deepin.dde.keybinding)
```

**目录结构示例：**

```
deepin-myapp/
├── CMakeLists.txt
├── configs/
│   ├── deepin-myapp.ini
│   ├── deepin-myapp.shortcut.action1/
│   │   └── org.deepin.shortcut.json
│   ├── deepin-myapp.shortcut.action2/
│   │   └── org.deepin.shortcut.json
│   └── deepin-myapp.gesture.swipe3up/
│       └── org.deepin.gesture.json
└── translations/
    ├── deepin-myapp.ts
    ├── deepin-myapp_zh_CN.ts
    └── deepin-myapp_de.ts
```

#### 4.2.5 验证部署

```bash
# 1. 检查注册文件
cat /usr/share/deepin/org.deepin.dde.keybinding/deepin-myapp.ini

# 2. 检查 JSON 配置文件
ls -la /usr/share/dsg/configs/org.deepin.dde.keybinding/deepin-myapp.*/

# 3. 检查翻译文件（如果有）
ls -la /usr/share/deepin/org.deepin.dde.keybinding/translations/deepin-myapp/

# 4. 验证 JSON 格式
jq . /usr/share/dsg/configs/org.deepin.dde.keybinding/deepin-myapp.shortcut.action1/org.deepin.shortcut.json

# 5. 触发重载
dbus-send --session --type=method_call \
    --dest=org.deepin.dde.Keybinding1 \
    /org/deepin/dde/Keybinding1 \
    org.deepin.dde.Keybinding1.ReloadConfigs

# 6. 查询是否加载成功
dbus-send --session --print-reply \
    --dest=org.deepin.dde.Keybinding1 \
    /org/deepin/dde/Keybinding1 \
    org.deepin.dde.Keybinding1.ListShortcutsByApp \
    string:"deepin-myapp"
```

---

## 5. DBus 接口使用

### 5.1 监听快捷键激活信号

当用户触发快捷键时，快捷键服务会发出 `ShortcutActivated` 信号。

**C++ 示例（Qt）：**

```cpp
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDebug>

class ShortcutListener : public QObject
{
    Q_OBJECT
public:
    ShortcutListener(QObject *parent = nullptr) : QObject(parent)
    {
        // 连接到快捷键激活信号
        QDBusConnection::sessionBus().connect(
            "org.deepin.dde.Keybinding1",           // 服务名
            "/org/deepin/dde/Keybinding1",          // 对象路径
            "org.deepin.dde.Keybinding1",           // 接口名
            "ShortcutActivated",                     // 信号名
            this,
            SLOT(onShortcutActivated(QString, QStringList))
        );
    }

private slots:
    void onShortcutActivated(const QString &id, const QStringList &triggerValue)
    {
        qDebug() << "Shortcut activated:" << id;
        qDebug() << "Trigger value:" << triggerValue;
        
        // 根据 id 执行相应操作
        if (id == "deepin-myapp.shortcut.action1") {
            // 执行 action1
        }
    }
};
```

### 5.2 查询快捷键信息

**列出所有快捷键：**

```cpp
QDBusInterface iface("org.deepin.dde.Keybinding1",
                     "/org/deepin/dde/Keybinding1",
                     "org.deepin.dde.Keybinding1",
                     QDBusConnection::sessionBus());

QDBusReply<QList<ShortcutInfo>> reply = iface.call("ListAllShortcuts");
if (reply.isValid()) {
    for (const auto &info : reply.value()) {
        qDebug() << "ID:" << info.id;
        qDebug() << "Name:" << info.displayName;
        qDebug() << "Hotkeys:" << info.hotkeys;
    }
}
```

**查询指定应用的快捷键：**

```cpp
QDBusReply<QList<ShortcutInfo>> reply = iface.call("ListShortcutsByApp", "deepin-myapp");
```

**检查快捷键冲突：**

```cpp
QDBusReply<ShortcutInfo> reply = iface.call("LookupConflictShortcut", "Ctrl+Alt+T");
if (reply.isValid() && !reply.value().id.isEmpty()) {
    qDebug() << "Conflict with:" << reply.value().displayName;
    qDebug() << "Replaceable:" << reply.value().modifiable;
}
```

`LookupConflictShortcut` 返回的 `ShortcutInfo.modifiable` 表示冲突项是否允许修改。
调用方只有在该值为 `true` 时才应提供“替换”操作；对于不可修改的系统快捷键，
应只展示冲突提示。

### 5.3 动态修改快捷键

**修改按键组合：**

```cpp
QStringList newHotkeys = {"Ctrl+Shift+T"};
QDBusReply<bool> reply = iface.call("ModifyHotkeys", 
                                     "deepin-myapp.shortcut.action1", 
                                     newHotkeys);
if (reply.isValid() && reply.value()) {
    qDebug() << "Hotkey modified successfully";
}
```

**禁用快捷键：**

```cpp
QDBusReply<bool> reply = iface.call("Disable", "deepin-myapp.shortcut.action1");
```

**重置所有快捷键：**

```cpp
iface.call("Reset");
```

### 5.4 主要接口

| 用途 | 方法 |
|---|---|
| 查询 | `ListAllShortcuts`、`ListShortcutsByApp`、`ListShortcutsByCategory`、`GetShortcut`、`SearchShortcuts` |
| 分类元数据 | `ListCategories` |
| 修改 | `ModifyHotkeys`、`Disable`、`SwapHotkeys`、`ReplaceHotkey`、`Reset` |
| 自定义快捷键 | `AddCustomShortcut`、`ModifyCustomShortcut`、`DeleteCustomShortcut` |
| 录入 | `BeginCapture`、`EndCapture` 和 `KeyEvent` 信号 |
| 手势 | `ListAllGestures`、`ModifyGesture` |

`category` 是提供方拥有的自由字符串，不是固定整数枚举。客户端通过
`ListCategories` 获取分类的显示名、顺序和 `isCustom` 标记，不应硬编码分类键。
`GestureInfo.availableActions` 是当前后端动作能力的来源。

D-Bus 结构体必须在调用前注册 Qt 元类型。字段顺序和完整方法签名以
`src/core/keybindingmanager.h`、`src/core/gesturemanager.h` 为准。

## 6. 测试与调试

### 6.1 配置文件验证

**检查 JSON 格式：**

```bash
# 验证 JSON 语法
jq . configs/deepin-myapp.shortcut.action1/org.deepin.shortcut.json

# 检查必填字段
jq '.contents | keys' configs/deepin-myapp.shortcut.action1/org.deepin.shortcut.json
```

**验证字段值：**

```bash
# 检查 appId
jq '.contents.appId.value' org.deepin.shortcut.json

# 检查 hotkeys
jq '.contents.hotkeys.value' org.deepin.shortcut.json

# 检查 modifiable
jq '.contents.modifiable.value' org.deepin.shortcut.json
```

### 6.2 手动触发重载

安装配置文件后，手动触发快捷键服务重载：

```bash
dbus-send --session --type=method_call \
    --dest=org.deepin.dde.Keybinding1 \
    /org/deepin/dde/Keybinding1 \
    org.deepin.dde.Keybinding1.ReloadConfigs
```

### 6.3 查看日志

**查看快捷键服务日志：**

```bash
# shortcut 插件运行在 dde 分组进程中
journalctl --user -u deepin-service-group@dde.service -f
```

**启用调试日志：**

```bash
# 设置环境变量
export QT_LOGGING_RULES="*.debug=true"

# 重启服务
systemctl --user restart deepin-service-group@dde.service
```

### 6.4 常见问题排查

#### 问题 1：快捷键未生效

**检查步骤：**

1. 确认配置文件已安装到正确位置
   ```bash
   ls -la /usr/share/dsg/configs/org.deepin.dde.keybinding/deepin-myapp.*/
   ```

2. 确认注册文件存在
   ```bash
   cat /usr/share/deepin/org.deepin.dde.keybinding/deepin-myapp.ini
   ```

3. 手动触发重载
   ```bash
   dbus-send --session --type=method_call \
       --dest=org.deepin.dde.Keybinding1 \
       /org/deepin/dde/Keybinding1 \
       org.deepin.dde.Keybinding1.ReloadConfigs
   ```

4. 查询快捷键是否已注册
   ```bash
   dbus-send --session --print-reply \
       --dest=org.deepin.dde.Keybinding1 \
       /org/deepin/dde/Keybinding1 \
       org.deepin.dde.Keybinding1.ListShortcutsByApp \
       string:"deepin-myapp"
   ```

#### 问题 2：翻译未显示

**检查步骤：**

1. 确认 .qm 文件已安装
   ```bash
   ls -la /usr/share/deepin/org.deepin.dde.keybinding/translations/deepin-myapp/
   ```

2. 检查文件命名格式
   ```bash
   # 正确格式：<appId>_<lang>.qm
   # 例如：deepin-myapp_zh_CN.qm
   ```

3. 确认 modifiable 为 true
   ```bash
   jq '.contents.modifiable.value' org.deepin.shortcut.json
   ```

#### 问题 3：快捷键冲突

**检查冲突：**

```bash
dbus-send --session --print-reply \
    --dest=org.deepin.dde.Keybinding1 \
    /org/deepin/dde/Keybinding1 \
    org.deepin.dde.Keybinding1.LookupConflictShortcut \
    string:"Ctrl+Alt+T"
```

**解决方案：**

- 选择不同的按键组合
- 或在配置文件中提供多个备选按键（hotkeys 数组）

#### 问题 4：特殊按键不工作

**X11 环境：**

使用 `xev` 工具查看按键码：

```bash
xev | grep keycode
```

**Wayland 环境：**

使用 `evtest` 工具：

```bash
sudo evtest
# 选择输入设备，然后按下按键查看键码
```

**通用环境：**

监听底层按键事件：

```bash
# 监听按键事件
dbus-monitor --session "interface='org.deepin.dde.KeyEvent1'"
```

#### 问题 5：`modifiable=true` 但用户改键不持久化

**症状**: 控制中心快捷键页面显示该项可编辑（modifiable=true），用户修改后 UI 提示成功，但下次 reload（重启服务、再次 dpkg trigger、注销重登）又回到默认 hotkey。`Disable` 同理 —— 关闭后又自己亮起来。

**根因**: dconfig JSON schema 的 `hotkeys` / `enabled` 字段被声明为 `"permissions": "readonly"`。dde-services 在 `ModifyHotkeys` / `Disable` 内部会调 `DConfig::setValue` 持久化用户的修改，readonly 字段的 setValue 会被 dconfig daemon 拒绝/忽略，导致内存里改了，磁盘没改，重载后又回滚。

**反例（来自 dde-launchpad 早期适配）**:

```json
{
    "hotkeys": {
        "value": ["Meta"],
        "permissions": "readonly",   // ❌ 与 modifiable=true 矛盾
        ...
    },
    "enabled": {
        "value": true,
        "permissions": "readonly",   // ❌ 同上
        ...
    },
    "modifiable": {
        "value": true,
        "permissions": "readonly",   // ✅ 这个 readonly 是对的
        ...
    }
}
```

**正确写法**: 凡是允许 `modifiable=true` 的快捷键，`hotkeys` 和 `enabled` 字段必须 `"permissions": "readwrite"`。其余字段（`appId` / `displayName` / `triggerType` / `triggerValue` / `category` / `modifiable` 本身）保持 readonly 是合理的 —— 这些是应用方定义、用户不能改。

**判定规则**:

| modifiable | hotkeys.permissions | enabled.permissions | 含义 |
|---|---|---|---|
| `false` | `readonly` | `readonly` | 系统硬绑定，控制中心不展示 |
| `true` | `readwrite` | `readwrite` | 用户可改可关 ✅ |
| `true` | `readonly` | — | ❌ 反例，UI 改了不存盘 |

#### 问题 6：单 modifier 键（Meta / Ctrl 等）作为 hotkey 行为不确定

**说明**: X11 和 Treeland 后端都接受单 modifier 配置，但它与 `Meta+X` 等组合键
同时存在时依赖后端的按下/释放派发语义，应用不能只验证单按场景。

**适配建议**:
- 优先用 modifier + 非 modifier 实键的组合（如 `Ctrl+Alt+T`、`Meta+E`）
- 若必须用单 modifier 键唤起，需在 X11 和 Treeland 分别回归单按、组合键、
  长按重复和锁键状态，确认不会在组合键释放时误触发

#### 问题 7：dpkg trigger 触发的快捷键修改长时间不生效

**症状**: 安装/卸载带快捷键配置的包后,`ReloadConfigs` 被调,journal 里能看到 `Config added` / `Parsed KeyConfig`,但用户按对应快捷键长时间无响应,直到某次 D-Bus 调用（d-feet、控制中心打开等）介入后才突然生效。

**根因**: `TreelandShortcutWrapper::commit / commitDeferred` 调用底层 wayland commit 后没有 `wl_display_flush`,wayland 协议消息卡在 libwayland 客户端 outgoing 缓冲。dde-services 是非 GUI 进程,与 compositor 之间没有常规 wayland 往来,缓冲要等到 Qt event loop 因为其它原因（D-Bus 调用等）跑一轮才被 Qt wayland 平台插件附带 flush。这之前 Treeland 根本没收到 bind/commit,binding 自然不生效。

**修复（2026-05-25 已合入）**: `commit / commitDeferred` 调底层 commit 后通过 `QNativeInterface::QWaylandApplication::display()` 拿 `wl_display*` 并显式 `wl_display_flush`,确保协议消息立即送达。

**对应代码**: `src/backend/wayland/treelandshortcutwrapper.cpp`(`commit`、`commitDeferred`、`flushWaylandDisplay`)

### 6.5 调试工具

**列出所有快捷键：**

```bash
dbus-send --session --print-reply \
    --dest=org.deepin.dde.Keybinding1 \
    /org/deepin/dde/Keybinding1 \
    org.deepin.dde.Keybinding1.ListAllShortcuts
```

**监听快捷键激活：**

```bash
dbus-monitor --session \
    "type='signal',interface='org.deepin.dde.Keybinding1',member='ShortcutActivated'"
```

**查看 NumLock/CapsLock 状态：**

```bash
dbus-send --session --print-reply \
    --dest=org.deepin.dde.Keybinding1 \
    /org/deepin/dde/Keybinding1 \
    org.freedesktop.DBus.Properties.Get \
    string:"org.deepin.dde.Keybinding1" \
    string:"NumLockState"
```

---


## 7. 附录

### 7.1 按键名称参考

快捷键使用 Qt 6 `QKeySequence::PortableText` 名称，例如 `Ctrl`、`Alt`、
`Shift`、`Meta`、`F1`、`Volume Up` 和 `Media Next`。完整名称以
[Qt QKeySequence 文档](https://doc.qt.io/qt-6/qkeysequence.html)为准。

### 7.2 Linux 按键码参考

Qt 没有定义的硬件键可以使用十进制或十六进制 Linux input keycode。常见示例：

| 按键 | 十进制 | 宏 |
|---|---:|---|
| 电源键 | 116 | `KEY_POWER` |
| 亮度减/增 | 224 / 225 | `KEY_BRIGHTNESSDOWN` / `KEY_BRIGHTNESSUP` |
| 键盘背光切换 | 228 | `KEY_KBDILLUMTOGGLE` |
| 麦克风静音 | 248 | `KEY_MICMUTE` |
| 触摸板开关 | 530 | `KEY_TOUCHPAD_TOGGLE` |

完整定义见 `/usr/include/linux/input-event-codes.h`。X11 可用 `xev`，通用环境
可用 `evtest` 确认实际键码。

### 7.3 手势 Action ID

`triggerValue` 保存数字 action ID：

- `0` 表示禁用。
- `1-99` 与 Treeland shortcut 协议枚举保持一致。
- `100+` 是 dde-services 的 X11 扩展动作。

动作 ID、名称和后端支持状态的权威定义在
`src/core/triggeractioncatalog.h`、`src/core/triggeractioncatalog.cpp` 和
`src/core/gestureactioncatalog.cpp`。客户端应读取 `GestureInfo.availableActions`，
不要复制动作表或根据会话类型推断支持范围。

## 相关资源

- [Qt QKeySequence](https://doc.qt.io/qt-6/qkeysequence.html)
- [DConfig](https://github.com/linuxdeepin/dtkcore)
