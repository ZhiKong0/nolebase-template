# Keil 工程编译与烧录操作指南（STM32/ARM + STC/C51）

---

## 一、路径变量定义（需根据实际项目修改）

**必需变量（编译）：**

- `{项目根目录}`: Keil 项目文件所在目录（包含 `project.uvprojx`/`project.uvproj` 与 `Objects\`）
- `{UV4_ARM路径}`: Keil ARM 的 `UV4.exe` 安装路径（示例：`D:\keil\Keil-v5\Arm\UV4\UV4.exe`）
- `{UV4_C51路径}`: Keil C51 的 `UV4.exe` 安装路径（示例：`D:\keil\Keil-v5\C51\UV4\UV4.exe`）
- `{Target名}`: Keil Target 名（通常是 `Target 1`）

**必需变量（烧录 - STM32）：**

- `{CubeProgCLI路径}`: STM32CubeProgrammer CLI 路径（示例：`E:\STMcubeProgrammer\programmer\bin\STM32_Programmer_CLI.exe`）
- `{HEX路径}`: 编译生成的 HEX 文件（通常是 `{项目根目录}\Objects\project.hex`）
- `{AXF路径}`: 编译生成的 AXF/ELF 文件（通常是 `{项目根目录}\Objects\project.axf`）
- `{pyOCD路径}`: pyOCD 可执行文件路径（适用于 DAPLink / CMSIS-DAP，示例：`C:\Users\DZ\AppData\Local\Programs\Python\Python313\Scripts\pyocd.exe`）
- `{pyOCD目标名}`: pyOCD 使用的目标芯片名（当前这台机器上，`STM32F103C8T6` 实测应使用 `stm32f103rc`）
- `{DAPLink盘符}`: 可选，DAPLink 挂载为U盘时的盘符（示例：`G:`）

**示例:**

```
项目根目录 = f:\Documents\GitHub\xxx\Project
UV4_ARM路径 = D:\keil\Keil-v5\Arm\UV4\UV4.exe
UV4_C51路径 = D:\keil\Keil-v5\C51\UV4\UV4.exe
Target名 = Target 1
CubeProgCLI路径 = E:\STMcubeProgrammer\programmer\bin\STM32_Programmer_CLI.exe
HEX路径 = {项目根目录}\Objects\project.hex
AXF路径 = {项目根目录}\Objects\project.axf
pyOCD路径 = C:\Users\DZ\AppData\Local\Programs\Python\Python313\Scripts\pyocd.exe
pyOCD目标名 = stm32f103rc
DAPLink盘符 = G:
```

---

## 1.1 本机可直接使用的配置（已验证路径）

> 适用工程：`Project_Refactor`（STM32F103C8 + Keil ARM + DAPLink/pyOCD + 可选 ST-LINK/CubeProgrammer）

```powershell
$PROJ_ROOT = "f:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project_Refactor"
$UV4_ARM   = "D:\keil\Keil-v5\Arm\UV4\UV4.exe"
$TARGET    = "Target 1"
$LOG       = "$PROJ_ROOT\Objects\project.build_log.htm"
$HEX       = "$PROJ_ROOT\Objects\project.hex"
$AXF       = "$PROJ_ROOT\Objects\project.axf"
$CUBE_CLI  = "E:\STMcubeProgrammer\programmer\bin\STM32_Programmer_CLI.exe"
$PYOCD     = "C:\Users\DZ\AppData\Local\Programs\Python\Python313\Scripts\pyocd.exe"
$PYOCD_TARGET_PRIMARY  = "stm32f103rc"
$PYOCD_TARGET_FALLBACK = ""
```

### 1.1.0 一键全过程（ST-LINK：编译 -> 检查 log -> 校验 HEX -> Flash + Reset）

```powershell
if (!(Test-Path $PROJ_ROOT)) { throw "PROJ_ROOT not found: $PROJ_ROOT" }
if (!(Test-Path $UV4_ARM))   { throw "UV4_ARM not found: $UV4_ARM" }
if (!(Test-Path $CUBE_CLI))  { throw "CUBE_CLI not found: $CUBE_CLI" }

& $UV4_ARM -b "$PROJ_ROOT\project.uvprojx" -j0 -t $TARGET -o $LOG
if ($LASTEXITCODE -ne 0) { throw "Keil build failed, exit code=$LASTEXITCODE" }
if (!(Test-Path $LOG)) { throw "build log not found: $LOG" }

$raw = Get-Content -Raw $LOG
if ($raw -notmatch '0 Error\(s\)') { throw "Build failed: not found 0 Error(s)" }
if ($raw -notmatch 'creating hex file') { throw "Build failed: hex not created" }

if (!(Test-Path $HEX)) { throw "HEX not found: $HEX" }
$hexItem = Get-Item $HEX
$srcLatest = Get-ChildItem "$PROJ_ROOT\User", "$PROJ_ROOT\Hardware", "$PROJ_ROOT\System" -Recurse -Include *.c,*.h |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($hexItem.LastWriteTime -le $srcLatest.LastWriteTime) { throw "HEX is NOT newer than sources" }

& $CUBE_CLI -c port=SWD freq=4000 mode=UR reset=HWrst -w $HEX -v -rst
if ($LASTEXITCODE -ne 0) { throw "Flash failed, exit code=$LASTEXITCODE" }
```

### 1.1.1 一键全过程（DAPLink：编译 -> 自动选择 HEX/AXF -> pyOCD 烧录）

```powershell
if (!(Test-Path $PROJ_ROOT)) { throw "PROJ_ROOT not found: $PROJ_ROOT" }
if (!(Test-Path $UV4_ARM))   { throw "UV4_ARM not found: $UV4_ARM" }
if (!(Test-Path $PYOCD))     { throw "pyOCD not found: $PYOCD" }

& $UV4_ARM -b "$PROJ_ROOT\project.uvprojx" -j0 -t $TARGET -o $LOG
if (!(Test-Path $LOG)) { throw "build log not found: $LOG" }

