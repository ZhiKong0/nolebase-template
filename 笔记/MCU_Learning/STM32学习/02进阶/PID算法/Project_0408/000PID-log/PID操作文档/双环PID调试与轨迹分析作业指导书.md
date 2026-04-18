# 双环 PID 调试与轨迹分析作业指导书

## 1. 文档目的与适用范围

本文面向 `Project_Refactor` 小车工程，目标是把以下几件事固定成标准流程：

- 基于 `DAPLink + pyOCD` 完成编译与烧录
- 在 **不使用 `trim`** 的前提下完成起步偏置调整
- 使用 `exp_runner.py` 采集实验数据
- 使用 `trajectory_analyzer.py` 进行 **0.5 秒分段**状态分析与整体轨迹重建
- 根据轨迹、偏航角、编码器数据判断下一轮 PID 与起步保护参数的调整方向

> **安全警告**
> 
> 任何烧录、`#RUN!`、`#EXP=RUN,...!`、`#RAW=...!` 操作前，必须先确认小车已架空或处于可控试验区，周围无人手脚靠近车轮。

---

## 2. 硬件与工具链配置

### 2.1 硬件连接基线

当前工程的姿态传感器已经切换到 `BNO085`，控制链路基于编码器 + BNO085 偏航角工作。

推荐现场固定检查项：

- **[主控]** STM32F103 系列目标板已上电
- **[调试器]** DAPLink 已连接电脑与目标板 SWD
- **[串口]** USB-TTL 或 DAPLink 虚拟串口侧统一使用 `COM18`
- **[IMU]** BNO085 已按工程接线接入，能通过 `#IMU_INIT!` / `#STAT!` 看到 `who=0x85`
- **[编码器]** 左右编码器方向、计数与符号已确认

### 2.2 工具链统一约定

本文统一采用以下工具链：

- **[调试器]** DAPLink
- **[烧录工具]** `pyOCD`
- **[工程编译]** `Keil UV4` 命令行编译
- **[串口通信]** `COM18`
- **[串口参数]** `115200 8N1`

### 2.3 关于 `COM18` 与 pyOCD 的说明

需要特别说明一件事：

- **[串口通信端口]** 本文统一指定为 `COM18`
- **[烧录链路]** `pyOCD` 实际通过 DAPLink 的 `CMSIS-DAP` 接口访问目标板，不是通过 `COM18` 这个串口号去烧录

因此现场操作上可以统一记忆为：

- **[串口调参/实验口]** `COM18`
- **[烧录设备]** 与 `COM18` 同属一套 DAPLink/调试连接，但 `pyOCD` 命令本身不写 `COM18`

这样既满足统一管理，也符合当前工具真实行为。

### 2.4 编译与烧录标准步骤

以下步骤基于 `md/mcu-build-flash.md` 的当前推荐流程整理。

#### 步骤 1：Keil 命令行编译

```powershell
$PROJ_ROOT = "f:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project_Refactor"
$UV4_ARM   = "D:\keil\Keil-v5\Arm\UV4\UV4.exe"
$TARGET    = "Target 1"
$LOG       = "$PROJ_ROOT\Objects\project.build_log.htm"

& $UV4_ARM -b "$PROJ_ROOT\project.uvprojx" -j0 -t $TARGET -o $LOG
```

#### 步骤 2：检查 `project.build_log.htm`

必须同时确认：

- **[成功条件 1]** 出现 `0 Error(s)`
- **[成功条件 2]** 出现 `creating hex file`

#### 步骤 3：验证 HEX 是否为最新

必须确认：

- `Objects\project.hex` 存在
- `project.hex` 的修改时间晚于 `User/Hardware/System` 中最新 `.c/.h` 文件

#### 步骤 4：pyOCD 烧录

当前推荐流程是：**先全片擦除，再烧录**。

