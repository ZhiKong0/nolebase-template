# VS Code / Windsurf 单片机开发环境配置指南

> 本文档介绍如何使用 VS Code 或 Windsurf 替代 Keil 进行单片机开发，包括编辑器配置、编译、调试等完整方案。

---

## 一、Keil 功能分析

| Keil 功能 | 用途 | VS Code/Windsurf 替代方案 |
|-----------|------|---------------------------|
| 代码编辑 | 编写 C/汇编代码 | ✅ **完全替代** - VS Code 编辑体验更好 |
| 项目管理 | 管理文件、头文件路径 | ✅ **完全替代** - 通过配置文件管理 |
| 编译 | 调用编译器生成 HEX | ✅ **可替代** - 使用 Keil 命令行或开源工具链 |
| 调试 | 断点、单步、查看变量 | ⚠️ **部分替代** - 需要配置 GDB + OpenOCD |
| 烧录 | 下载程序到单片机 | ✅ **可替代** - 使用命令行工具 |
| 仿真 | 模拟单片机运行 | ❌ **无法替代** - 需用真机调试 |

**结论：** VS Code/Windsurf 完全可以作为主力编辑器，但调试功能需要额外配置。

---

## 二、VS Code 配置方案

### 2.1 必装插件

| 插件名称 | 功能 | 推荐度 |
|----------|------|--------|
| **C/C++** (Microsoft) | 语法高亮、代码补全、跳转定义 | ⭐⭐⭐⭐⭐ |
| **C/C++ Extension Pack** | 扩展工具包 | ⭐⭐⭐⭐⭐ |
| **CMake Tools** | 如果使用 CMake 构建 | ⭐⭐⭐⭐ |
| **ARM Assembly** | ARM 汇编语法支持 | ⭐⭐⭐⭐ |
| **Cortex-Debug** | ARM 调试支持（需要 J-Link/ST-Link） | ⭐⭐⭐⭐ |
| **Serial Monitor** | 串口监视器 | ⭐⭐⭐⭐ |
| **Hex Editor** | 查看 HEX 文件 | ⭐⭐⭐ |

**安装方法：**
```
1. 打开 VS Code
2. 点击左侧扩展图标（或按 Ctrl+Shift+X）
3. 搜索上述插件名称
4. 点击 Install 安装
```

### 2.2 项目配置文件

在项目根目录创建 `.vscode` 文件夹，包含以下配置文件：

#### 1. c_cpp_properties.json（IntelliSense 配置）

```json
{
    "configurations": [
        {
            "name": "STM32",
            "includePath": [
                "${workspaceFolder}/**",
                "${workspaceFolder}/Start",
                "${workspaceFolder}/System",
                "${workspaceFolder}/User",
                "${workspaceFolder}/Library",
                "${workspaceFolder}/Hardware",
                "${workspaceFolder}/Hardware/IIC"
            ],
            "defines": [
                "STM32F10X_MD",
                "USE_STDPERIPH_DRIVER"
            ],
            "compilerPath": "D:/keil/Keil-v5/ARM/ARMCC/bin/armcc.exe",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "gcc-arm"
        },
        {
            "name": "C51",
            "includePath": [
                "${workspaceFolder}/**"
            ],
            "defines": [],
            "compilerPath": "D:/keil/Keil-v5/C51/BIN/C51.exe",
            "cStandard": "c89",
            "intelliSenseMode": "msvc-x64"
        }
    ],
    "version": 4
}
```

