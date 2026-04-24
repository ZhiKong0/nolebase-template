# Findings

## 2026-04-24 单链循迹 PID 重构

### 关键证据
- 当前 `fisheye` 的 `TRACK` 运行时参数面含有多组耦合量：
  - `centerKpScale / midKpScale / edgeKpScale`
  - `centerDevRatio / midDevRatio / edgeDevRatio`
  - `recenterDecayStep / centerDeadband / offcenterBoost / centerSingleHoldTicks`
- 这些量同时出现在：
  - [`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h)
  - [`Hardware/line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h)
  - [`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - [`User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c)
  - [`000Project_PC_Control/config.yaml`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/config.yaml)
  - [`000Project_PC_Control/track_adaptive_tuner.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/track_adaptive_tuner.py)
- `Project_track_infrared` 已经存在更干净的实现：`linePos -> 单一 PD -> 左右轮差速`，并且只保留最小找线状态机。

### 判断
- 旧实现的问题不只是参数难调，而是“多定义 + 多宏 + 多键值对”本身造成控制链和调参面耦合。
- 如果继续在旧链上修小问题，只会留下更多半废弃接口。
- 最优路径是整体迁移单链实现，再在 `fisheye` 上只保留必要兼容层。

### 本轮修正
- `line_track` 主链重构为：
  - 连续 `linePos`
  - 单一 `track.lkp / track.lkd`
  - 单一 `track.dev_ratio`
  - 单一 `track.deadband`
  - `pos_lpf / d_lpf`
  - 最小找线状态机
- 删除分层梯度相关运行时结构字段和配置宏。
- 清理旧短命令口，仅保留单链参数口。
- 同步把自适应调参脚本和 `config.yaml` 的参数表收敛到单链参数面。

## 兼容性判断

### 保留项
- `LineTrack_Init/Start/Stop/Update/IsRunning`
- 主循环与 `MODE_TRACK`
- 串口遥测 `HB`
- `dbgScoreEnabled` 遥测位

### 变化项
- `LT_TLM_STATE_CORNER` 不再作为主状态；外侧状态统一为 `LT_TLM_STATE_EDGE`
- 旧短命令不再可用：
  - `#CSR/#CSM/#CMR/#CMM/#EDR/#EDM/#RCD/#CDB/#OCB/#CHT`
- 自适应调参配置从“多梯度分层调”改成单链参数调

## 2026-04-24 自动调参骨架收口

### 关键证据
- 当前 `track_adaptive_tuner.py` 的搜索器已经是阶段化坐标搜索，不需要再退回网格扫描。
- 真正缺的是更贴近你赛道目标的评分骨架：
  - 之前更偏“中心占比 + 摆动率”
  - 还没有把“`EDGE` 后回中时间”和“`FNDL/FNDR` 后回中时间”单独建模
- 当前目标速度已经明确固定在 `40` 档，因此本轮不应再把 `speed_target` 当作搜索维度。

### 判断
- 本轮最合理的做法不是马上继续扫参数，而是先把自动调参骨架收成：
  - 固定 `speed_target=40`
  - 三阶段复测：`fit -> pid -> recovery`
  - 新评分项：`edge_recovery_* / search_recovery_*`
  - 更重罚 `S` 弯区的外侧占比、摆动频率和恢复耗时
- 这样后续真跑时，脚本优化方向才会和你肉眼评价一致。

### 本轮修正
- `track_adaptive_tuner.py`
  - 新增 `edge_recovery_success_ratio`
  - 新增 `edge_recovery_mean_s`
  - 新增 `search_recovery_success_ratio`
  - 新增 `search_recovery_mean_s`
  - 控制台输出同步展示恢复时间和恢复成功率
- `config.yaml`
  - `duration_s` 拉长到 `10s`
  - `repeats/confirm_best_repeats` 提升到 `2/3`
  - `speed_target` 固定为 `40`
  - 新增 `recovery` 阶段
  - 把 `recover_ticks/search_turn_fast/search_turn_slow/search_timeout` 纳入参数面

### 当前边界
- 本轮只把自动调参骨架补完整，不实际启动扫描。
- 也不在本轮重新改动单链 PID 主实现，避免把“控制律问题”和“评分器问题”混在一起。

## 2026-04-24 提速并关闭自动停车

### 关键证据
- 当前 `TRACK` 默认目标速度仍是 `40.0f`，并且大偏差时基础 PWM 会被压到 `220`，整体手感偏保守。
- 自动停车链并不在 `main.c` 主状态机里主动判停，而是在 [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 内部通过“交叉计数达到阈值 -> `autoFlag` 置停 -> `LineTrack_Update()` 把自身打回 idle”实现。
- 这条链已经和当前单链 PID 主控制脱节，继续保留只会让车在正常循迹过程中被内部状态机意外停下。

### 判断
- 提速应优先改默认速度档和大偏差下的基础推进下限，不需要碰主 `PD`。
- 自动停车应整条删除，而不是只改单个阈值，否则后面仍会保留半废弃停机状态。

### 本轮修正
- `PID_TRACK_SPEED_TARGET: 40.0 -> 44.0`
- `TRACK_FOLLOW_BASE_MIN_PWM: 220 -> 250`
- `TRACK_DEFAULT_CROSSINGS: 4 -> 0`
- 删除 `line_track` 内部的 `autoFlag` 停机链和交叉计数到点自动停逻辑
- 保留 `LineTrack_Start(uint8_t crossings)` 接口名，但 `crossings` 仅为兼容占位，不再参与停车

## 2026-04-24 exp266 S 弯限速分析

### 关键证据
- [`exp_0266_20260424_064949_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0266_20260424_064949_KEY_T.txt) 里，前段 `SCRV` 区间虽然状态是 `SCRV`，但 `pc` 从约 `98` 逐步爬到 `223`，说明起步阶段本身就偏慢。
- 更重的拖速发生在 `TRMR/EDGE` 之后：日志里多次出现 `pc` 被压回 `80~150` 区间，然后又重新慢慢爬升，看起来像“每过一个弯点就重新起步”。
- 当前实现中有两条直接相关的链：
  - [`pid_controller.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/pid_controller.c) 里的 `SPEED_RAMP_RATE / SPEED_ENTRY`
  - [`main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 里 `cornerDone` 后把 `speedRampTarget` 重置为接近当前低速的恢复逻辑

### 判断
- `exp266` 的“`S` 弯速度限制太狠”不只是弯中基础 PWM 限速，更主要是：
  - 起步斜坡偏慢
  - 每次找线/转角恢复后都重新从低速恢复
- 这会让 `S` 弯看起来不像连续冲弯，而像“弯后再起步”。

### 本轮修正
- `SPEED_ENTRY: 9.2 -> 12.0`
- `SPEED_RAMP_RATE: 20.0 -> 36.0`
- 新增恢复速度参数：
  - `TRACK_RESUME_SPEED_MIN = 28.0`
  - `TRACK_RESUME_SPEED_BOOST = 8.0`
  - `TRACK_RESUME_SPEED_MAX = 44.0`
- `cornerDone` 后恢复目标改为：
  - `currentSpeed + TRACK_RESUME_SPEED_BOOST`
  - 再用 `TRACK_RESUME_SPEED_MIN / MAX / targetSpeed` 夹紧

## 2026-04-24 exp271 丢线找线偏慢与主 P 偏软

### 关键证据
- [`exp_0271_20260424_065825_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0271_20260424_065825_KEY_T.txt) 前段 `SCRV` 区间并不差，真正的问题从约 `t=460ms` 起连续出现 `sb=0`，随后长时间停留在 `FNDR/FNDL`。
- 找线区间里左右轮输出长期固定在 `OL=240, OR=-150` 或镜像值，说明当前 pivot 找线力度偏小，而且找线确认链起得偏慢。
- 回线后很快再次丢线，说明除了找线慢，重新接管也偏早；边灯一闪就退出搜索，会让车还没回到中心带就重新进入跟线。
- 当前单链参数里：
  - `PID_TRACK_LINE_KP = 9.8`
  - `TRACK_FOLLOW_DEV_RATIO = 0.52`
  - `TRACK_LOST_CONFIRM_TICKS = 3`
  - `TRACK_SEARCH_TURN_PWM_FAST/SLOW = 240/150`
  这一组整体偏保守。

### 判断
- `exp271` 的主因不是“纯粹搜索算法错误”，而是四件事叠在一起：
  - 丢线确认起得慢
  - pivot 找线力度不够
  - 搜索后接管条件过宽
  - 主循迹 `P` 和差速上限略软，导致回线后不够果断，容易再丢
- 这轮最合理的修正不是回到旧分层状态机，而是在现有单链 PID 上把：
  - `P`
  - 差速上限
  - 找线确认
  - 搜索接管门槛
  - 恢复窗口方向黏性
  一起提到更适合 `S` 弯的档位。

### 本轮修正
- `PID_TRACK_LINE_KP: 9.8 -> 11.0`
- `TRACK_FOLLOW_DEV_RATIO: 0.52 -> 0.58`
- `TRACK_FOLLOW_DEV_STEP_LIMIT: 28 -> 34`
- `TRACK_LOST_CONFIRM_TICKS: 3 -> 2`
- `TRACK_LOST_FAST_CONFIRM_TICKS: 2 -> 1`
- `TRACK_SEARCH_BLIND_TICKS: 1 -> 0`
- `TRACK_SEARCH_ARC_PWM_FAST/SLOW: 220/140 -> 240/160`
- `TRACK_SEARCH_TURN_PWM_FAST/SLOW: 240/150 -> 290/180`
- `TRACK_RECOVER_TICKS: 8 -> 10`
- `line_track.c`
  - 在 `recoverTicks` 窗口内再次丢线时，优先沿 `recoverDir` 继续找线
  - `track_search_exit_ready()` 改成只在中心带重新见线时退出搜索，不再扫到外侧边灯就立刻接管

## 2026-04-24 exp277 主 P 仍偏软且找线退出过慢

### 关键证据
- [`exp_0277_20260424_070545_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0277_20260424_070545_KEY_T.txt) 显示，当前 `S` 弯前半段可以正常循迹，但进入 `EDGE` 后仍然容易因为主链约束不足而打到 `FNDL/FNDR`。
- 日志中多处出现：
  - `EDGE` 时 `bd=4`、`lp≈175~190`，但输出仍不足以把车快速拉回中线；
  - 进入搜索后长时间保持 `OL=320, OR=-200` 或镜像值，说明 pivot 虽增强，但退出条件仍拖慢恢复。
- 更关键的是：在搜索过程中已经重新扫到同侧外缘线，例如 `sb=3`，但状态仍持续停留在 `FNDL`，说明“只认中心带退出搜索”的门槛过严，造成恢复链过慢。

### 判断
- `exp277` 这轮不是单纯“找线 PWM 不够”，而是：
  - 主 `P` 和差速上限仍偏软；
  - 搜索退出从“过早”改成“只认中心带”后，现在又偏慢；
  - 更合理的链路应是：
    - 中心/内侧立即退出搜索；
    - 同侧外缘稳定连续若干拍后退出搜索；
    - 避免一闪就退，也避免明明已经重新扫到线还继续原地拖。

### 本轮修正
- `PID_TRACK_LINE_KP: 11.0 -> 12.2`
- `TRACK_FOLLOW_DEV_RATIO: 0.58 -> 0.62`
- `TRACK_FOLLOW_DEV_STEP_LIMIT: 34 -> 38`
- `TRACK_SEARCH_TURN_PWM_FAST/SLOW: 290/180 -> 320/200`
- `TRACK_RECOVER_TICKS: 10 -> 12`
- 新增 `TRACK_SEARCH_SIDE_EXIT_TICKS = 2`
- `line_track.c`
  - 新增 `searchSeenTicks`
  - 将搜索重获线判断改成三类：
    - `0`: 未重获
    - `1`: 同侧外缘，需稳定累计
    - `2`: 中心/内侧/交叉，立即退出
  - 这样外侧同侧线连续两拍即可退出搜索并交还给单链 `PD`，不再拖到中心带才放手

## 2026-04-24 exp284/285 无故出线根因

### 关键证据
- [`exp_0284_20260424_071315_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0284_20260424_071315_KEY_T.txt) 和 [`exp_0285_20260424_071323_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0285_20260424_071323_KEY_T.txt) 都在正常 `SCRV/TRM` 区间里出现了短暂 `sb=0`，随后立即进入 `FNDL/FNDR`。
- `exp284` 在 `t=160ms` 仍是稳定 `SCRV`，到 `t=180ms` 只因一拍 `sb=0` 就进 `FNDL`。
- `exp285` 在 `t=120ms` 出现一次 `sb=0`，到 `t=140ms` 就进 `FNDL`；说明 `TRACK_LOST_CONFIRM_TICKS=2`、`TRACK_LOST_FAST_CONFIRM_TICKS=1` 已经过于激进。
- 更严重的是，`exp285` 在搜索中已经重新见到 `sb=192` 这类反侧外缘线，但状态仍长时间保持 `FNDL`。这证明上一轮新增的：
  - `recoverDir` 优先选方向
  - 搜索退出只认同侧/中心
  两条链叠起来后，会把错误方向搜索锁死。

### 判断
- 这次“无缘无故就出去了”不是单纯 `P` 太大，而是上轮为了提速恢复引入了新的状态耦合：
  - 瞬时全灭过快触发搜索
  - 错误方向搜索一旦进入，就会因为只认同侧退出而持续越找越远
- 所以修正重点不是再调硬，而是先把这条错误触发链拆掉。

### 本轮修正

## 2026-04-24 8路4051 + 4路直连 升级为12路输入链

### 关键证据
- 当前硬件并不是纯 `8` 路模块，而是 `12` 路循迹前端，只是板上此前只装了 `A3~A10` 并通过 `74HC4051` 扫描。
- 现有工程里 8 路假设同时出现在三层：
  - 固件输入层：[`sensor_fusion.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/sensor_fusion.c)
  - 主控制层：[`line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h)、[`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - PC 侧工具链：[`track_adaptive_tuner.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/track_adaptive_tuner.py)、[`config.yaml`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/config.yaml)
- 如果只改其中一层，会出现：
  - 板端 `12` 位、脚本还按 `8` 位掩码评分
  - 板端支持 `TS12`，脚本仍只能发到 `TS8`
  - 板端中心对已变成 `A6/A7`，脚本仍按旧 `S4/S5` 口径统计中心占比

### 判断
- 最干净的切口不是再堆一层“12 路兼容帧”，而是直接把整条输入链统一成：
  - `A1/A2/A11/A12` 直连 GPIO
  - `A3~A10` 继续走 `74HC4051`
  - MCU 内部统一合成为 `uint16_t bits12`
- 12 路位序必须按物理从左到右固定为 `bit0=A1 ... bit11=A12`，否则后续 `linePos`、掩码、调参脚本都会继续耦合混乱。

### 本轮修正
- [`config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h)
  - `LINE_SENSOR_COUNT: 8 -> 12`
  - 新增直连引脚：
    - `A1 -> PA12`
    - `A2 -> PB11`
    - `A11 -> PB15`
    - `A12 -> PC13`
  - 扩展 `TRACK_LINE_POS_S1..S12`
- [`sensor_fusion.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/sensor_fusion.h)
  - `LineSensor_Data_t.bits` 从 `uint8_t` 升为 `uint16_t`
- [`sensor_fusion.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/sensor_fusion.c)
  - 新增 4 路直连初始化与读取
  - 保留 `74HC4051` 扫描 `A3~A10`
  - 合成为 `bit0~bit11 = A1~A12`
- [`line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h)
  - 掩码体系整体升级为 `12` 位
  - 中心对改为 `A6/A7`
- [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - 所有 `bits` 主链改成 `uint16_t`
  - 位置拟合、找线、交叉、遥测和参数列表联动升级为 `12` 路
- [`bsp_uart.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/bsp_uart.h)、[`bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/bsp_uart.c)
  - 遥测里的 `sensorBits` 升为 `uint16_t`
- [`main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c)
  - `TS` 命令支持 `TS1..TS12`
  - `turnback` 的 `sb` 输出改成 `0x%04X`
- [`track_adaptive_tuner.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/track_adaptive_tuner.py)
  - 中心掩码改为 `0x0060`
  - 外侧掩码改为 `0x0F0F`
  - `track.sensor_scale1..12` 命令生成与回读统一支持
- [`config.yaml`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/config.yaml)
  - `line_sensor.positions_m` 扩成 `12` 项
  - 自适应调参参数面扩成 `sensor_scale1..12`
  - `fit / recovery` 阶段参数选择联动到 12 路

### 验证
- 重新编译 [`project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)，[`project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 为 `0 Error(s), 0 Warning(s)`
- 已按 `pyOCD list -> erase -> load -> reset` 烧录到探头 `031305620164`
- `COM18` 串口口仍被外部进程占用，因此本轮未完成 `TS12` 回读烟测；这不是烧录失败，而是串口验证被占用阻塞

## 2026-04-24 转角控制链收成 FOLLOW / SEARCH / RECOVER 三段

### 关键证据
- 当前代码虽然名义上有 `SEARCH_PHASE_ARC/PIVOT`，但实际入口 [`track_enter_search()`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 已经总是直接把 `searchPhase` 置到 `PIVOT`，`ARC` 只剩死分支。
- 真正的反应慢点不在 `SEARCH`，而在：
  - `FOLLOW` 大偏差时仍只靠普通 `PD`
  - `SEARCH` 退出后虽然会置 `recoverDir/recoverTicks`，但恢复期没有“继续同向收回”的硬约束
- 这会让车出现：
  - 还看得见外侧线时不够果断
  - 刚重见线就很快完全交回普通 `PD`
  - 导致“回来了但没收紧，又被甩出去”

### 判断
- 如果只是把找线改成单向 `pivot`，改善有限；因为问题主因不只在“扫空”。
- 更有效的切法是把强转前移到 `FOLLOW`，并把 `RECOVER` 变成显式的同向收回链。
- 所以这轮控制逻辑应收成三段：
  - `FOLLOW`：小偏差普通 `PD`
  - `FOLLOW`：大偏差且同侧明显占优时，直接同向强化转入
  - `SEARCH`：只有全灭才进入，且单向 `pivot`
  - `RECOVER`：重新见到同侧线后，继续顺着该方向收回中心

### 本轮修正
- [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - 新增 `track_pick_visible_dir()`
  - 新增 `track_is_turnin_follow_case()`
  - 新增 `track_apply_follow_guidance()`
  - 在 `track_drive_follow()` 中把同向强化前移到普通 `PD` 之后、限幅之前
  - 在恢复窗口内，优先按 `recoverDir` 继续同向收回，而不是立刻完全交回普通 `PD`
  - `recoverTicks` 现在优先在“回中心”时清零，而不是单纯按时间提前放手
- [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)、[`line_track.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.h)、[`config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h)
  - 删除 `TRACK_SEARCH_ARC_*` 宏和运行时 `searchArcPwm*`
  - 删掉 `ARC` 进 `PIVOT` 的阶段切换分支
  - `SEARCH` 固化为单向 `pivot` 兜底链
- [`bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/bsp_uart.c)
  - 串口遥测不再尝试输出已删除的 `ARC` 阶段

### 验证
- 重新编译 [`project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)，[`project.build_log.htm`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Objects/project.build_log.htm) 为 `0 Error(s), 0 Warning(s)`
- 已按 `pyOCD list -> erase -> load -> reset` 烧录到探头 `031305620164`
- `COM18` 仍被外部进程占用，最小 `#STAT!` 烟测未能回读
- `PID_TRACK_LINE_KP: 12.2 -> 11.4`
- `TRACK_FOLLOW_DEV_RATIO: 0.62 -> 0.58`
- `TRACK_FOLLOW_DEV_STEP_LIMIT: 38 -> 34`
- `TRACK_LOST_CONFIRM_TICKS: 2 -> 3`
- `TRACK_LOST_FAST_CONFIRM_TICKS: 1 -> 2`
- `TRACK_SEARCH_TURN_PWM_FAST/SLOW: 320/200 -> 300/190`
- `TRACK_RECOVER_TICKS: 12 -> 10`
- `line_track.c`
  - 去掉 `recoverDir` 在 `track_pick_search_dir()` 里的最高优先级，改回优先相信最近有效线历史和最近差速方向
  - `track_search_reacquire_class()` 改成：
    - 中心/内侧/交叉：立即退出
    - 任一外侧稳定两拍：允许退出
  - 这样既不会回到“一闪就退”，也不会因为方向判错而在错误方向上锁死搜索

## 2026-04-24 exp287 跟线不积极

### 关键证据
- [`exp_0287_20260424_072003_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0287_20260424_072003_KEY_T.txt) 的普通 `SCRV` 区间里，`sb=12/48`、`lp≈55`、`bd=±1` 时输出差速约 `14`，这部分并不异常，说明中心附近小偏差主链没有完全失效。
- 真正偏软的是 `EDGE` 区间：日志中多次出现 `sb=3/192`、`lp≈191`、`bd=±4`，但输出只有类似 `OL=245, OR=297` 或 `OL=297, OR=245`，差速仍偏小，导致车在大偏差时回中不够积极。
- 当前参数组为：
  - `PID_TRACK_LINE_KP = 11.4`
  - `TRACK_FOLLOW_ERROR_SCALE = 80.0`
  - `TRACK_FOLLOW_DEV_RATIO = 0.58`
  - `TRACK_FOLLOW_DEV_STEP_LIMIT = 34`
  这一组叠在一起，会把单链 `PD` 的回中力度压得偏保守。

### 判断
- 这次用户说“是不是 `P` 小了”，方向基本是对的，但不只是单纯 `KP` 小。
- 更准确的根因是：单链主 `P` 偏软，同时 `errorScale`、`devRatio`、`stepLimit` 也一起限制了大偏差状态下的输出。
- 因为 `exp284/285` 刚修好误入搜索链，所以本轮不应再碰搜索方向逻辑；最安全有效的做法是只加强主循迹链。

### 本轮修正
- `PID_TRACK_LINE_KP: 11.4 -> 12.6`
- `TRACK_FOLLOW_ERROR_SCALE: 80.0 -> 72.0`
- `TRACK_FOLLOW_DEV_RATIO: 0.58 -> 0.62`
- `TRACK_FOLLOW_DEV_STEP_LIMIT: 34 -> 40`
- 搜索确认、找线方向、搜索退出逻辑全部保持不变，避免把 `exp284/285` 的问题重新带回来

## 2026-04-24 exp289 抓线力仍偏弱

### 关键证据
- [`exp_0289_20260424_073012_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0289_20260424_073012_KEY_T.txt) 里，普通 `SCRV` 区间 `sb=12/48`、`lp≈56` 时输出差速大约 `16~18`，这部分仍属可接受。
- 真正的问题在 `EDGE/TRMR`：多处出现 `sb=192`、`lp≈185~191`、`bd=4`，但输出只有类似 `OL=290, OR=226` 到 `OL=310, OR=246`，等价单边差速约 `32`。
- 结合当前公式：
  - `error = (linePos - deadband) / errorScale`
  - `output = kp * error + kd * d`
  在 `linePos≈191` 时，即使 `dev_ratio` 还没打满，`PD` 本体输出也只在三十多，说明当前主限制点就是 `kp / deadband / errorScale` 这一组，而不是搜索链或差速上限。

### 判断
- `exp289` 里的“抓线力不够”已经不是“找线太慢”的问题，而是单链主循迹在大偏差时的输出强度仍偏保守。
- 由于 `exp284/285` 刚修好误入搜索，因此这轮继续遵守边界：只强化单链主 `PD`，不去改搜索确认、搜索方向、搜索退出。

### 本轮修正
- `PID_TRACK_LINE_KP: 12.6 -> 13.8`
- `TRACK_FOLLOW_DEADBAND: 8.0 -> 6.0`
- `TRACK_FOLLOW_ERROR_SCALE: 72.0 -> 64.0`
- `TRACK_FOLLOW_DEV_RATIO: 0.62 -> 0.64`
- `TRACK_FOLLOW_DEV_STEP_LIMIT: 40 -> 44`
- 搜索链相关参数保持不变

## 2026-04-24 exp293 跟线仍不积极且回正找线偏慢

### 关键证据
- [`exp_0293_20260424_073421_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0293_20260424_073421_KEY_T.txt) 里，`EDGE/TRMR` 段仍长期出现：
  - `lp≈188~191`
  - `bd=±4`
  - 输出大约 `OL/OR = 310/228` 或 `233/313`
  说明大偏差回中仍偏软。
- 同一日志里，找线恢复虽然方向基本对，但 `FNDL/FNDR -> TRML/TRMR` 常要多拖一拍，原因不是 pivot 不动，而是当前 `track_search_reacquire_class()` 对左右外侧一视同仁，为避免误退只好配 `TRACK_SEARCH_SIDE_EXIT_TICKS=2`，结果“同侧已经重新见线”时也被迫多等。
- 这条结构问题在代码里是明确的：[`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 原实现里 `track_search_reacquire_class(uint8_t bits, uint8_t dir)` 实际忽略了 `dir`，只要任一外侧亮都算 `class 1`。

### 判断
- `exp293` 的问题已经拆成两条：
  - 主循迹单链 `PD` 仍偏软
  - 找线回正的同侧重获线退出链过慢
- 如果只继续加 `P`，找线回正还是会拖；如果只改搜索退出，不够硬的主链又会很快重新掉回搜索。
- 因此这轮需要同时改两条最短链路，但仍不碰搜索方向选择逻辑。

### 本轮修正
- `PID_TRACK_LINE_KP: 13.8 -> 15.0`
- `TRACK_FOLLOW_DEADBAND: 6.0 -> 4.0`
- `TRACK_FOLLOW_ERROR_SCALE: 64.0 -> 58.0`
- `TRACK_FOLLOW_DEV_RATIO: 0.64 -> 0.66`
- `TRACK_FOLLOW_DEV_STEP_LIMIT: 44 -> 48`
- `TRACK_SEARCH_TURN_PWM_FAST/SLOW: 300/190 -> 320/210`
- `TRACK_SEARCH_SIDE_EXIT_TICKS: 2 -> 1`
- `track_search_reacquire_class()` 改为真正按 `searchDir` 判定：
  - 中心/内侧/交叉：立即退出
  - 同侧外缘：计为可退出
  - 反侧外缘：不计退出，继续搜索

## 2026-04-24 exp305 跟线还不够及时

### 关键证据
- [`exp_0305_20260424_074215_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0305_20260424_074215_KEY_T.txt) 的 `EDGE/TRMR` 段里，`lp≈180~191`、`bd=±4` 时输出虽然已经比更早版本大，但仍常表现为“慢慢爬上去”，不像用户想要的那种更早、更果断地回中。
- 代码链上更像是“迟滞叠加”而不是单纯 `P` 不够：
  - `TRACK_LINE_POS_STEP = 45`
  - `TRACK_FOLLOW_POS_LPF_ALPHA = 0.60`
  - `TRACK_FOLLOW_D_LPF_ALPHA = 0.38`
  - `TRACK_FOLLOW_DEV_STEP_LIMIT = 48`
  这一组会同时限制 `linePos` 跟随速度、误差导数刷新速度和差速输出爬升速度。
- 搜索方向链在本轮日志里不是第一主因。`exp305` 主要抱怨是“跟线不够及时”，不是“方向找错”。

### 判断
- 这次不应再先碰搜索方向逻辑，否则容易把 `exp284/285` 刚修好的误入搜索链带坏。
- 更准确的处理方式是：保持单链结构不变，只把主循迹响应提速，让位置误差、导数和差速都更快跟上真实线位变化。

### 本轮修正
- `PID_TRACK_LINE_KP: 15.0 -> 16.2`
- `TRACK_LINE_POS_STEP: 45 -> 60`
- `TRACK_FOLLOW_POS_LPF_ALPHA: 0.60 -> 0.74`
- `TRACK_FOLLOW_D_LPF_ALPHA: 0.38 -> 0.46`
- `TRACK_FOLLOW_DEV_STEP_LIMIT: 48 -> 60`
- `TRACK_SEARCH_TURN_PWM_FAST/SLOW: 320/210 -> 340/220`
- 其余搜索确认、搜索方向、搜索退出条件保持不变

## 2026-04-24 找线自转偏慢

### 关键证据
- 当前搜索驱动在 [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 的 `track_drive_search()` 中分成两段：
  - `ARC`：同向前进偏转
  - `PIVOT`：一侧反转原地找线
- 用户本轮明确抱怨的是“自转找线的时候太慢”，对应的是 `PIVOT` 分支，而不是普通 `ARC` 扫线或主循迹本身。
- 当前配置里：
  - `TRACK_SEARCH_TURN_PWM_FAST = 340`
  - `TRACK_SEARCH_TURN_PWM_SLOW = 220`
  这会直接决定 `FNDL/FNDR` 状态下原地摆头的角速度。

### 判断
- 这次最小正确改法不是再碰主循迹 `PD`，也不是去改丢线确认或搜索退出。
- 最干净的处理边界是：只提高 `PIVOT` 自转找线 PWM，让丢线后摆头更快。

### 本轮修正
- `TRACK_SEARCH_TURN_PWM_FAST: 340 -> 380`
- `TRACK_SEARCH_TURN_PWM_SLOW: 220 -> 250`
- `ARC` 搜索参数、主循迹参数、丢线判定参数保持不变

## 2026-04-24 exp404 抓线不强但不降速

### 关键证据
- [`exp_0404_20260424_091906_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0404_20260424_091906_KEY_T.txt) 里，多段 `EDGE/TRMR` 已经出现 `lp≈150~293` 的大偏差，但输出长期停在近似固定的分裂档位，例如 `355/145` 或 `145/355`，并没有随着偏差继续明显加力。
- 当前 [`track_apply_follow_guidance()`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 只提供固定同向抓线下限：
  - `FOLLOW` 固定 `0.42 / 72`
  - `RECOVER` 固定 `0.48 / 88`
- 这会让同向抓线在大偏差时过于“平”，表现成：已经到外侧了，但抓线力度不像偏差那样继续增强，最后更容易掉到 `sb=0` 再进搜索。

### 判断
- `exp404` 的主因不是速度档不够低，也不是本轮要靠继续压基础 PWM 换稳定。
- 最合适的切口是只强化 `FOLLOW/RECOVER` 内部的同向抓线，让它随 `abs(linePos)` 平滑增强，并把强化前移到“同侧外缘已经明显占优”的区间。
- 这样可以在不降速的前提下，让外侧大偏差更早、更硬地往中线抓回。

### 本轮修正
- [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - 新增 `track_build_guidance_severity()`，按 `abs(linePos)` 构建 `0~1` 抓线强度，并在命中同侧外缘灯时额外上调。
  - `track_is_turnin_follow_case()` 改为：
    - `absPos < SMALL_MAX` 不强化
    - `SMALL_MAX ~ MEDIUM_MAX` 只有命中同侧外缘时才提前强化
    - `>= MEDIUM_MAX` 继续按大偏差强化
  - `track_apply_follow_guidance()` 改成随偏差增强：
    - `FOLLOW`: `ratio 0.44 -> 0.58`, `min 80 -> 124`
    - `RECOVER`: `ratio 0.52 -> 0.62`, `min 96 -> 136`
- `PID_TRACK_SPEED_TARGET`、`TRACK_FOLLOW_BASE_MIN_PWM` 与基础速度链保持不变。