```powershell
$PYOCD = "C:\Users\DZ\AppData\Local\Programs\Python\Python313\Scripts\pyocd.exe"
$HEX   = "$PROJ_ROOT\Objects\project.hex"
$TGT   = "stm32f103rc"

& $PYOCD erase --chip --no-config -t $TGT -M under-reset -f 100000
& $PYOCD load --no-config -t $TGT -M under-reset -f 100000 -e sector $HEX
```

### 2.5 烧录安全与失败处理

- **[安全]** 烧录前电机应停止，避免程序复位后电机突然起转
- **[失败重试]** 若 `load` 首次失败，优先断电上电后重试
- **[经验]** 若出现首页写失败，优先坚持“`erase --chip` -> `load`”流程
- **[不要跳过检查]** 不要只看 Keil 返回码，必须读 `project.build_log.htm`

---

## 3. 控制结构与调试入口总览

当前工程控制结构为典型串级双环：

```text
目标速度
  -> 速度环（speed loop）
  -> 目标姿态 / targetAngle
  -> 姿态环（angle loop）
  -> 核心 PWM 输出 pwmCore
  -> 航向差速修正 + 起步偏置 + 左右死区补偿
  -> 左右轮最终输出 OL / OR
```

其中：

- **[速度环]** 依据编码器平均速度计算 `speedErr / speedOut`
- **[姿态环]** 依据姿态误差计算 `angleErr / angleOut`
- **[航向环]** 依据 `yaw` 与 `yawRate` 做差速修正
- **[起步偏置与死区]** 用于解决起步前段左右轮不对称问题

### 3.1 当前 `exp_runner.py` 默认主路径的能力边界

`exp_runner.py` 的 `main()` 当前默认主路径已经支持下发以下参数：

- `#SPD=`
- `#SKP=`
- `#SKI=`
- `#SKD=`
- `#AKP=`
- `#AKI=`
- `#AKD=`
- `#DZ=`
- `#DZT=`
- `#DZL=`
- `#DZR=`
- `#SBF=`
- `#SBV=`
- `#SIV=`
- `#HDT=`

这意味着：

- **[能做]** 常规 PID、基础死区、左右独立死区、起步偏置与起步保护参数的一次性 CLI 下发与实验采集
- **[仍建议]** 批量扫参前先用少量手工串口命令确认参数方向与作用，再用 `exp_runner.py` 做标准化采样
- **[结论]** 现在可以采用“**`exp_runner.py` 直接设参采集 + `trajectory_analyzer.py` 分析**”作为主流程；现场快速排障时再退回手动串口命令

---

## 4. 偏置调整方案（严禁使用 `trim`）

## 4.1 基本约束

本文明确规定：**严禁使用 `trim` 变量调偏置。**

原因有三点：

- **[当前固件现实]** 当前 `Hardware/Control.c` 中没有 `#TRIM=` 命令解析入口
- **[脚本现实]** `exp_runner.py` 的 `main()` 虽然保留了被隐藏的 `--trim` 参数，但当前默认实验路径并不会把它转成当前固件可用的偏置命令
- **[分析现实]** `trajectory_analyzer.py` 中保留 `trim` 字段是为了兼容旧日志，不代表当前分支应该依赖 `trim` 调偏

因此，当前工程的起步偏置调整，必须使用以下参数族完成。

## 4.2 当前固件允许的非 `trim` 偏置入口

### A. 左右独立死区

- `#DZL=`：左侧死区补偿
- `#DZR=`：右侧死区补偿
- `#DZ=`：同时设置左右统一死区
- `#DZT=`：死区阈值

### B. 起步偏置

- `#SBF=`：起步偏置幅值
- `#SBV=`：起步偏置退出速度阈值

### C. 起步保护

- `#SIV=`：速度积分启用阈值
- `#HDT=`：航向环延迟介入时间

## 4.3 `SBF` 的实际方向含义

根据当前 `Control.c` 的 `control_apply_motor_output()` 实现：

- 当前进时 `SBF > 0`
  - 左侧核心输出减小
  - 右侧核心输出增大
  - 即：**补右轮**