#### 2. tasks.json（编译任务配置）

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Keil Build (ARM)",
            "type": "shell",
            "command": "D:/keil/Keil-v5/UV4/UV4.exe",
            "args": [
                "-b",
                "${workspaceFolder}/project.uvprojx",
                "-j0",
                "-t",
                "Target 1",
                "-o",
                "${workspaceFolder}/Objects/project.build_log.htm"
            ],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "presentation": {
                "echo": true,
                "reveal": "always",
                "focus": false,
                "panel": "shared"
            },
            "problemMatcher": {
                "pattern": {
                    "regexp": "^(.*):(\\d+):\\s*(warning|error):\\s*(.*)$",
                    "file": 1,
                    "line": 2,
                    "severity": 3,
                    "message": 4
                }
            }
        },
        {
            "label": "Keil Build (C51)",
            "type": "shell",
            "command": "D:/keil/Keil-v5/C51/UV4/UV4.exe",
            "args": [
                "-b",
                "${workspaceFolder}/project.uvproj",
                "-j0",
                "-t",
                "Target 1"
            ],
            "group": "build"
        },
        {
            "label": "Clean",
            "type": "shell",
            "command": "rm",
            "args": [
                "-rf",
                "${workspaceFolder}/Objects/*"
            ],
            "windows": {
                "command": "powershell",
                "args": ["-Command", "Remove-Item -Path '${workspaceFolder}/Objects/*' -Recurse -Force -ErrorAction SilentlyContinue"]
            }
        }
    ]
}
```

#### 3. launch.json（调试配置）

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Cortex Debug (ST-Link)",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "openocd",
            "executable": "${workspaceFolder}/Objects/project.axf",
            "svdFile": "${workspaceFolder}/STM32F103.svd",
            "configFiles": [
                "interface/stlink.cfg",
                "target/stm32f1x.cfg"
            ],
            "runToEntryPoint": "main",
            "preLaunchTask": "Keil Build (ARM)"
        },
        {
            "name": "Cortex Debug (J-Link)",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "jlink",
            "device": "STM32F103C8",
            "interface": "swd",
            "executable": "${workspaceFolder}/Objects/project.axf",
            "svdFile": "${workspaceFolder}/STM32F103.svd",
            "runToEntryPoint": "main",
            "preLaunchTask": "Keil Build (ARM)"
        }
    ]
}
```

#### 4. settings.json（编辑器设置）

```json
{
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "C_Cpp.intelliSenseCacheSize": 5120,
    "C_Cpp.intelliSenseMemoryLimit": 4096,
    "files.associations": {
        "*.h": "c",
        "*.c": "c",
        "*.s": "armasm",
        "*.S": "armasm"
    },
    "editor.formatOnSave": true,
    "C_Cpp.formatting": "clangFormat",
    "[c]": {
        "editor.defaultFormatter": "ms-vscode.cpptools",
        "editor.tabSize": 4,
        "editor.insertSpaces": false
    },
    "terminal.integrated.defaultProfile.windows": "PowerShell",
    "serialport.monitor.baudRate": 115200,
    "serialport.monitor.lineEnding": "\n"
}
```

---

## 三、Windsurf 配置方案

Windsurf 基于 VS Code，配置方式基本相同，但有以下优势：

### 3.1 Windsurf 特有优势

| 功能 | 说明 |
|------|------|
| **AI 代码补全** | 比传统 IntelliSense 更智能的代码预测 |
| **自然语言生成代码** | 用中文/英文描述功能，自动生成代码框架 |
| **代码解释** | 选中代码后让 AI 解释功能 |
| **错误诊断** | AI 自动分析编译错误并提供修复建议 |

### 3.2 Windsurf 配置步骤

1. **打开 Windsurf**，选择项目文件夹
2. **安装 C/C++ 插件**（与 VS Code 相同）
3. **复制上述 .vscode 配置文件**到项目目录
4. **使用 Cascade 面板进行 AI 辅助编程**

### 3.3 Windsurf 单片机开发工作流

```
1. 在 Cascade 中描述需求：
   "帮我写一个 STM32 的 I2C 初始化函数，使用 PB6/PB7"

2. Cascade 生成代码后，点击 Apply Changes

3. 按 Ctrl+Shift+B 编译（调用 Keil）

4. 在 Terminal 中使用命令行烧录

5. 如有错误，将错误信息粘贴到 Cascade 让 AI 分析
```

---

## 四、调试功能替代方案

### 4.1 方案对比