$raw = Get-Content -Raw $LOG
if ($raw -notmatch '0 Error\(s\)') { throw "Build failed: not found 0 Error(s)" }

$images = @()
if (($raw -match 'creating hex file') -and (Test-Path $HEX)) {
  $images += $HEX
}
if (Test-Path $AXF) {
  $images += $AXF
}
if ($images.Count -eq 0) {
  throw "Neither HEX nor AXF exists. Cannot flash."
}

& $PYOCD list --probes
if ($LASTEXITCODE -ne 0) { throw "pyOCD probe enumeration failed" }

$targetOk = $PYOCD_TARGET_PRIMARY
if ([string]::IsNullOrWhiteSpace($targetOk)) { throw "pyOCD target is empty" }
if (-not $targetOk) { throw "No usable pyOCD target found" }

$flashOk = $false
foreach ($image in $images) {
  Write-Host "TRY_IMAGE=$image"
  & $PYOCD erase --chip --no-config -t $targetOk -M under-reset -f 100000
  if ($LASTEXITCODE -ne 0) { continue }
  & $PYOCD load --no-config -t $targetOk -M under-reset -f 100000 -e sector $image
  if ($LASTEXITCODE -eq 0) {
    $flashOk = $true
    break
  }
}
if (-not $flashOk) { throw "pyOCD load failed for both HEX and AXF" }
```

### 1.1.2 经验结论（实测）

- **当前工程实测可用 target 名是 `stm32f103rc`**：虽然芯片丝印是 `STM32F103C8T6`，但这台机器上的 `pyOCD` 直接用 `stm32f103rc` 成功
- **`UV4` 命令返回码不一定可靠**：要以 `project.build_log.htm` 里是否出现 `0 Error(s)` 和 `creating hex file` 为准
- **DAPLink 探头能枚举，不代表目标板一定连通**：`Target = n/a` 或 `commander` 超时，通常还是供电 / SWD 接线 / 连接时机问题
- **`Target = n/a` 常见于目标板没上电或 SWD 线没接好**
- **`HEX` 没生成时不要卡住**：如果 `AXF` 是最新的，`pyOCD` 可以直接烧 `AXF`
- **`commander -c status` 适合排查，不是烧录成功的必要前置条件**：实测存在 `status` 超时但 `load --M under-reset` 仍能成功的情况
- **`skipped xxxx bytes` 通常不是失败**：表示板子里已经是相同固件，`pyOCD` 跳过了重复写入
- **如果 `pyocd flash` 在 `0x08000000` 报 `flash program page failure`，优先尝试“全片擦除后重烧”**：本机 2026-03-23 对 `Project_Refactor` 的实测中，普通 `flash` 连续两次都在首页写入失败，但执行 `erase --chip` 后再次 `flash` 成功

---

## 二、编译方法

### 2.1 STM32/ARM：使用 Keil 命令行编译（生成 HEX）

**模板命令（推荐使用 `.uvprojx`）：**

```bash
{UV4_ARM路径} -b "{项目根目录}\project.uvprojx" -j0 -t "{Target名}" -o "{项目根目录}\Objects\project.build_log.htm"
```

**实际示例:**

```bash
D:\keil\Keil-v5\Arm\UV4\UV4.exe -b "f:\Project\MyProject\project.uvprojx" -j0 -t "Target 1" -o "f:\Project\MyProject\Objects\project.build_log.htm"
```

**参数说明:**

- `-b`: 编译项目
- `-j0`: 输出详细信息
- `-t "Target 1"`: 指定目标
- `-o`: 输出编译日志到指定文件

---

### 2.2 编译结果查看

**编译日志文件:** `{项目根目录}\Objects\project.build_log.htm`

**注意: 一定要阅读 `project.build_log.htm` 再去烧录！！！**

**关键信息:**

- `0 Error(s)`: 编译成功
- `Program Size: data=xx.x xdata=0 code=xxxx`: 程序大小
- `creating hex file`: 成功生成 HEX 文件

**经验：build_log.htm 在某些环境下可能乱码/编码识别失败**

- 你可以用 PowerShell 直接提取关键字（比“打开 HTML”更稳）：

```powershell
$log = "{项目根目录}\Objects\project.build_log.htm"
Get-Content -Raw $log | Select-String -Pattern '0 Error\(s\)','creating hex file' -AllMatches
```

---

### 2.3 验证 HEX 文件已更新（重要）

**目的:** 确保烧录的是最新编译的代码，避免烧录旧版本

**验证步骤:**

1. **检查HEX文件修改时间**

```bash
# Windows PowerShell
Get-Item "{项目根目录}\Objects\project.hex" | Select-Object FullName, LastWriteTime
```

2. **检查源代码最新修改时间**

```bash
# Windows PowerShell
Get-ChildItem "{项目根目录}\User", "{项目根目录}\Hardware", "{项目根目录}\System" -Recurse -Include *.c,*.h | Sort-Object LastWriteTime -Descending | Select-Object -First 1 FullName, LastWriteTime
```

3. **确认时间关系**

- **HEX时间 > 所有源代码修改时间** → 验证通过
- **HEX时间 < 某个源代码修改时间** → 需要重新编译

**验证失败处理:**

- 如果HEX未更新或时间不对，**重新执行编译命令**
- 检查编译日志是否显示 `creating hex file`

---

## 三、查找 CH340 串口（仅串口通信/部分烧录场景需要）

### 3.1 使用Python查找串口

**命令:**

```bash
python -c "import serial.tools.list_ports as s; ports=s.comports(); [print(f'{p.device}: {p.description}') for p in ports]"
```

**查找CH340端口:**

```bash
python -c "import serial.tools.list_ports as s; ports=s.comports(); ch340=[p for p in ports if 'CH340' in p.description]; print(ch340[0].device if ch340 else 'CH340 not found')"
```

### 3.2 常见串口设备

- `COM6: USB-SERIAL CH340 (COM6)` - 常见USB转串口芯片

## 四、烧录方法

### 4.1 STM32：使用 STM32CubeProgrammer CLI（推荐，ST-LINK + SWD）

**适用芯片:** STM32（如 STM32F103）

**连接探测（可选，但推荐先做一次）：**

```powershell
& "{CubeProgCLI路径}" -c port=SWD freq=4000 mode=UR reset=HWrst -rdu
```

**烧录 + 校验 + 复位（推荐命令）：**

```powershell
& "{CubeProgCLI路径}" -c port=SWD freq=4000 mode=UR reset=HWrst -w "{HEX路径}" -v -rst
```

**参数经验值（来自实测稳定组合）：**

- `port=SWD`：使用 SWD
- `mode=UR`：Under Reset（最容易连上“问题板/跑飞板”）
- `reset=HWrst`：硬复位
- `freq=4000`：4MHz（不稳就降到 1000/2000）
- `-v`：烧录后校验（强烈建议保留）
- `-rst`：烧录后复位运行

**常见失败排查：**

- 现象 `No target found` / 连接不上：
  - 优先用 `mode=UR reset=HWrst`
  - 降低 `freq`（如 1000）
  - 确认 ST-LINK 驱动正常、线序正确（SWDIO/SWCLK/GND/3V3）
  - 目标板供电电压是否正常（CLI 输出里会显示 Voltage）
- 现象烧录成功但运行不对：
  - 回到“2.3 HEX 更新时间验证”，确认烧录的是最新 hex

---

### 4.1.1 STM32：使用 pyOCD（DAPLink / CMSIS-DAP，默认先全片擦除）

**适用场景：**

- 使用 DAPLink / CMSIS-DAP 探头
- 已安装 `pyocd`
- Keil 工程能生成 `hex` 或至少能生成 `axf`

**先探测探头：**

```powershell
& "{pyOCD路径}" list --probes
```

**实测现象说明：**

- 如果这里能看到 `CMSIS-DAP` 探头，说明探头驱动基本正常
- 如果 `Target` 一列显示 `n/a`，通常表示：
  - 目标板没上电
  - SWDIO / SWCLK / GND / 3V3 连接有问题
  - 还没正确指定目标芯片名

**连接测试（推荐先做一次）：**

```powershell
& "{pyOCD路径}" commander --no-config -t "{pyOCD目标名}" -c status -c exit
```

> 当前工程实测说明：`STM32F103C8T6` 在这台机器上的 `pyOCD` 环境里直接使用 `stm32f103rc` 更稳。

**默认流程：先全片擦除，再烧录 HEX：**

```powershell
& "{pyOCD路径}" erase --chip --no-config -t "{pyOCD目标名}" -M under-reset -f 100000
& "{pyOCD路径}" load --no-config -t "{pyOCD目标名}" -M under-reset -f 100000 -e sector "{HEX路径}"
```

**默认流程：先全片擦除，再烧录 AXF/ELF：**

```powershell
& "{pyOCD路径}" erase --chip --no-config -t "{pyOCD目标名}" -M under-reset -f 100000
& "{pyOCD路径}" load --no-config -t "{pyOCD目标名}" -M under-reset -f 100000 -e sector "{AXF路径}"
```

**参数说明（实测稳定组合）：**

- `--no-config`：忽略工程目录或用户目录里的 `pyocd.yaml`，减少配置干扰
- `-M under-reset`：目标程序跑飞时更容易连上
- `-f 100000`：当前这台机器 + DAPLink + 本工程实测成功值；如果稳定后再尝试升频
- `erase --chip`：**当前工程默认流程**，先做全片擦除，避免首页页写失败或旧内容残留
- `-e sector`：`load` 阶段仍保留按扇区写入，但前置流程默认已经做过全片擦除

**当前工程默认流程：先全片擦除，再重烧（本机 2026-03-24 起固定为默认）**

> 适用现象：`pyocd flash` 或 `pyocd load` 在首页附近失败，例如：`flash program page failure (address 0x08000000; result code 0x1)`

```powershell
& "{pyOCD路径}" erase --chip --no-config -t "{pyOCD目标名}" -M under-reset -f 100000
& "{pyOCD路径}" load --no-config -t "{pyOCD目标名}" -M under-reset -f 100000 -e sector "{HEX路径}"
```

**本次实测结论：**

- 目标名 `stm32f103c8` 在本机 `pyOCD 0.43.1` 环境中 **不被识别**
- 改用 `pyocd list --targets` 查到的 `stm32f103rc` 后，普通 `flash` 仍连续两次在 `0x08000000` 页写失败
- 执行 `erase --chip` 后，再用同一 target 和 `1MHz` 频率重烧成功
- 成功日志特征为：`Erased xxxx bytes ... programmed xxxx bytes ... skipped 0 bytes ...`

**实战经验：HEX 没生成时不要卡住，直接用 AXF/ELF 烧录**

- 有些工程虽然 `0 Error(s)`，但 build log 里没有 `creating hex file`
- 这时不要误判为“不能烧录”
- 只要 `Objects\project.axf` 是最新生成的，就可以直接用 `pyOCD` 烧录

**常见失败排查：**

- 现象 `Target type xxx not recognized`
  - 先执行：`pyocd list --targets`
  - 检查目标名是否写对
  - 如需安装支持包，可尝试：`pyocd pack install stm32f103c8`
  - 某些环境内置目标名不全，例如实测可见 `stm32f103rc` 但没有 `stm32f103c8`，这时直接改用 `stm32f103rc`
- 现象 `list --probes` 有探头，但 `commander` 连不上
  - 检查目标板供电
  - 检查 SWD 接线
  - 烧录时改用 `load --no-config -t stm32f103rc -M under-reset -f 100000 -e sector ...` 直接尝试
  - 降低 `-f` 频率
- 现象 `flash program page failure (address 0x08000000; result code 0x1)`
  - 先不要只重复同一条 `flash` 命令
  - 先给目标板断电上电，再试 1 次
  - 如果仍失败，优先执行 `erase --chip` 再重新 `flash`
  - 当前工程实测：`erase --chip` 之后可恢复正常烧录
- 现象 `pyocd load` 成功，但输出 `skipped xxxx bytes`
  - 这通常表示板子里已经是相同固件
  - 不是失败，而是 pyOCD 比对后跳过重复写入

---

### 4.2 STC/C51：使用串口烧录工具（示例：stcgal）

**适用芯片:** STC 系列单片机（STC15、STC32 等）

**前提条件:**

- 已安装stcgal: `pip install stcgal`

**烧录模板命令:**

```bash
stcgal -P {芯片型号} -p {串口号} "{项目根目录}\Objects\project.hex"
```

**实际示例:**

```bash
stcgal -P stc15 -p COM6 "f:\Project\MyProject\Objects\project.hex"
```

**参数说明:**

- `-P stc15`: 指定STC15系列单片机（根据实际芯片修改）
- `-p COM6`: 指定串口号（根据实际修改）
- 最后一个参数: HEX文件完整路径

**其他常见烧录工具:**

| 工具          | 适用芯片     | 示例命令                                                   |
| ------------- | ------------ | ---------------------------------------------------------- |
| stcgal        | STC系列      | `stcgal -P stc15 -p COM6 project.hex`                    |
| STM32 ST-Link | STM32        | 使用STM32CubeProgrammer或Keil内置烧录                      |
| J-Link        | ARM Cortex-M | `JLinkExe -device STM32F103C8 -if SWD -speed 4000`       |
| OpenOCD       | 多种ARM芯片  | `openocd -f interface/stlink.cfg -f target/stm32f1x.cfg` |

---

### 4.3 烧录流程（stcgal 示例）

1. **断开开发板电源**（拔USB线）
2. **等待2秒**
3. **重新上电**（插回USB线）
4. **等待stcgal自动识别并烧录**
5. **烧录成功提示:**

```bash
Setting options: done
Target UID: XXXXXXXXXXXX
Disconnected!
```

---

### 4.4 烧录失败排查（stcgal 示例）

- 检查串口号是否正确
- 确认开发板已断电再上电（冷启动）
- 检查USB线连接
- 尝试其他COM口
- **尝试多次烧录**（见4.4节）

---

### 4.5 烧录失败重试机制（重要）

**问题现象:**

- 烧录时显示 `Serial port error` 或 `Waiting for MCU` 超时
- 有时单次烧录会失败，需要多次尝试

**解决方法 - 多次重试:**

如果烧录失败，**请连续尝试烧录最多5次**，步骤如下：

| 次数          | 操作                                   |
| ------------- | -------------------------------------- |
| 第1次         | 执行烧录命令 → 断电上电 → 观察结果   |
| 第2次         | 如果失败，再次执行烧录命令 → 断电上电 |
| 第3次         | 如果失败，再次执行烧录命令 → 断电上电 |
| 第4次         | 如果失败，再次执行烧录命令 → 断电上电 |
| 第5次         | 如果失败，再次执行烧录命令 → 断电上电 |
| 第5次后仍失败 | 停止烧录，检查硬件连接或更换USB线      |

**注意事项:**

- 每次烧录前必须**断电再上电**（冷启动）
- 断电后等待2秒再上电
- 确保USB线连接牢固
- 如果5次都失败，可能是硬件问题

---

## 五、完整自动化流程

### 5.1 STM32（ARM）：PowerShell 一键编译 + CubeProgrammer 烧录

```powershell
# 变量（按实际修改）
$ProjRoot = "{项目根目录}"
$UV4 = "{UV4_ARM路径}"
$Target = "{Target名}"
$Log = Join-Path $ProjRoot "Objects\project.build_log.htm"
$Hex = "{HEX路径}"
$Cube = "{CubeProgCLI路径}"