- 当前进时 `SBF < 0`
  - 左侧核心输出增大
  - 右侧核心输出减小
  - 即：**补左轮**

因此：

- **[右轮偏慢 / 车容易向右偏]** 优先考虑小幅增加正向 `SBF`
- **[左轮偏慢 / 车容易向左偏]** 优先考虑小幅减小 `SBF`，即让 `SBF` 向负方向走

> **注意**
> 
> 方向判断优先看 `el/er`、`OL/OR`，不要只凭肉眼感觉判断偏航方向。

## 4.4 推荐调参顺序

当前工程推荐的起步偏置整定顺序为：

```text
DZL / DZR -> SBF -> SBV -> SIV -> HDT
```

含义如下：

- **[先补死区]** 先让左右轮尽量同时起转
- **[再补偏置]** 再用 `SBF` 微调起步前段左右不对称
- **[再限作用区间]** 用 `SBV` 控制偏置何时退出
- **[再控积分介入]** 用 `SIV` 防止起步前几百毫秒积分抢控制
- **[最后控航向介入]** 用 `HDT` 防止航向环过早“抢方向盘”

## 4.5 标准操作步骤

### 步骤 1：进入文本遥测模式

```text
#VOFA=0!
#STAT!
```

目的：

- 确保 `STAT/HB/EXP_DUMP` 可见
- 避免二进制 `VOFA` 模式下文本被抑制

### 步骤 2：做基线校准

```text
#STOP!
#IMU_INIT!
#CAL!
#STAT!
```

关注：

- `who=0x85`
- `addr=0x4A` 或 `0x4B`
- `y`、`yr` 静止时是否正常

### 步骤 3：下发基础 PID 与速度档位

建议先把常规 PID 调到已知稳定值，再做起步偏置整定。

示例：

```text
#SPD=1!
#SKP=0.10!
#SKI=0.012!
#SKD=0.00!
#AKP=7.00!
#AKI=0.08!
#AKD=0.35!
```

### 步骤 4：设置起步偏置相关参数

示例：

```text
#DZL=10!
#DZR=12!
#DZT=1.0!
#SBF=2!
#SBV=18!
#SIV=18!
#HDT=400!
#STAT!
```

### 步骤 5：执行试跑

有两种推荐方式。

#### 方式 A：直接串口试跑

```text
#EXP=STREAM,1!
#EXP=RUN,101,10000!
```

试验结束后：

```text
#EXP=STREAM,0!
#EXP=DUMP,101!
```

#### 方式 B：使用 `exp_runner.py` 做标准采集

```bash
python exp_runner.py --port COM18 --baud 115200 --id 101 --ms 10000 --spd 1 --skp 0.1 --ski 0.012 --skd 0.0 --akp 7.0 --aki 0.08 --akd 0.35 --dz 10 --dzt 1.0 --dzl 10 --dzr 12 --sbf 2 --sbv 18 --siv 18 --hdt 400 --realtime
```

> **注意**
> 
> 当前 `exp_runner.py` 已支持 `--dzl`、`--dzr`、`--sbf`、`--sbv`、`--siv`、`--hdt`。若你希望脚本完全接管一次实验，请直接在命令行显式写出这些参数，避免沿用上一次上电遗留的固件参数。

## 4.6 各参数的判断逻辑

### `DZL / DZR`

适用于：

- 起步瞬间 `el/er` 就不对称
- 一侧轮子明显晚起转

调整原则：

- **[谁慢补谁]** 哪一侧编码器起得慢，就优先增加哪一侧死区补偿
- **[小步进]** 每次改 `1 PWM`

### `SBF`

适用于：

- `DZL/DZR` 已大致对称
- 但起步前段仍有稳定单侧偏航

调整原则：

- **[小步进]** 每次改 `1 PWM`
- **[先看 `sbo`]** `STAT/HB` 中 `sbo` 只应在低速起步段短暂非零

### `SBV`

适用于：

- 起步偏置有效，但退出过早或拖得过晚