| 调试需求 | Keil 方式 | VS Code/Windsurf 替代方案 | 复杂度 |
|----------|-----------|---------------------------|--------|
| 断点调试 | 硬件断点 | OpenOCD + GDB + Cortex-Debug | ⭐⭐⭐ |
| 查看变量 | Watch 窗口 | GDB 监视窗口 | ⭐⭐⭐ |
| 寄存器查看 | 寄存器窗口 | SVD 文件 + 调试视图 | ⭐⭐ |
| 单步执行 | Step Over/Into | GDB 命令 | ⭐⭐⭐ |
| 内存查看 | Memory 窗口 | Hex Editor 或 GDB | ⭐⭐ |
| 日志输出 | 串口打印 | 串口监视器插件 | ⭐ |

### 4.2 详细配置：OpenOCD 调试（推荐用于 STM32）

#### 步骤 1：安装 OpenOCD

```bash
# Windows (使用 MSYS2 或下载预编译版本)
pacman -S mingw-w64-x86_64-openocd

# 或下载预编译版本：
# https://github.com/openocd-org/openocd/releases
```

#### 步骤 2：配置 OpenOCD

创建 `openocd.cfg` 文件：

```tcl
# ST-Link 调试器 + STM32F103
interface stlink
transport select hla_swd
source [find target/stm32f1x.cfg]

# 或者 J-Link
# interface jlink
# transport select swd
# source [find target/stm32f1x.cfg]
```

#### 步骤 3：启动 OpenOCD 服务器

```bash
openocd -f openocd.cfg
```

#### 步骤 4：VS Code 启动调试

按 F5 启动调试，VS Code 会自动连接 OpenOCD。

### 4.3 简化方案：串口日志调试（推荐用于 C51）

对于 C51 等不支持硬件调试的单片机，使用串口打印是最实用的方案。

#### 1. 代码中添加日志输出

```c
// debug.h
#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdio.h>

// 定义日志级别
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3

#define CURRENT_LOG_LEVEL LOG_LEVEL_DEBUG

// 串口发送函数（用户实现）
extern void UART_SendChar(char c);

// 重定向 printf 到串口
#define putchar(c) UART_SendChar(c)

// 日志宏
#define LOG_DEBUG(fmt, ...) if (CURRENT_LOG_LEVEL <= LOG_LEVEL_DEBUG) printf("[D] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  if (CURRENT_LOG_LEVEL <= LOG_LEVEL_INFO)  printf("[I] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  if (CURRENT_LOG_LEVEL <= LOG_LEVEL_WARN)  printf("[W] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) if (CURRENT_LOG_LEVEL <= LOG_LEVEL_ERROR) printf("[E] " fmt "\r\n", ##__VA_ARGS__)

// 变量打印宏
#define LOG_VAR(name, value) LOG_DEBUG("%s = %d (0x%X)", #name, value, value)

#endif
```

#### 2. 使用 VS Code 串口监视器

安装 **Serial Monitor** 插件后：

```
1. 按 Ctrl+Shift+P
2. 输入 "Serial Monitor: Open"
3. 选择串口号和波特率（如 COM6, 115200）
4. 实时查看日志输出
```

### 4.4 进阶方案：逻辑分析仪

对于时序调试，逻辑分析仪是比 Keil 仿真更直观的工具。

| 工具 | 价格 | 用途 |
|------|------|------|
| Saleae Logic | $500+ | 专业级，8通道，软件强大 |
| DSLogic | $100+ | 国产，性价比高 |
| 普通 8通道 LA | $20-50 | 基础分析，够用 |

**使用场景：**
- I2C/SPI 时序分析
- PWM 波形验证
- 中断响应时间测量
- 多任务调度分析

---

## 五、完整工作流示例

### 场景：STM32 I2C 项目开发