# [1/4] 编译
& $UV4 -b (Join-Path $ProjRoot "project.uvprojx") -j0 -t $Target -o $Log

# [2/4] 检查 build log（关键字，避免HTML乱码无法打开）
$raw = Get-Content -Raw $Log
if ($raw -notmatch '0 Error\(s\)') { throw "Build failed: not found 0 Error(s)" }
if ($raw -notmatch 'creating hex file') { throw "Build failed: hex not created" }

# [3/4] 检查 HEX 更新时间是否新于源码
$hexItem = Get-Item $Hex
$src = Get-ChildItem "$ProjRoot\User","$ProjRoot\Hardware","$ProjRoot\System" -Recurse -Include *.c,*.h | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($hexItem.LastWriteTime -le $src.LastWriteTime) { throw "HEX is older than source. Rebuild." }

# [4/4] 烧录 + 校验 + 复位（SWD Under Reset + HWrst）
& $Cube -c port=SWD freq=4000 mode=UR reset=HWrst -w $Hex -v -rst
```

---

### 5.1.1 STM32（ARM）：PowerShell 一键编译 + pyOCD（DAPLink）烧录，默认全片擦除 + HEX/AXF 自动兜底

```powershell
# 变量（按实际修改）
$ProjRoot = "{项目根目录}"
$UV4 = "{UV4_ARM路径}"
$Target = "{Target名}"
$Log = Join-Path $ProjRoot "Objects\project.build_log.htm"
$Hex = "{HEX路径}"
$Axf = "{AXF路径}"
$Pyocd = "{pyOCD路径}"
$PyocdTarget = "{pyOCD目标名}"
# 当前工程实测：STM32F103C8T6 在本机 pyOCD 环境下使用 stm32f103rc