调整原则：

- **[偏置退太快]** 提高 `SBV`
- **[偏置拖太久]** 降低 `SBV`

### `SIV`

适用于：

- 起步前几百毫秒有明显猛冲
- 偏航伴随突发速度拉升

调整原则：

- **[起步冲得太猛]** 增大 `SIV`
- **[稳态误差长期难收敛]** 再考虑适度降低 `SIV` 或微调速度环 `Ki`

### `HDT`

适用于：

- 车刚起步就像被强行拉方向
- 前段 `OL/OR` 很快出现明显差速

调整原则：

- **[过早抢控制]** 增大 `HDT`
- **[延迟过长，中段才开始管方向]** 适度减小 `HDT`

---

## 5. 数据采集与分析流程

## 5.1 采样基线：按 0.5 秒分段

本文统一按 **每 0.5 秒** 输出一个运行状态窗口。

这与当前脚本实现一致：

- `exp_runner.py` 中 `DETAIL_WINDOW_S = 0.5`
- `trajectory_analyzer.py` 中 `--segment-s` 默认值为 `0.5`
- `build_time_windows()` 会按该窗口长度生成阶段诊断结果

## 5.2 推荐采集方式

### 方式 A：标准实验采集

```bash
python exp_runner.py --port COM18 --baud 115200 --id 201 --ms 10000 --spd 1 --skp 0.1 --ski 0.012 --skd 0.0 --akp 7.0 --aki 0.08 --akd 0.35 --dz 10 --dzt 1.0 --dzl 10 --dzr 12 --sbf 2 --sbv 18 --siv 18 --hdt 400 --realtime
```

当前默认主路径会：

- 打开 `COM18`
- 发送 `#STOP!`
- 下发 PID、`DZ/DZT`，以及 CLI 中显式指定的 `DZL/DZR/SBF/SBV/SIV/HDT`
- 发送 `#EXP=STREAM,1!`
- 发送 `#EXP=RUN,id,ms!`
- 记录原始串口日志到 `*_raw.txt`
- 自动调用 `emit_detailed_analysis()` 生成：
  - `*_windows.csv`
  - `*_trajectory.csv`

### 方式 B：原始日志单独复盘

若你已经有 `*_raw.txt`，可以直接调用：

```bash
python trajectory_analyzer.py --raw path\to\exp201_10000ms_xxx_raw.txt --segment-s 0.5 --plot --save path\to\exp201_plot.png
```

## 5.3 关键数据字段

当前 `trajectory_analyzer.py` 的 `parse_hb_rows()` 会从 `HB/STAT` 中读取并整理以下核心量：

- **[时间]** `tick`
- **[运行状态]** `run`
- **[偏航角]** `y -> yaw_deg`
- **[偏航角速度]** `yr`
- **[左右核心输出]** `L / R`
- **[左右最终输出]** `OL / OR`
- **[左右编码器速度]** `el / er`
- **[编码器差]** `ed`
- **[陀螺数据]** `gx / gy / gz`
- **[兼容字段]** `trim / at`（旧日志兼容，当前分支不作为主调参入口）

## 5.4 轨迹拼接原理

### 5.4.1 分段轨迹

`trajectory_analyzer.py` 中：

- `segment_rows()`：按时间窗切段
- `summarize_time_window()`：生成每段状态摘要
- `build_time_windows()`：把多个 `0.5s` 窗口拼成连续时间诊断列表

### 5.4.2 整体轨迹

整体轨迹由 `build_total_trajectory()` 完成。核心逻辑是：

- 先用 `split_run_stretches()` 找出所有 `run=1` 的连续运行段
- 再用 `reconstruct_path()` 或 `reconstruct_path_from_outputs()` 计算每段位移
- 最后按段拼接成整条轨迹

编码器轨迹的核心增量形式可理解为：

```text
v ≈ 0.5 * (el + er)
ds = v * dt * speed_scale
x += ds * cos(yaw)
y += ds * sin(yaw)
```