```
┌─────────────────────────────────────────────────────────────┐
│  1. 在 Windsurf 中编写代码                                    │
│     - 使用 Cascade 生成 I2C 初始化代码                        │
│     - 手动编写业务逻辑                                        │
│     - 使用 C/C++ 插件的跳转功能查看库函数定义                  │
├─────────────────────────────────────────────────────────────┤
│  2. 编译                                                      │
│     - Ctrl+Shift+B 调用 Keil 编译                             │
│     - 在 Terminal 查看编译结果                                  │
│     - 如有错误，让 Cascade 分析并修复                         │
├─────────────────────────────────────────────────────────────┤
│  3. 烧录                                                      │
│     - Terminal 中运行：stcgal -P stm32 -p COM6 project.hex    │
│     - 或使用 ST-Link + STM32CubeProgrammer                      │
├─────────────────────────────────────────────────────────────┤
│  4. 调试（三选一）                                            │
│     方案 A: 串口日志                                          │
│       - 代码中插入 LOG_DEBUG("i2c_status = %d", status);      │
│       - Serial Monitor 查看输出                                │
│                                                            │
│     方案 B: OpenOCD + GDB (如果配置好)                        │
│       - F5 启动调试                                           │
│       - 设置断点，单步执行                                    │
│                                                            │
│     方案 C: 逻辑分析仪                                        │
│       - 连接 SDA/SCL 到 LA                                    │
│       - 查看 I2C 时序是否正确                                 │
└─────────────────────────────────────────────────────────────┘
```

---

## 六、各平台详细配置

### 6.1 STM32 (ARM Cortex-M)

**推荐工具链：**
- 编辑器：VS Code / Windsurf
- 编译器：Keil ARMCC / GCC ARM Embedded
- 调试器：OpenOCD + ST-Link / J-Link
- 烧录：ST-Link Utility / STM32CubeProgrammer

**必须安装：**
```bash
# OpenOCD (Windows)
# 下载地址：https://github.com/openocd-org/openocd/releases

# 添加到系统 PATH 后验证
openocd --version

# GDB ARM
# 包含在 GNU Arm Embedded Toolchain 中
# 下载地址：https://developer.arm.com/downloads/-/gnu-rm
```

### 6.2 C51 (8051)

**限制说明：**
- C51 单片机通常**不支持硬件调试**（没有 SWD/JTAG 接口）
- 只能使用串口日志 + 逻辑分析仪调试

**推荐配置：**
- 编辑器：VS Code / Windsurf
- 编译器：Keil C51（必须使用，无开源替代）
- 烧录：stcgal / 厂家专用工具
- 调试：串口日志 + LED 指示

### 6.3 ESP32

**推荐工具链：**
- 编辑器：VS Code / Windsurf
- 编译器：ESP-IDF (基于 GCC)
- 调试器：OpenOCD + ESP-Prog
- 烧录：esptool.py

**VS Code 插件：**
- Espressif IDF
- 自动包含所有工具链

---

## 七、常见问题

### Q: VS Code 找不到头文件

**A:** 检查 `c_cpp_properties.json` 中的 `includePath` 是否包含所有头文件目录。

### Q: 编译任务失败

**A:** 
1. 检查 Keil 路径是否正确
2. 确认 `project.uvprojx` 存在
3. 在 Terminal 中手动运行命令查看错误

### Q: 调试时提示 "Unable to connect to OpenOCD"

**A:**
1. 确认 OpenOCD 已启动（手动运行 `openocd -f interface/stlink.cfg -f target/stm32f1x.cfg`）
2. 检查 ST-Link 驱动是否安装
3. 确认单片机供电正常

### Q: Windsurf 的 Cascade 不了解我的项目结构

**A:** 在对话开始时提供上下文：
```
"这是我的 STM32F103 项目，使用标准库，项目结构如下：
- User/main.c: 主程序
- Hardware/IIC: I2C驱动
- System/Delay: 延时函数

请帮我..."
```

---

## 八、总结推荐

| 场景 | 推荐方案 |
|------|----------|
| **主力编辑器** | Windsurf（AI 辅助编程） |
| **STM32 编译** | Keil 命令行（保持兼容性）或 GCC |
| **STM32 调试** | OpenOCD + GDB + 串口日志 |
| **C51 编译** | Keil 命令行（必须使用） |
| **C51 调试** | 串口日志 + 逻辑分析仪 |
| **烧录** | 命令行工具（stcgal、STM32CubeProgrammer） |

**最佳实践：**
1. 用 Windsurf 写代码（享受 AI 辅助）
2. 用 Keil 命令行编译（确保兼容性）
3. 用串口日志调试（简单有效）
4. 复杂问题用逻辑分析仪（直观高效）