# [1/5] 编译
& $UV4 -b (Join-Path $ProjRoot "project.uvprojx") -j0 -t $Target -o $Log

# [2/5] 检查 build log（以 log 为准，不只看 UV4 返回码）
if (!(Test-Path $Log)) { throw "build log not found: $Log" }
$raw = Get-Content -Raw $Log
if ($raw -notmatch '0 Error\(s\)') { throw "Build failed: not found 0 Error(s)" }

# [3/5] 选择烧录镜像：优先 HEX，失败时自动回退 AXF
$Images = @()
if (($raw -match 'creating hex file') -and (Test-Path $Hex)) { $Images += $Hex }
if (Test-Path $Axf) { $Images += $Axf }
if ($Images.Count -eq 0) { throw "Neither HEX nor AXF exists" }

# [4/5] 探测 DAPLink
& $Pyocd list --probes
if ($LASTEXITCODE -ne 0) { throw "DAPLink probe enumerate failed" }

# [5/5] 烧录（当前工程实测：STM32F103C8T6 可用 stm32f103rc）
$FlashOk = $false
foreach ($Image in $Images) {
  Write-Host "TRY_IMAGE=$Image"
  & $Pyocd erase --chip --no-config -t $PyocdTarget -M under-reset -f 100000
  if ($LASTEXITCODE -ne 0) { continue }
  & $Pyocd load --no-config -t $PyocdTarget -M under-reset -f 100000 -e sector $Image
  if ($LASTEXITCODE -eq 0) {
    $FlashOk = $true
    break
  }
}
if (-not $FlashOk) { throw "All pyOCD load attempts failed" }
```

### 5.1.1.1 STM32（ARM）：PowerShell 一键编译 + pyOCD（DAPLink）烧录【实测更稳：锁定 Probe Unique ID + 默认全片擦除 + 自动重试 + HEX/AXF 兜底】

> 适用场景：
>
> - `pyocd list --probes` 能看到 CMSIS-DAP 探头，但经常出现 `Target = n/a` 或偶发连接失败
> - 需要脚本自动重试；失败时你只需要按提示给目标板断电上电
>
> 关键点：
>
> - **锁定探头 Unique ID**（避免多探头/枚举漂移）
> - **先 commander/status 再 erase + load**（连通性不过就别浪费时间烧）
> - **`-M under-reset`**（跑飞/上电不稳时更容易连上）
> - **降频 `-f 100000`**（不稳继续降到 50000/20000）
> - **PowerShell 避免 `$ErrorActionPreference='Stop'`**：pyOCD 会把 INFO 打到 stderr，PowerShell 可能误判为错误并中断

```powershell
# 变量（按实际修改；本机 Project_Refactor 可直接用 1.1 的已验证路径）
$PROJ_ROOT = "{项目根目录}"
$UV4_ARM   = "{UV4_ARM路径}"
$TARGET    = "{Target名}"
$LOG       = Join-Path $PROJ_ROOT "Objects\project.build_log.htm"
$HEX       = Join-Path $PROJ_ROOT "Objects\project.hex"
$AXF       = Join-Path $PROJ_ROOT "Objects\project.axf"
$PYOCD     = "{pyOCD路径}"
$TGT       = "{pyOCD目标名}"   # 当前工程实测：stm32f103rc