因此：

- **[编码器 + yaw]** 决定主轨迹
- **[OL/OR]** 可用于辅助判断控制是否已明显失衡

## 5.5 如何生成整体轨迹图

当 `trajectory_analyzer.py` 加 `--plot` 时，会调用 `matplotlib` 生成 4 个主要视图：

- **[图 1]** 平面轨迹图
  - 编码器轨迹
  - 输出辅助轨迹
  - 若存在多段运行，则显示 stitched 总轨迹
- **[图 2]** `yaw vs time`
- **[图 3]** `OL-OR` 与 `ed`
- **[图 4]** 曲率与 `yr`

保存 PNG 的示例：

```bash
python trajectory_analyzer.py --raw .\000Data\exp201_10000ms_20260326_160000_raw.txt --segment-s 0.5 --plot --save .\000Data\exp201_10000ms_plot.png
```

## 5.6 0.5 秒窗口的建议观察项

每个 `0.5s` 窗口至少记录：

- **[Yaw 状态]** `yaw_start / yaw_end / yaw_delta / yaw_mean`
- **[编码器状态]** `el_mean / er_mean / ed_mean`
- **[输出状态]** `outL_mean / outR_mean / out_diff_mean`
- **[轨迹状态]** `x_total_end / y_total_end / path_len`
- **[运动状态]** `state / motion_level / stiction`

## 5.7 如何根据分析结果决定下一步调参

### A. 起步阶段 `0~3s` 就明显偏

优先看：

- `el_mean / er_mean`
- `outL_mean / outR_mean`
- `y_total_end`
- `yaw_delta`

建议动作：

- **[编码器先失衡]** 先调 `DZL/DZR`
- **[输出先失衡]** 再调 `SBF`
- **[起步猛冲伴随偏航]** 调 `SIV`
- **[刚起步就被拉方向]** 调 `HDT`

### B. 轨迹整体弯曲但起步正常

优先看：

- `yaw_drift_deg_s`
- `lateral_final`
- `dominant_side`
- `out_diff_mean`

建议动作：

- **[中后段持续漂移]** 先查航向延迟与航向修正强度，再看姿态/速度环是否造成长期输出不对称

### C. 超调明显、来回摆动

典型表现：

- `yaw`、轨迹侧偏在相邻窗口反复换边
- `yr_rms` 偏大
- `curvature_rms` 偏大
- `phase_shift` 出现明显反复变化

建议动作：

- **[姿态内环过冲]** 适当增加 `AKD` 或减小 `AKP`
- **[速度外环推动过猛]** 适当减小 `SKP`

### D. 稳态误差明显、长期偏差不消失

典型表现：

- 轨迹整体单侧偏移但不剧烈振荡
- `yaw_drift_deg_s` 持续同号
- `ed_mean` 长期单侧偏置

建议动作：

- **[速度稳态误差]** 考虑微调 `SKI`
- **[姿态零偏长期存在]** 再评估 `AKI`
- **[仅起步失衡]** 不要急着碰 `Ki`，先回到 `DZL/DZR/SBF/SBV/SIV/HDT`

### E. 响应迟缓、轨迹拖泥带水

典型表现：

- `path_len` 增长慢
- `speed_proxy` 偏低
- `motion_level` 长期为 `barely_moving` 或 `slow_crawl`

建议动作：

- **[优先]** 先检查死区补偿与起步输出
- **[再考虑]** 适度增加 `SKP` 或 `AKP`

---

## 6. 推荐实验流程

## 6.1 常规双环实验

### 串口侧

```text
#VOFA=0!
#IMU_INIT!
#CAL!
#STAT!
```

### PC 侧

```bash
python exp_runner.py --port COM18 --baud 115200 --id 301 --ms 10000 --spd 1 --skp 0.1 --ski 0.012 --skd 0.0 --akp 7.0 --aki 0.08 --akd 0.35 --dz 10 --dzt 1.0 --dzl 10 --dzr 12 --sbf 2 --sbv 18 --siv 18 --hdt 400 --realtime
```

