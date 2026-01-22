// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

# Deepin 快捷键服务 - 应用适配开发者指南

> 版本: 1.0  
> 更新日期: 2025-01-20

## 目录

- [1. 快速开始](#1-快速开始)
- [2. 配置文件编写](#2-配置文件编写)
- [3. 国际化翻译](#3-国际化翻译)
- [4. 文件部署](#4-文件部署)
- [5. Debian 打包集成](#5-debian-打包集成)
- [6. DBus 接口使用](#6-dbus-接口使用)
- [7. 测试与调试](#7-测试与调试)
- [8. 最佳实践](#8-最佳实践)
- [9. 完整示例项目](#9-完整示例项目)
- [10. 附录](#10-附录)

---

## 1. 快速开始

### 1.1 概述

Deepin 快捷键服务是一个基于 DConfig 的动态快捷键管理框架，支持：

- ✅ X11 和 Wayland 双平台
- ✅ 键盘快捷键和手势快捷键（Wayland）
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

## 2. 配置文件编写

快捷键服务使用两种配置文件协同工作：

- **JSON 配置文件**：定义每个快捷键的具体行为（使用 DConfig 格式）
- **INI 注册文件**：告诉快捷键服务去哪里找 JSON 配置文件

### 2.1 JSON 配置文件（快捷键和手势）

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
| `hotkeys` | array[string] | ✅ | 按键组合列表，如 ["Ctrl+Alt+T"]，支持多个不同按键组合触发一个操作 |
| `triggerType` | int | ✅ | 触发类型：1=命令, 2=应用, 3=动作 |
| `triggerValue` | array[string] | ✅ | 触发值，根据 triggerType 不同而不同 |
| `category` | int | ✅ | 分类：1=系统, 2=应用, 3=自定义 |
| `enabled` | bool | ✅ | 是否启用，默认 true |
| `modifiable` | bool | ✅ | 用户是否可修改快捷键的hotKeys |
| `keyEventFlags` | int | ❌ | 触发时机标志位，默认 2（释放时） |

**hotkeys 字段说明：**

支持两种格式：

1. **Qt 标准按键名称**（按键快捷键，必须使用Qt定义的标准按键名称）
   - 示例：`"Meta+D"`, `"Ctrl+Alt+T"`, `"Volume Mute"`
   - 参考：[Qt QKeySequence 文档](https://doc.qt.io/qt-6/qkeysequence.html)

2. **Linux 按键码**（用于特殊按键，Qt未有定义的按键或者适配厂商特殊按键）
   - 10进制：`"530"` (触摸板开关)
   - 16进制：`"0x212"` (触摸板开关)
   - 常用按键码见 [附录 10.2](#102-linux-按键码对照表)

**triggerType 和 triggerValue：**

| triggerType | 说明 | triggerValue 格式 | 示例 |
|-------------|------|-------------------|------|
| 1 (Command) | 执行命令 | `["/path/to/exe", "arg1", "arg2"]` | `["/usr/bin/deepin-editor"]` |
| 2 (App) | 启动应用 | `["appId"]` | `["deepin-editor"]` |
| 3 (Action) | Compositor 动作 | `["actionId"]` | `["18"]` (锁屏) |

**keyEventFlags 标志位：**

| 值 | 说明 | 适用场景 |
|----|------|----------|
| 1 (0x1) | 按下时触发，key_press | 快速响应场景 |
| 2 (0x2) | 释放时触发（默认），key_release | 常规快捷键 |
| 4 (0x4) | 重复时触发（长按），repeat | 音量、亮度调节 |
| 5 (0x5) | 按下+重复，key_press \| repeat | repeat连续触发场景 |

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
            "permissions": "readonly",
            "visibility": "private"
        },
        "displayName": {
            "value": "terminal",
            "serial": 0,
            "flags": [],
            "name": "displayName",
            "name[zh_CN]": "终端",
            "description": "打开系统默认终端程序",
            "permissions": "readonly",
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
          "permissions": "readonly",
          "visibility": "private"
        },
        "triggerType": {
            "value": 1,
            "serial": 0,
            "flags": [],
            "name": "triggerType",
            "name[zh_CN]": "快捷键触发动作类型",
            "description": "二进制command类型",
            "permissions": "readonly",
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
            "permissions": "readonly",
            "visibility": "private"
        },
        "category": {
            "value": 1,
            "serial": 0,
            "flags": [],
            "name": "category",
            "name[zh_CN]": "快捷键类别,1:系统",
            "description": "系统快捷键",
            "permissions": "readonly",
            "visibility": "private"
        },
        "enabled": {
            "value": true,
            "serial": 0,
            "flags": [],
            "name": "enabled",
            "name[zh_CN]": "使能",
            "description": "是否启用快捷键",
            "permissions": "readonly",
            "visibility": "private"
        },
        "modifiable": {
            "value": true,
            "serial": 0,
            "flags": [],
            "name": "modifiable",
            "name[zh_CN]": "能否修改快捷键",
            "description": "不能修改快捷键",
            "permissions": "readonly",
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
      "value": ["/usr/lib/deepin-daemon/dde-shortcut-tool", "touchpad", "toggle"]
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
      "value": ["/usr/lib/deepin-daemon/dde-shortcut-tool", "audio", "up"]
    },
  	...  
  }
}
```


#### 2.1.4 手势快捷键配置（Wayland）

手势快捷键仅在 Wayland 环境下生效。

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
      "value": ["16"],
      "serial": 0,
      "flags": [],
      "name": "triggerValue",
      "description": "16 = toggle_multitask_view action",
      "permissions": "readwrite",
      "visibility": "private"
    },
    "category": {
      "value": 1,
      "serial": 0,
      "flags": [],
      "name": "category",
      "permissions": "readwrite",
      "visibility": "private"
    },
    "enabled": {
      "value": true,
      "serial": 0,
      "flags": [],
      "name": "enabled",
      "permissions": "readonly",
      "visibility": "private"
    },
    "modifiable": {
      "value": false,
      "serial": 0,
      "flags": [],
      "name": "modifiable",
      "permissions": "readonly",
      "visibility": "private"
    }
  }
}
```

#### 2.1.5 配置文件验证

在部署前，建议验证 JSON 格式：

```bash
# 使用 jq 验证 JSON 格式
jq . org.deepin.shortcut.json

# 检查必填字段
jq '.contents | keys' org.deepin.shortcut.json
```

**必填字段检查清单：**

- ✅ appId
- ✅ displayName
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

**示例 2：混合键盘和手势（Wayland）**

```ini
[Config]
SubPaths=deepin-myapp.shortcut.action1;deepin-myapp.shortcut.action2;deepin-myapp.gesture.swipe3up
```

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
/usr/share/org.deepin.dde.keybinding/translations/<appId>/<appId>_<lang>.qm
```

**示例：**

```
/usr/share/org.deepin.dde.keybinding/translations/deepin-editor/deepin-editor_zh_CN.qm
/usr/share/org.deepin.dde.keybinding/translations/deepin-editor/deepin-editor_de.qm
```

### 3.2 使用 CMake 集成工具

快捷键服务提供了 `DdeShortcutI18n` CMake 模块，可以自动化处理翻译流程。

#### 3.2.1 DdeShortcutI18n 模块

该模块提供 `dde_shortcut_add_translations` 函数，自动完成：

1. 从 JSON 配置文件提取 `displayName` 字段
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
   - 运行 `extract_shortcuts_i18n.py` 从 JSON 提取 `displayName`
   - 生成包含 `QT_TRANSLATE_NOOP` 的 C++ 文件
   - `lupdate` 自动更新 `.ts` 文件
   - `lrelease` 编译 `.ts` 为 `.qm` 文件
   - 安装 `.qm` 文件到指定目录

3. **手动更新翻译** (`update_shortcut_i18n` target)：
   - 当修改了 JSON 配置中的 `displayName` 后
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
- 翻译文件只包含快捷键的 `displayName`，不包含应用其他文本

### 3.3 手动翻译流程

如果不使用 CMake 模块，可以手动处理翻译：

**步骤 1：提取翻译字符串**

```bash
python3 /usr/share/dde-shortcut-manager/tools/extract_shortcuts_i18n.py \
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

/usr/share/deepin/org.deepin.dde.keybinding/
└── deepin-myapp.ini

/usr/share/org.deepin.dde.keybinding/translations/deepin-myapp/
├── deepin-myapp_zh_CN.qm
└── deepin-myapp_de.qm
```



#### 4.2.2 注册文件内容

**deepin-myapp.ini：**

```ini
[Config]
SubPaths=deepin-myapp.shortcut.action1;deepin-myapp.shortcut.action2
```

#### 4.2.3 配置文件关系图

```
deepin-myapp.ini (注册文件)
    ├─ SubPath: deepin-myapp.shortcut.action1
    │   └─ 对应: /usr/share/dsg/configs/org.deepin.dde.keybinding/
    │             deepin-myapp.shortcut.action1/org.deepin.shortcut.json
    │
    └─ SubPath: deepin-myapp.shortcut.action2
        └─ 对应: /usr/share/dsg/configs/org.deepin.dde.keybinding/
                  deepin-myapp.shortcut.action2/org.deepin.shortcut.json
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

# 获取所有快捷键和手势配置子目录
file(GLOB CONFIG_SUBDIRS 
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/deepin-myapp.shortcut.*"
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/deepin-myapp.gesture.*"
)

# 为每个子目录安装其 JSON 配置文件
foreach(SUBDIR_PATH ${CONFIG_SUBDIRS})
    # 提取子目录名称
    get_filename_component(SUBDIR_NAME ${SUBDIR_PATH} NAME)
    
    dtk_add_config_meta_files(
        APPID "org.deepin.dde.keybinding"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/configs"
        FILES "${SUBDIR_PATH}/org.deepin.shortcut.json"
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
│   └── deepin-myapp.shortcut.action2/
│       └── org.deepin.shortcut.json
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
ls -la /usr/share/org.deepin.dde.keybinding/translations/deepin-myapp/

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
}
```

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

### 5.4 代码示例

**完整的快捷键管理类：**

```cpp
#include <QObject>
#include <QDBusInterface>
#include <QDBusConnection>
#include <QDBusReply>

class ShortcutManager : public QObject
{
    Q_OBJECT
public:
    explicit ShortcutManager(QObject *parent = nullptr)
        : QObject(parent)
        , m_interface(new QDBusInterface("org.deepin.dde.Keybinding1",
                                         "/org/deepin/dde/Keybinding1",
                                         "org.deepin.dde.Keybinding1",
                                         QDBusConnection::sessionBus(),
                                         this))
    {
        // 连接信号
        QDBusConnection::sessionBus().connect(
            "org.deepin.dde.Keybinding1",
            "/org/deepin/dde/Keybinding1",
            "org.deepin.dde.Keybinding1",
            "ShortcutActivated",
            this,
            SLOT(onShortcutActivated(QString, QStringList))
        );
        
        QDBusConnection::sessionBus().connect(
            "org.deepin.dde.Keybinding1",
            "/org/deepin/dde/Keybinding1",
            "org.deepin.dde.Keybinding1",
            "ShortcutChanged",
            this,
            SLOT(onShortcutChanged(QString))
        );
    }
    
    // 查询快捷键
    QStringList getHotkeys(const QString &shortcutId)
    {
        QDBusReply<ShortcutInfo> reply = m_interface->call("GetShortcut", shortcutId);
        if (reply.isValid()) {
            return reply.value().hotkeys;
        }
        return {};
    }
    
    // 修改快捷键
    bool modifyHotkeys(const QString &shortcutId, const QStringList &newHotkeys)
    {
        QDBusReply<bool> reply = m_interface->call("ModifyHotkeys", shortcutId, newHotkeys);
        return reply.isValid() && reply.value();
    }
    
    // 检查冲突
    QString checkConflict(const QString &hotkey)
    {
        QDBusReply<ShortcutInfo> reply = m_interface->call("LookupConflictShortcut", hotkey);
        if (reply.isValid() && !reply.value().id.isEmpty()) {
            return reply.value().displayName;
        }
        return QString();
    }

signals:
    void shortcutActivated(const QString &id);
    void shortcutChanged(const QString &id);

private slots:
    void onShortcutActivated(const QString &id, const QStringList &triggerValue)
    {
        Q_UNUSED(triggerValue)
        emit shortcutActivated(id);
    }
    
    void onShortcutChanged(const QString &id)
    {
        emit shortcutChanged(id);
    }

private:
    QDBusInterface *m_interface;
};
```

---

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
# 使用 journalctl
journalctl --user -u dde-shortcut-manager -f

# 或查看系统日志
tail -f ~/.cache/deepin/dde-shortcut-manager/dde-shortcut-manager.log
```

**启用调试日志：**

```bash
# 设置环境变量
export QT_LOGGING_RULES="*.debug=true"

# 重启服务
systemctl --user restart dde-shortcut-manager
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
   ls -la /usr/share/org.deepin.dde.keybinding/translations/deepin-myapp/
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

使用 `dde-shortcut-manager` 提供的测试工具：

```bash
# 监听按键事件
dbus-monitor --session "interface='org.deepin.dde.KeyEvent1'"
```

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

### 7.1 Qt 标准按键名称参考

**修饰键：**

| Qt 名称 | 说明 | 示例 |
|---------|------|------|
| `Ctrl` | Control 键 | `Ctrl+C` |
| `Alt` | Alt 键 | `Alt+F4` |
| `Shift` | Shift 键 | `Shift+Delete` |
| `Meta` | Meta/Super/Windows 键 | `Meta+D` |

**功能键：**

| Qt 名称 | 说明 |
|---------|------|
| `F1` - `F12` | 功能键 |
| `Esc` | Escape 键 |
| `Tab` | Tab 键 |
| `Backtab` | Shift+Tab |
| `Backspace` | 退格键 |
| `Return` | 回车键 |
| `Enter` | 小键盘回车 |
| `Insert` | Insert 键 |
| `Delete` | Delete 键 |
| `Pause` | Pause 键 |
| `Print` | Print Screen 键 |
| `SysReq` | SysRq 键 |
| `Home` | Home 键 |
| `End` | End 键 |
| `Left` | 左箭头 |
| `Up` | 上箭头 |
| `Right` | 右箭头 |
| `Down` | 下箭头 |
| `PageUp` | Page Up 键 |
| `PageDown` | Page Down 键 |

**多媒体键：**

| Qt 名称 | 说明 |
|---------|------|
| `Volume Up` | 音量增大 |
| `Volume Down` | 音量减小 |
| `Volume Mute` | 静音 |
| `Media Play` | 播放/暂停 |
| `Media Stop` | 停止 |
| `Media Previous` | 上一曲 |
| `Media Next` | 下一曲 |
| `Media Record` | 录制 |
| `Favorites` | 收藏 |
| `Search` | 搜索 |
| `Standby` | 待机 |
| `Open URL` | 打开 URL |
| `Launch Mail` | 启动邮件 |
| `Launch Media` | 启动媒体 |
| `Launch 0` - `Launch F` | 自定义启动键 |
| `Bass Boost` | 低音增强 |
| `Bass Up` | 低音增大 |
| `Bass Down` | 低音减小 |
| `Treble Up` | 高音增大 |
| `Treble Down` | 高音减小 |
| `Microphone Volume Up` | 麦克风音量增大 |
| `Microphone Volume Down` | 麦克风音量减小 |
| `Microphone Mute` | 麦克风静音 |

**特殊键：**

| Qt 名称 | 说明 |
|---------|------|
| `CapsLock` | 大写锁定 |
| `NumLock` | 数字锁定 |
| `ScrollLock` | 滚动锁定 |
| `Menu` | 菜单键 |
| `Help` | 帮助键 |
| `Back` | 后退 |
| `Forward` | 前进 |
| `Stop` | 停止 |
| `Refresh` | 刷新 |
| `Zoom In` | 放大 |
| `Zoom Out` | 缩小 |

**完整参考：**

https://doc.qt.io/qt-6/qkeysequence.html#toString

### 7.2 Linux 按键码对照表

**常用特殊按键码：**

| 按键名称 | 10进制 | 16进制 | 宏定义 |
|---------|--------|--------|--------|
| 触摸板开关 | 530 | 0x212 | KEY_TOUCHPAD_TOGGLE |
| 触摸板开 | 531 | 0x213 | KEY_TOUCHPAD_ON |
| 触摸板关 | 532 | 0x214 | KEY_TOUCHPAD_OFF |
| 电源键 | 116 | 0x74 | KEY_POWER |
| 睡眠 | 142 | 0x8e | KEY_SLEEP |
| 唤醒 | 143 | 0x8f | KEY_WAKEUP |
| 麦克风静音 | 248 | 0xf8 | KEY_MICMUTE |
| 飞行模式 | 247 | 0xf7 | KEY_RFKILL |
| 键盘背光切换 | 228 | 0xe4 | KEY_KBDILLUMTOGGLE |
| 键盘背光减 | 229 | 0xe5 | KEY_KBDILLUMDOWN |
| 键盘背光增 | 230 | 0xe6 | KEY_KBDILLUMUP |
| 显示切换 | 227 | 0xe3 | KEY_SWITCHVIDEOMODE |
| 亮度减 | 224 | 0xe0 | KEY_BRIGHTNESSDOWN |
| 亮度增 | 225 | 0xe1 | KEY_BRIGHTNESSUP |
| WLAN 开关 | 238 | 0xee | KEY_WLAN |
| 蓝牙开关 | 237 | 0xed | KEY_BLUETOOTH |

**查找按键码的方法：**

1. **使用 evtest 工具：**
   ```bash
   sudo apt install evtest
   sudo evtest
   # 选择输入设备，然后按下按键
   ```

2. **查看内核头文件：**
   ```bash
   grep -r "KEY_" /usr/include/linux/input-event-codes.h
   ```

3. **使用 xev（X11）：**
   ```bash
   xev | grep keycode
   ```

**完整参考：**

`/usr/include/linux/input-event-codes.h`

### 7.3 Treeland Action 枚举值

用于 Wayland 环境下的 Compositor 动作（triggerType = 3）。

| 值 | 名称 | 说明 |
|----|------|------|
| 1 | notify | 通知应用（发送信号） |
| 2 | workspace_1 | 切换到工作区 1 |
| 3 | workspace_2 | 切换到工作区 2 |
| 4 | workspace_3 | 切换到工作区 3 |
| 5 | workspace_4 | 切换到工作区 4 |
| 6 | workspace_5 | 切换到工作区 5 |
| 7 | workspace_6 | 切换到工作区 6 |
| 8 | prev_workspace | 上一个工作区 |
| 9 | next_workspace | 下一个工作区 |
| 10 | show_desktop | 显示桌面 |
| 11 | maximize | 最大化窗口 |
| 12 | cancel_maximize | 取消最大化 |
| 13 | move_window | 移动窗口 |
| 14 | close_window | 关闭窗口 |
| 15 | show_window_menu | 显示窗口菜单 |
| 16 | toggle_multitask_view | 切换多任务视图 |
| 17 | toggle_fps_display | 切换 FPS 显示 |
| 18 | lockscreen | 锁屏 |
| 19 | shutdown_menu | 关机菜单 |
| 20 | quit | 退出 |
| 21 | taskswitch_next | 下一个任务 |
| 22 | taskswitch_prev | 上一个任务 |
| 23 | taskswitch_quick_advance | 快速切换任务 |

**使用示例：**

```json
{
  "triggerType": {
    "value": 3
  },
  "triggerValue": {
    "value": ["18"]
  }
}
```

### 7.4 常见错误代码

| 错误 | 原因 | 解决方案 |
|------|------|----------|
| 配置文件未加载 | 注册文件不存在或格式错误 | 检查 `/usr/share/deepin/org.deepin.dde.keybinding/<appId>.ini` |
| 快捷键不生效 | 未触发重载 | 手动调用 `ReloadConfigs` DBus 方法 |
| 翻译未显示 | .qm 文件路径错误 | 确认文件在 `/usr/share/org.deepin.dde.keybinding/translations/<appId>/` |
| 快捷键冲突 | 与其他快捷键重复 | 使用 `LookupConflictShortcut` 检查冲突 |
| JSON 解析失败 | 格式错误 | 使用 `jq` 验证 JSON 格式 |
| modifiable 为 false 但需要翻译 | 配置错误 | 系统快捷键应设置 `modifiable: false`，不需要翻译 |
| SubPath 格式错误 | 命名不符合规范 | 使用 `<appId>.<type>.<name>` 格式，参见 [2.2.5 节](#225-subpath-命名规范) |

### 7.5 FAQ

**Q1: 我的应用需要多少个配置文件？**

A: 每个快捷键对应一个独立的配置文件（一个 SubPath）。如果你的应用有 3 个快捷键，就需要 3 个配置文件。

**Q2: 可以在运行时动态添加快捷键吗？**

A: 不建议。快捷键应该在安装时通过 DConfig 配置文件注册。如果需要临时快捷键，可以使用应用内的 QShortcut。

**Q3: 如何处理快捷键冲突？**

A: 
1. 在配置文件中提供多个备选按键（hotkeys 数组）
2. 设置 `modifiable: true`，让用户自定义
3. 在应用设置中提供冲突检测和修改界面

**Q4: X11 和 Wayland 需要不同的配置吗？**

A: 不需要。配置文件是统一的，快捷键服务会自动处理平台差异。手势快捷键仅在 Wayland 下生效。

**Q5: 如何测试快捷键是否正常工作？**

A:
```bash
# 1. 查询快捷键是否已注册
dbus-send --session --print-reply \
    --dest=org.deepin.dde.Keybinding1 \
    /org/deepin/dde/Keybinding1 \
    org.deepin.dde.Keybinding1.ListShortcutsByApp \
    string:"your-app-id"

# 2. 监听激活信号
dbus-monitor --session \
    "type='signal',interface='org.deepin.dde.Keybinding1',member='ShortcutActivated'"

# 3. 按下快捷键，查看是否有信号输出
```

**Q6: 翻译文件必须提供吗？**

A: 只有 `modifiable: true` 的快捷键需要提供翻译。系统级不可修改的快捷键（`modifiable: false`）不需要翻译。

**Q7: 如何支持多个按键组合？**

A: 在 `hotkeys` 字段中提供数组：
```json
"hotkeys": {
  "value": ["Ctrl+N", "Ctrl+Shift+N"]
}
```

**Q8: 可以使用自定义的按键名称吗？**

A: 不可以。必须使用 Qt 标准按键名称或 Linux 按键码。参见 [附录 10.1](#101-qt-标准按键名称参考) 和 [附录 10.2](#102-linux-按键码对照表)。

**Q9: 如何处理长按触发？**

A: 使用 `keyEventFlags` 字段：
```json
"keyEventFlags": {
  "value": 5
}
```
其中 5 = Press(1) + Repeat(4)，适用于音量、亮度等需要连续触发的场景。

**Q10: 卸载应用时快捷键会自动清理吗？**

A: 是的。当配置文件和注册文件被删除后，触发器会通知快捷键服务重载，快捷键会自动清理。

---

## 相关资源

- **架构设计文档**: `ARCHITECTURE_DESIGN_ZH.md`
- **快捷键服务仓库**: https://github.com/linuxdeepin/dde-shortcut-manager
- **DConfig 文档**: https://github.com/linuxdeepin/dtkcore
- **Qt QKeySequence 文档**: https://doc.qt.io/qt-6/qkeysequence.html
- **Linux Input Event Codes**: `/usr/include/linux/input-event-codes.h`

---

## 贡献与反馈

如果你在适配过程中遇到问题或有改进建议，欢迎：

- 提交 Issue: https://github.com/linuxdeepin/dde-shortcut-manager/issues
- 提交 Pull Request
- 联系维护者

---

**文档版本**: 1.0  
**最后更新**: 2025-01-20  
**维护者**: Deepin Team