# 说明：Unique ID 可先运行 `pyocd list --probes` 获取；也可以先留空，让脚本自动取第 1 个
$PROBE_UID = ""  # 例如：031305620164

# 重试与频率参数
$MAX_TRIES = 10
$FREQ      = 100000

if (!(Test-Path $PROJ_ROOT)) { throw "PROJ_ROOT not found: $PROJ_ROOT" }
if (!(Test-Path $UV4_ARM))   { throw "UV4_ARM not found: $UV4_ARM" }
if (!(Test-Path $PYOCD))     { throw "pyOCD not found: $PYOCD" }

# [1/4] 编译
& $UV4_ARM -b (Join-Path $PROJ_ROOT "project.uvprojx") -j0 -t $TARGET -o $LOG
if (!(Test-Path $LOG)) { throw "build log not found: $LOG" }
$raw = Get-Content -Raw -Encoding Default $LOG
if ($raw -notmatch '0 Error\(s\)') { throw "Build failed: not found 0 Error(s)" }

# [2/4] 选择镜像：优先 HEX，失败回退 AXF
$Images = @()
if (Test-Path $HEX) { $Images += $HEX }
if (Test-Path $AXF) { $Images += $AXF }
if ($Images.Count -eq 0) { throw "Neither HEX nor AXF exists" }

# [3/4] 枚举探头并确定 Unique ID
Write-Host "=== pyOCD probes ==="
$probeText = (& $PYOCD list --probes 2>&1 | Out-String)
Write-Host $probeText

if ([string]::IsNullOrWhiteSpace($PROBE_UID)) {
  # 从输出中尽量提取第一行 Unique ID（输出格式：Probe/Board  Unique ID  Target）
  $m = [regex]::Match($probeText, "(?m)^\s*0\s+.+?\s+(\d{6,})\s+\S+\s*$")
  if (-not $m.Success) { throw "Cannot parse probe Unique ID. Please set $PROBE_UID manually." }
  $PROBE_UID = $m.Groups[1].Value
}

Write-Host ("USE_PROBE_UID=" + $PROBE_UID)

# [4/4] 自动重试烧录
for ($i = 1; $i -le $MAX_TRIES; $i++) {
  Write-Host "\n=== TRY $i/$MAX_TRIES ==="

  Write-Host "-- commander status --"
  & $PYOCD commander --no-config -u $PROBE_UID -t $TGT -c status -c exit 2>&1 | Out-Host

  foreach ($img in $Images) {
    Write-Host ("-- erase chip before load: " + $img)
    & $PYOCD erase --chip --no-config -u $PROBE_UID -t $TGT -M under-reset -f $FREQ 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) {
      continue
    }
    Write-Host ("-- load image: " + $img)
    & $PYOCD load --no-config -u $PROBE_UID -t $TGT -M under-reset -f $FREQ -e sector $img 2>&1 | Out-Host
    if ($LASTEXITCODE -eq 0) {
      Write-Host "FLASH_OK"
      break
    }
  }

  if ($LASTEXITCODE -eq 0) { break }

  Write-Host "FLASH_FAIL"
  Write-Host "ACTION: 请给目标板断电->上电（冷启动），等待 2 秒；脚本 3 秒后自动重试"
  Start-Sleep -Seconds 3
}