### 复盘侧

```bash
python trajectory_analyzer.py --raw .\000Data\exp301_10000ms_xxx_raw.txt --segment-s 0.5 --plot --save .\000Data\exp301_plot.png
```

## 6.2 起步偏置专项实验

### 串口先设起步参数

```text
#DZL=10!
#DZR=12!
#SBF=2!
#SBV=18!
#SIV=18!
#HDT=400!
#STAT!
```

### 再用 `exp_runner.py` 采集

```bash
python exp_runner.py --port COM18 --baud 115200 --id 302 --ms 8000 --spd 1 --skp 0.1 --ski 0.012 --skd 0.0 --akp 7.0 --aki 0.08 --akd 0.35 --dz 10 --dzt 1.0 --dzl 10 --dzr 12 --sbf 2 --sbv 18 --siv 18 --hdt 400 --realtime
```

### 重点只看前 0~3 秒

重点查看：

- `yaw_delta`
- `ed_mean`
- `out_diff_mean`
- `y_total_end`
- 各 `0.5s` 窗口的 `state`

---

## 7. 调试迭代记录表

建议每一轮试验都填写以下表格。

| 轮次 | 日期时间 | 固件版本/提交 | 串口 | 实验ID | 运行时长ms | 速度档 `SPD` | `SKP` | `SKI` | `SKD` | `AKP` | `AKI` | `AKD` | `DZL` | `DZR` | `DZT` | `SBF` | `SBV` | `SIV` | `HDT` | `yaw_delta` | `ed_mean` | `lateral_final` | `dominant_side` | 现象描述 | 下一步动作 |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| 1 |  |  | COM18 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| 2 |  |  | COM18 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| 3 |  |  | COM18 |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |

### 7.1 建议固定补充记录

每轮至少再补三条文字：

- **[起步 0~0.5s]** 左右轮谁先起转，`el/er` 是否对称
- **[起步 0~3s]** 偏航方向、是否有猛冲、是否像被航向环强拉
- **[全程]** 轨迹最终向哪一侧偏、是否出现明显振荡或饱和

---

## 8. 当前工程的几个重要注意事项

- **[注意 1]** 当前 `Control.c` 命令集里没有 `#TRIM=`，本文禁止把旧分支的 `trim` 方法带到当前分支
- **[注意 2]** 当前 `exp_runner.py main()` 默认不会暴露 `DZL/DZR/SBF/SBV/SIV/HDT` 这类起步专用参数
- **[注意 3]** 当前 `Control.c` 中 `#VOFA=3/5` 对应的是当前代码定义的 `JustFloat3/5` 数据通道，不要照搬旧文档
- **[注意 4]** 做 `STAT/HB/EXP_DUMP` 诊断时必须保持 `#VOFA=0!` 文本模式
- **[注意 5]** 若只想做快速电机开环试验，可使用 `#RAW=`，但同样要先架空小车

---

## 9. 结论

对当前 `Project_Refactor` 工程，推荐的标准方法不是“边跑边随意改很多参数”，而是：

- **[第一步]** 用 `Keil + pyOCD + DAPLink` 完成稳定编译烧录
- **[第二步]** 串口固定走 `COM18`，先确认 `BNO085` 与编码器状态
- **[第三步]** 起步偏置一律使用 `DZL/DZR/SBF/SBV/SIV/HDT`，**严禁使用 `trim`**
- **[第四步]** 用 `exp_runner.py` 采样，用 `trajectory_analyzer.py` 做 **0.5 秒分窗 + 整体轨迹拼接**
- **[第五步]** 根据 `yaw / el / er / OL / OR / 轨迹侧偏` 判断下一轮是调 PID 还是调起步保护参数

只要坚持这个流程，双环 PID 调试、起步偏置整定与轨迹分析就能统一在同一套工程语言下进行，避免“现象描述很多，但证据不闭环”的低效调参方式。