if ($LASTEXITCODE -ne 0) {
  throw "Flash failed after retries. Check power/SWD wiring (SWDIO/SWCLK/GND/3V3/NRST) and try lower -f."
}
```

### 5.1.1.2 STM32（ARM）：PowerShell 一键编译 + pyOCD，默认全片擦除后重烧

> 适用场景：
>
> - 已确认本机 `pyOCD` 可识别的目标名，例如 `stm32f103rc`
> - 想优先走 `flash`，但遇到 `flash program page failure (address 0x08000000; result code 0x1)` 时自动切到“全片擦除后重烧”

```powershell
$ProjRoot = "{项目根目录}"
$UV4 = "{UV4_ARM路径}"
$Target = "{Target名}"
$Log = Join-Path $ProjRoot "Objects\project.build_log.htm"
$Hex = "{HEX路径}"
$Pyocd = "{pyOCD路径}"
$PyocdTarget = "{pyOCD目标名}"

# [1/4] 编译
& $UV4 -b (Join-Path $ProjRoot "project.uvprojx") -j0 -t $Target -o $Log
if (!(Test-Path $Log)) { throw "build log not found: $Log" }
$raw = Get-Content -Raw -Encoding Default $Log
if ($raw -notmatch '0 Error\(s\)') { throw "Build failed: not found 0 Error(s)" }
if ($raw -notmatch 'creating hex file') { throw "Build failed: hex not created" }

# [2/4] 校验 HEX
if (!(Test-Path $Hex)) { throw "HEX not found: $Hex" }
$hexItem = Get-Item $Hex
$src = Get-ChildItem "$ProjRoot\User", "$ProjRoot\Hardware", "$ProjRoot\System" -Recurse -Include *.c,*.h |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($hexItem.LastWriteTime -lt $src.LastWriteTime) { throw "HEX is older than source. Rebuild." }

# [3/4] 默认全片擦除
Write-Host "ERASE_CHIP"
& $Pyocd erase --chip --no-config -t $PyocdTarget -M under-reset -f 100000
if ($LASTEXITCODE -ne 0) { throw "Chip erase failed" }

# [4/4] 再 load HEX
Write-Host "ERASE_OK -> LOAD_HEX"
& $Pyocd load --no-config -t $PyocdTarget -M under-reset -f 100000 -e sector $Hex
if ($LASTEXITCODE -ne 0) { throw "Load failed after chip erase" }
```

---

### 5.2 STC/C51：编译+烧录一键脚本（stcgal 示例）

**基础版本:**

```batch
@echo off
chcp 65001 >nul
set 项目根目录=f:\Project\MyProject
set UV4路径=D:\keil\Keil-v5\C51\UV4\UV4.exe
set 串口号=COM6

echo [1/3] 正在编译...
%UV4路径% -b "%项目根目录%\project.uvproj" -j0 -t "Target 1"
if errorlevel 1 (
    echo 编译失败!
    pause
    exit /b 1
)

echo [2/3] 编译成功，准备烧录...
echo [3/3] 请给开发板断电再上电，然后按任意键继续...
pause >nul

stcgal -P {芯片型号} -p %串口号% "%项目根目录%\Objects\project.hex"

echo.
if %errorlevel% == 0 (
    echo 烧录成功！
) else (
    echo 烧录失败，请重试（最多5次）
)
pause
```

**带自动重试的版本:**

```batch
@echo off
chcp 65001 >nul
echo ========================================
echo MCU项目 - 一键编译烧录工具
echo ========================================

set 项目根目录=f:\Project\MyProject
set UV4路径=D:\keil\Keil-v5\C51\UV4\UV4.exe
set 串口号=COM6
set 烧录次数=0
set 最大次数=5

echo.
echo [1/3] 正在编译...
%UV4路径% -b "%项目根目录%\project.uvproj" -j0 -t "Target 1"
if errorlevel 1 (
    echo [错误] 编译失败! 请检查代码错误。
    pause
    exit /b 1
)
echo [成功] 编译完成！

echo.
echo [2/3] 准备烧录...
echo HEX文件: %项目根目录%\Objects\project.hex

echo.
echo ========================================
echo [3/3] 开始烧录（最多%最大次数%次尝试）
echo ========================================
echo 【重要】当显示 "Waiting for MCU" 时:
echo   1. 拔掉开发板USB线（断电）
echo   2. 等待2秒
echo   3. 插回USB线（上电）
echo.

:retry_loop
set /a 烧录次数+=1
echo.
echo ---------- 第 %烧录次数% / %最大次数% 次尝试 ----------

stcgal -P {芯片型号} -p %串口号% "%项目根目录%\Objects\project.hex"
if %errorlevel% == 0 (
    echo.
    echo ========================================
    echo 烧录成功！
    echo ========================================
    pause
    exit /b 0
)

echo.
echo [失败] 第 %烧录次数% 次烧录失败。
if %烧录次数% lss %最大次数% (
    echo 请按任意键进行下一次尝试...
    pause >nul
    goto retry_loop
) else (
    echo ========================================
    echo 已达到最大重试次数(%最大次数%)，烧录失败。
    echo 请检查:
    echo  - 开发板是否连接
    echo  - 串口号是否正确
    echo  - USB线是否完好
    echo  - 是否进行了断电再上电操作
    echo ========================================
    pause
    exit /b 1
)
```

---

## 六、项目结构说明

```
MyProject/
├── User/
│   └── main.c              # 主程序文件
├── Objects/
│   ├── project.hex         # 编译生成的烧录文件
│   └── project.build_log.htm  # 编译日志
├── System/                 # 系统文件夹（延时、中断等）
├── Hardware/               # 硬件驱动文件夹
├── Library/                # 库文件夹
├── Start/                  # 启动文件
│   └── startup.s
├── project.uvproj          # Keil项目文件
└── 烧录.bat                # 一键烧录脚本
```

**标准项目特点:**

- 按功能分层：User/、System/、Hardware/、Library/
- 模块化设计，便于维护和复用
- 清晰的项目结构，方便团队协作

---

## 七、从零开始的开发流程

### 7.1 初始化项目

1. 根据项目需求创建文件夹结构
2. 将库文件放入对应目录
3. 配置project.uvproj，添加所有需要的文件引用
4. 设置正确的IncludePath

### 7.2 编写代码

1. 在main.c中按步骤编写代码
2. 每完成一个功能就编译测试
3. 遇到错误时参考"2.3 编译失败排查"

### 7.3 编译烧录

1. 编译生成HEX文件
2. 使用烧录脚本或命令行烧录
3. 如失败，按"4.4 重试机制"最多尝试5次

---

## 八、常见问题

### Q: 编译报错 "syntax error near 'identifier'"

A: 检查变量声明位置。某些编译器要求变量声明必须在函数开头，将变量声明移到函数开始处。

### Q: 外设不工作

A: 检查：

1. 硬件连接是否正确
2. 驱动代码是否初始化相应外设
3. 时钟配置是否正确
4. 引脚配置是否正确（输入/输出模式）

### Q: 按键或输入信号抖动

A: 添加软件消抖：

- 连续多次采样取稳定值
- 使用定时器中断定期采样
- 使用边沿检测，只触发一次

### Q: 烧录失败"Waiting for MCU"

A: 需要给目标设备断电再上电(冷启动)才能进入下载模式。如果一次失败，请重试最多5次。

**注意：** 不同MCU的冷启动要求可能不同，请参考具体芯片的数据手册。

### Q: 编译报错 "can't open file 'Hardware\Key.c'"

A: 项目引用了不存在的库文件。请打开project.uvproj，删除Hardware/和System/组中不存在的文件引用。

### Q: Keil 显示 `0 Error(s)`，但没有生成 `project.hex`

A: 按下面顺序检查：

1. 检查 build log 里是否真的出现了 `creating hex file`
2. 检查 `project.uvprojx` 里是否启用了 `<CreateHexFile>1</CreateHexFile>`
3. 如果 `hex` 确实没生成，但 `Objects\project.axf` 已更新，**可直接用 `pyOCD` 烧录 `axf`**

### Q: `pyocd` 一运行就报 JSON 解析错误，例如 `Expecting ',' delimiter`

A: 这通常是 `cmsis-pack-manager` 的索引缓存损坏。实测可先备份并移走：

```powershell
Move-Item "C:\Users\DZ\AppData\Local\cmsis-pack-manager\cmsis-pack-manager\index.json" "C:\Users\DZ\AppData\Local\cmsis-pack-manager\cmsis-pack-manager\index.json.bak"
```

然后重新执行：

```powershell
pyocd list --targets
pyocd list --probes
```

### Q: `pyocd list --probes` 能看到 DAPLink，但 `Target` 显示 `n/a`

A: 优先检查：

1. 板子是否上电
2. SWDIO / SWCLK / GND / 3V3 是否接对
3. 目标名是否正确
4. 是否需要改为 `-M under-reset`

### Q: `pyocd load` 输出 `skipped 36864 bytes (18 pages)` 是失败吗？

A: 不是。这通常表示：

- 板子里已经是相同程序
- pyOCD 做了内容比较后跳过了重复写入
- 这是正常优化行为，不代表烧录失败

### Q: `pyocd flash` 报 `flash program page failure (address 0x08000000; result code 0x1)` 怎么办？

A: 按下面顺序处理：

1. 先确认目标名是否正确；本机 `STM32F103C8T6` 实测应使用 `stm32f103rc`
2. 给目标板断电上电，再重试 1 次 `flash`
3. 如果仍失败，执行：

```powershell
pyocd erase --chip -f 1000000 -t stm32f103rc
pyocd flash -f 1000000 -t stm32f103rc "{HEX路径}"
```

4. 如果 `erase --chip` 后仍失败，再回头检查：
   - SWDIO / SWCLK / GND / 3V3 / NRST 接线
   - 板子供电
   - DAPLink 探头状态
   - 是否存在读保护 / 芯片异常

---

## 九、相关工具路径

- **Keil UV4 (ARM):** `D:\keil\Keil-v5\Arm\UV4\UV4.exe`
- **Keil UV4 (C51):** `D:\keil\Keil-v5\C51\UV4\UV4.exe`
- **STM32CubeProgrammer CLI:** `E:\STMcubeProgrammer\programmer\bin\STM32_Programmer_CLI.exe`
- **pyOCD:** `C:\Users\DZ\AppData\Local\Programs\Python\Python313\Scripts\pyocd.exe`
- **cmsis-pack-manager 缓存目录:** `C:\Users\DZ\AppData\Local\cmsis-pack-manager\cmsis-pack-manager`
- **其他烧录工具:** 根据MCU型号选择（stcgal、J-Link、OpenOCD等）

---

**文档生成时间:** 2025年-2026年
**适用于:** 所有 Keil MDK 工程（ARM、C51等）
**版本:** v4.2（补充 pyOCD 首页页写失败后全片擦除重烧实测流程）

---

# 附录：Keil 工程文件 (.uvprojx) 手动配置指南

本文档说明如何直接编辑 `project.uvprojx` 文件来管理 Keil 工程配置，无需通过 Keil IDE 界面操作。

## 一、文件位置与备份

### 1.1 文件路径

```
{项目根目录}\project.uvprojx
```

### 1.2 修改前必做

**强烈建议：** 修改前备份原文件

```bash
copy project.uvprojx project.uvprojx.backup
```

### 1.3 文件格式

- 标准 XML 格式
- 使用 UTF-8 编码
- 可用任何文本编辑器修改（VS Code、Notepad++、记事本等）

## 二、关键配置节点说明

### 2.1 文件引用节点 (Groups)

**路径：** `Project → Targets → Target → Groups → Group → Files`

```xml
<Groups>
  <Group>
    <GroupName>Hardware</GroupName>    <!-- 分组名称 -->
    <Files>
      <File>
        <FileName>Key.c</FileName>      <!-- 显示名称 -->
        <FileType>1</FileType>          <!-- 1=C文件, 5=头文件 -->
        <FilePath>.\Hardware\Key.c</FilePath>  <!-- 相对路径 -->
      </File>
      <File>
        <FileName>Key.h</FileName>
        <FileType>5</FileType>
        <FilePath>.\Hardware\Key.h</FilePath>
      </File>
    </Files>
  </Group>
</Groups>
```

**FileType 代码：**

- `1` - C 源文件 (.c)
- `2` - 汇编文件 (.s)
- `5` - 头文件 (.h)

### 2.2 头文件搜索路径 (IncludePath)

**路径：** `Project → Targets → Target → TargetOption → TargetArmAds → Cads → VariousControls → IncludePath`

```xml
<VariousControls>
  <MiscControls></MiscControls>
  <Define>STM32F10X_MD,USE_STDPERIPH_DRIVER</Define>
  <Undefine></Undefine>
  <IncludePath>.\Start;.\System;.\User;.\Library;.\Hardware;.\Hardware\IIC</IncludePath>
</VariousControls>
```

**路径格式：**

- 使用分号 `;` 分隔多个路径
- 使用 `.{文件夹}` 表示相对项目根目录的路径
- 支持子文件夹，如 `.\Hardware\IIC`

## 三、常用操作示例

### 3.1 添加文件到工程

**示例：添加 IIC.c 到 Hardware 分组**

在 `<GroupName>Hardware</GroupName>` 下的 `<Files>` 节点内添加：

```xml
<File>
  <FileName>IIC.c</FileName>
  <FileType>1</FileType>
  <FilePath>.\Hardware\IIC\IIC.c</FilePath>
</File>
```

**完整操作步骤：**

1. 找到目标 Group（如 Hardware）
2. 在 `<Files>` 节点内添加新的 `<File>` 节点
3. 确保 FilePath 指向正确的相对路径
4. 保存文件
5. 重启 Keil 加载新配置

### 3.2 从工程中移除文件

**示例：移除 Key.c**

找到并删除整个 `<File>` 节点：

```xml
<!-- 删除以下内容 -->
<File>
  <FileName>Key.c</FileName>
  <FileType>1</FileType>
  <FilePath>.\Hardware\Key.c</FilePath>
</File>
```

**注意：** 这仅从工程引用中移除，不会删除实际文件。

### 3.3 移动文件到子文件夹

**示例：将 OLED 文件移动到 Hardware/IIC/**

**修改前：**

```xml
<File>
  <FileName>OLED.c</FileName>
  <FileType>1</FileType>
  <FilePath>.\Hardware\OLED.c</FilePath>
</File>
```

**修改后：**

```xml
<File>
  <FileName>OLED.c</FileName>
  <FileType>1</FileType>
  <FilePath>.\Hardware\IIC\OLED.c</FilePath>  <!-- 更新路径 -->
</File>
```

**同时需要：**

1. 实际移动文件到目标文件夹
2. 更新 IncludePath 添加新路径

### 3.4 添加 Include 搜索路径

**示例：添加 Hardware/IIC 到搜索路径**

**修改前：**

```xml
<IncludePath>.\Start;.\System;.\User;.\Library;.\Hardware</IncludePath>
```

**修改后：**

```xml
<IncludePath>.\Start;.\System;.\User;.\Library;.\Hardware;.\Hardware\IIC</IncludePath>
```

**要点：**

- 在末尾添加 `;.{新路径}`
- 保持原有路径不变
- 路径使用反斜杠 `\`

## 四、创建新的分组

**示例：创建 IIC 分组**

在 `<Groups>` 节点内添加：

```xml
<Group>
  <GroupName>IIC</GroupName>
  <Files>
    <File>
      <FileName>IIC.c</FileName>
      <FileType>1</FileType>
      <FilePath>.\Hardware\IIC\IIC.c</FilePath>
    </File>
    <File>
      <FileName>SoftIIC.c</FileName>
      <FileType>1</FileType>
      <FilePath>.\Hardware\IIC\SoftIIC.c</FilePath>
    </File>
  </Files>
</Group>
```

## 五、常见问题排查

### 5.1 文件找不到警告

**现象：** Keil 中文件显示黄色感叹号

**原因：** FilePath 指向的文件不存在

**解决：**

1. 检查文件是否实际存在于指定路径
2. 修正 FilePath 为正确路径
3. 或删除该文件引用

### 5.2 头文件找不到

**现象：** 编译错误 `cannot open source file "xxx.h"`

**原因：** IncludePath 未包含头文件所在目录

**解决：**

1. 找到头文件实际路径
2. 添加到 IncludePath
3. 重启 Keil

### 5.3 修改后不生效

**原因：** Keil 缓存了旧的配置

**解决：**

1. 完全关闭 Keil
2. 重新打开工程
3. 如仍不生效，删除 `project.uvoptx` 缓存文件

## 六、快速检查清单

修改 `uvprojx` 后，确认以下事项：

- [ ] 文件路径使用反斜杠 `\`
- [ ] IncludePath 使用分号 `;` 分隔
- [ ] FileType 与文件类型匹配 (1=C, 5=H)
- [ ] 实际文件存在于指定路径
- [ ] 备份了原文件
- [ ] 重启 Keil 加载新配置

## 七、XML 格式注意事项

1. **标签必须闭合：** `<File>...</File>`
2. **区分大小写：** `<FileName>` 不等于 `<filename>`
3. **特殊字符转义：**
   - `&` → `&amp;`
   - `<` → `&lt;`
   - `>` → `&gt;`
4. **保持缩进：** 便于阅读和检查
