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

## 2026-04-24 起跑一致性与8秒评分窗口

### 关键证据
- `exp474` 仍是当前最可信的高分基线；在将评分窗口起点统一改为 `400ms` 后，重新评分结果为：
  - `total_score = 89.53`
  - `a67_cover_ratio = 99.18%`
  - `a67_exact_ratio = 81.47%`
- 用户手动摆正后，板端 `#LINE?!` 一度可读到：
  - `sbh=0x0C0`
  - `s12=000000110000`
  - `lp=0.5`
  说明“复位后立即读线”这条链可用。
- 但随后串口进入无输出态，必须执行 `pyocd reset` 才能重新恢复空闲心跳和命令响应。
- 这次直接抓取的 `exp_capture_20260424_194051_UART_T.txt` 是中途附着到已运行实验上的，不是从 `EVT:EXP_START` 前完整采到，因此不能拿来和 `exp474` 做参数优劣比较。

### 判断
- 当前最大阻塞点不是 `P/D` 再怎么拧，而是：
  - 无线串口链偶发失联，需 `reset` 后恢复
  - 起跑姿态仍需以 `#LINE?!` 做一次闸门确认
  - 不完整实验文件不能进评分闭环
- 因此后续复测必须遵守：
  1. `pyocd reset`
  2. `#LINE?!` 确认起跑线型
  3. 再发送 `#RUN!`
  4. 从 `EVT:EXP_START` 开始完整落盘

### 本轮修正
- [`experiment_score_watch.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_score_watch.py)
  - 新增 `--analysis-start-ms`
  - 默认从 `400ms` 后开始评分，避免把起跑瞬态直接算进总分
  - 报告新增 `analysis_start_ms`
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

## 2026-04-24 exp422 直线稳定性被恢复窗口打坏

### 关键证据
- [`exp_0422_20260424_093105_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0422_20260424_093105_KEY_T.txt) 在多处出现相同模式：
  - 先 `FNDR/FNDL`
  - 然后 `TRMR/TRML` 阶段已经重新见到中心带附近位型
  - 但输出仍沿旧 `recoverDir` 强打，形成 `311/91`、`417/123`、`480/167` 这类明显单向拉扯
  - 随后直线段短暂 `sb=0`，又重新掉回 `FNDL/FNDR`
- 最直接的证据在：
  - `t=200~260ms`：`st=TRMR`, `sb=48`, `lp≈-50`，说明线位已经偏到左侧，但输出仍持续强右打
  - `t=1080~1160ms`：`st=TRMR`, `sb=192/48`, `lp≈20~50`，已经接近中心，输出仍保持 `480/185` 到 `480/167`
- 这说明根因不是直线速度档过高，而是 `RECOVER` 只靠 `recoverTicks` 计时放手，缺少“方向已经反了就立刻放手”的条件。

### 判断
- `exp404` 引入的大偏差抓线增强本身不是这次直线失稳的第一根因，不能整条回退。
- 真正要修的是 `RECOVER` 的放手条件：
  - 当重见线已经跨回中心带
  - 或已明显偏到 `recoverDir` 的反侧
  - 就不应继续沿旧方向施加恢复抓线
- 这轮要做的是只回收 `RECOVER` 的方向黏性，保留 `FOLLOW` 的大偏差增强。

### 本轮修正
- [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - 新增 `track_should_hold_recover_guidance()`，统一判断恢复窗口是否还应该继续握住旧方向。
  - 规则改成：
    - `bits==0` 或交叉态时允许继续保留恢复方向
    - 一旦重新见线已回到中心锁定，立即清掉 `recoverDir/recoverTicks`
    - 一旦 `visibleDir` 已与 `recoverDir` 相反，且已经进入 `CENTER_BAND` 或 `abs(linePos) <= MEDIUM_MAX`，立即放手交还普通 `PD`
  - `track_apply_follow_guidance()` 只在 `track_should_hold_recover_guidance()==1` 时才继续施加恢复抓线下限
  - `track_signal_update()` 中的恢复窗口清理也改为使用同一判定，避免控制链与状态链分裂
- `FOLLOW` 的大偏差抓线增强保持不变，速度档与基础 PWM 不变。

## 2026-04-24 exp432 直线略晃且 S 弯抓线偏弱

### 关键证据
- [`exp_0432_20260424_094239_KEY_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0432_20260424_094239_KEY_T.txt) 显示两类症状同时存在：
  - 直线/近中心区时，`lp≈28~72` 这类小偏差仍在持续左右修正，说明中心附近单链误差偏敏感
  - `S` 弯/外侧区时，`lp≈145~150` 进入 `SCRV` 后仍会拖到更外侧，说明大偏差阶段主误差又不够硬
- 当前 `track_build_follow_error()` 仍是纯线性：
  - `error = deadband(linePos) / errorScale`
  - 小偏差和大偏差只是按同一比例尺线性进入同一套 `PD`
- 在这种口径下，单链 `PD` 很难同时满足：
  - 直线中线附近不要太敏感
  - `S` 弯外侧偏差要更早、更积极地抓回

### 判断
- `exp432` 不是单纯 `KP` 该再大或再小，而是单链误差映射本身太线性。
- 如果直接降 `KP`，直线会更稳，但 `S` 弯抓线会更弱。
- 如果直接加 `KP`，`S` 弯会更积极，但直线抖动会更明显。
- 更合适的修法是在不破坏“单链 PID”结构的前提下，把 `linePos -> error` 改成连续非线性整形：
  - 中心小偏差压小
  - 外侧大偏差放大

### 本轮修正
- [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - 新增 `track_shape_follow_error()`
  - 将单链误差从纯线性改成连续三段整形：
    - `<= SMALL_MAX`：`scale = 0.78`
    - `SMALL_MAX ~ MEDIUM_MAX`：连续过渡到 `1.00`
    - `MEDIUM_MAX ~ LARGE_MAX`：连续过渡到 `1.18`
  - `track_build_follow_error()` 先做 `deadband`，再做 `track_shape_follow_error()`，最后再按统一 `errorScale` 归一
- 速度档、基础 PWM、`FOLLOW/RECOVER` 的大偏差抓线增强链全部保持不变。

## 2026-04-24 回退到 ddc2eb7

### 关键结论
- 用户要求回到 `ddc2eb7` 的主循迹基线，目标是恢复当时更贴近“车的投影在线上”的行为。
- 实际需要回退的功能文件只有 [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)。
- `config.h`、`sensor_fusion.c`、`main.c` 等在 `ddc2eb7..HEAD` 范围内没有形成新的主行为差异，不需要跟着一起回退。

### 证据
- `git diff ddc2eb7 -- Project_track_fisheye/Hardware/line_track.c` 在回退前存在显著行为差异。
- 回退后再比对同一路径，对 `ddc2eb7` 已无剩余差异。

### 本轮处理
- 使用定向 `git restore --source ddc2eb7` 将 [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 恢复到 `ddc2eb7`。
- 没有使用整仓 `reset --hard`，避免影响其它工程和用户未提交改动。
- 构建并重新烧录，当前板上固件主循迹链已回到 `ddc2eb7` 对应实现。

## 2026-04-24 Project_track_fisheye 的 12 路版本定位

### 关键结论
- `Project_track_fisheye` 中“改到 12 路”的明确起点提交是：
  - `ddc2eb7 track: refactor 12-route turn-in and recovery chain`
- 这是 git 历史里第一次同时引入以下 12 路特征的版本：
  - `LINE_SENSOR_COUNT = 12`
  - `TRACK_LINE_POS_S9 ~ S12`
  - `sensor_fusion.h` 中 `LineSensor_Data_t.bits = uint16_t`
  - `line_track.h / line_track.c` 中的 `uint16_t bits` 与 `LT_BIT_S9 ~ LT_BIT_S12`

### 证据
- `git log -S 'LINE_SENSOR_COUNT 12'` 命中 `ddc2eb7`
- `git log -S 'TRACK_LINE_POS_S9'` 命中 `ddc2eb7`
- `git log --grep '12-route|12路'` 命中 `ddc2eb7`

### 后续演化
- `ddc2eb7` 之后，这条 12 路主线又经历了：
  - `c20f11a track: strengthen exp404 line catch without slowing`
  - `32394d0 track: release recover hold earlier for exp422`
  - `d8d4556 track: shape single-chain error for exp432`
- 随后：
  - `b422998 track: revert fisheye line_track to ddc2eb7 baseline`
  只把 [`line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c) 回退到 `ddc2eb7` 基线，但整个工程其余 12 路基础结构仍然保留。

### 当前判断
- 如果你要找“刚刚改到 12 路的原始版本”，用 `ddc2eb7`
- 如果你要找“当前工作树所处的 12 路分支最新落点”，它已经包含到：
  - `b422998`
  只是其中的 `line_track.c` 已回退到 `ddc2eb7` 的行为基线

## 2026-04-24 串口补充 12 路数字电平遥测

### 关键结论
- 当前 `HB:` 心跳里原本只有 `sb=`，它发送的是 `sensorBits` 的十进制整型值。
- 对 12 路版本来说，`sb=` 仍然可用，但肉眼无法直接看出 `S1~S12` 每一路当前高低电平。
- 实验记录器本身不需要改，只要在固件 `HB:` 中追加新字段，`experiment_logger` 就会原样写入文件。

### 本轮修正
- 在 [`Hardware/bsp_uart.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/bsp_uart.c) 新增：
  - `track_sensor_bits_to_text()`
- 在 `BspUart_SendTelemetryTrack()` 中保留原字段：
  - `sb=<decimal>`
- 同时新增两个可读字段：
  - `sbh=0x%03X`
  - `s12=<12位字符串>`

### 字段语义
- `sb`
  - 原兼容字段，十进制位掩码
- `sbh`
  - 同一位掩码的十六进制形式，便于直接看位图
- `s12`
  - 从左到右按 `S1~S12` 展开的 12 位数字电平串
  - 例如 `001100000000` 表示 `S3/S4` 命中，其余位未命中

### 兼容性判断
- 旧脚本仍然可以继续只读 `sb`
- 新的实验日志会额外携带 `sbh/s12`
- `parse_kv_line()` 这类基于 `k=v` 的脚本不会因为新增字段而失效

## 2026-04-24 exp447 S 弯抓线力分析

### 关键结论
- `exp447` 的主问题不是找线 pivot 太慢，而是 `S` 弯外侧 `EDGE` 段的主循迹输出进入平台区后，抓线力不再继续增加。
- 最直接的增强点是主 `FOLLOW` 链：
  - `PID_TRACK_LINE_KP`
  - `TRACK_FOLLOW_ERROR_SCALE`
  - `TRACK_FOLLOW_DEV_RATIO`
  - `TRACK_FOLLOW_DEV_STEP_LIMIT`
- 次一级是中心与滤波响应：
  - `TRACK_FOLLOW_DEADBAND`
  - `TRACK_FOLLOW_POS_LPF_ALPHA`
  - `TRACK_FOLLOW_D_LPF_ALPHA`
- 搜索相关变量只影响“已经出线之后”的恢复，不是 `S` 弯本体抓线力的第一来源。

### 证据
- 在 `exp447` 的 `EDGE` 段，已经出现 `S9~S11` 外侧命中、`lp≈150~175`、`bd=2~4`，但输出差速只维持在一档平台：
  - `OL/OR≈385/157`
  - `OL/OR≈408/166`
  - `OL/OR≈440/180`
- 这说明真正限制抓线力的是主循迹误差缩放、差速上限与步进限幅，而不是搜索速度。

### 作用点
- 主误差构造：[`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - `track_build_follow_error()`
- 主 PD 输出：[`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - `track_dev_speed_follow_pd()`
- 差速上限与同向补强：[`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - `track_drive_follow()`
  - `track_apply_follow_guidance()`

## 2026-04-24 统一串口调参接口重构

### 关键结论
- 当前工程原本已经有 `#TCFG`，但参数 owner 是碎的：
  - `main.c` 直接改 `g_pid`
  - `line_track.c` 自己维护一份 `ParamSet/Get/List`
  - 旧别名命令 `#SPD/#SKP/#AKP/#LKP...` 直接碰业务字段
- 真正的耦合点不是“命令太多”，而是 `UART -> 业务全局变量` 直写链。
- 这轮的切口应该是：
  - `UART/CLI -> 参数服务 -> owner 模块`
  - 而不是继续在 `handle_command()` 里补更多 `if/else`

### 本轮 owner 划分
- `DualLoop` owner：[`Hardware/pid_controller.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/pid_controller.c)
  - 新增 `g_dualLoopCfg`
  - 统一管理：
    - `straight.*`
    - `track.speed_*`
    - `heading.*`
    - `system.speed_*`
- `LineTrack` owner：[`Hardware/line_track.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/line_track.c)
  - 扩展 `g_lineTrackCfg`
  - 统一管理：
    - 12 路传感器缩放
    - 跟线 PD 相关
    - 阈值相关
    - 找线/恢复/交叉相关
    - 同向导向补强相关
    - 出弯恢复速度相关

### 低耦合策略
- `main.c` 现在只做：
  - `runtime_param_get/set/list/load_defaults`
  - 旧短命令到统一 key 的映射
- `main.c` 不再直接写：
  - `g_pid.speedPID.*`
  - `g_pid.headingPID.*`
  - `g_pid.targetSpeed`
  - `g_lineTrackCfg.*`
- 旧短命令仍保留，但已经降级成别名层，不再是业务写入口。

### 新增的一致性约束
- `LineTrack` 参数在 set 后会做归一，避免配置链自相矛盾：
  - `center < small < medium < large`
  - `lost_fast_confirm <= lost_confirm`
  - `search_turn_slow <= search_turn_fast`
  - `recover_turnin >= follow_turnin`
  - `resume_speed_min <= resume_speed_max`

### 当前阻塞
- 编译、烧录已完成
- 串口烟测被 `COM18` 占用阻塞，无法在本轮拿到 `#TCFG PING/LIST/GET/SET` 回包

## 2026-04-24 实验日志并行评分脚本

### 关键结论
- `experiment_logger.py` 已经把每轮实验稳定落成 `exp_*.txt`，因此新的评分脚本不需要抢串口，只要消费新文件即可。
- 对当前 12 路循迹，最适合先固化成 3 个核心指标：
  - 抓线性
  - 速度平滑性
  - `A6/A7` 覆盖比率
- “下一步参数建议”不宜直接抢 `COM18` 自动下发，因为 logger 当前独占串口；更合理的是输出：
  - 本轮评分报告
  - 当前最佳记录
  - 下一步首选/备选参数建议

### 本轮实现边界
- 新增 [`000Project_PC_Control/experiment_score_watch.py`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_score_watch.py)
- 新增 [`000Project_PC_Control/experiment_score_watch.ps1`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Project_PC_Control/experiment_score_watch.ps1)
- 评分器输入：
  - `EVT:EXP_START / EVT:EXP_STOP`
  - `HB:` 中的 `sbh / s12 / lp / st / el / er / pc / OL / OR`
- 评分器输出：
  - `*_score.json`
  - `*_score.md`
  - `leaderboard.csv`
  - `latest_report.md`
  - `watch_state.json`

### 当前评分定义
- 抓线性：
  - 以 `mean_abs_lp`、`loss_ratio`、`search_ratio`、`EDGE` 回中成功率、`SEARCH` 回中成功率综合计算
- 速度平滑性：
  - 只在中心附近或 `STRA/TRK` 区域评估，避免把弯道左右轮差天然存在误罚成 0 分
- `A6/A7` 中线覆盖：
  - `A6 or A7` 覆盖比率
  - `A6 & A7` 双灯精确居中比率

### 建议参数规则
- 搜索/全灭比率过高：
  - 优先 `track.search_turn_fast`
  - 其次 `track.search_turn_slow`
- 中线覆盖偏低且抓线不足：
  - 优先 `track.lkp`
  - 其次 `track.error_scale`
- 还看得到外侧线但抓不回：
  - 优先 `track.follow_turnin_ratio`
  - 其次 `track.follow_turnin_min`
- 中心段翻向频繁或速度不平顺：
  - 优先 `track.lkd`
  - 其次 `track.dev_step_limit`

## 2026-04-24 8秒复测与起跑姿态一致性

### 关键结论
- `COM18` 已恢复空闲，板子在线，当前固件支持紧凑 `#TCFG?=` / `#TCFG=...,...!` 参数接口。
- 上一轮确认最优参数重新下发并回读一致：
  - `track.lkp=16.2`
  - `track.lkd=6.8`
  - `track.follow_turnin_ratio=0.42`
  - `track.follow_turnin_min=72`
  - `track.error_scale=58`
  - `track.dev_ratio=0.66`
  - `track.dev_step_limit=60`
  - `track.search_turn_fast=400`
  - `track.search_turn_slow=240`
  - `track.static_bias=-8`
- 在这组参数下，`exp476` 的复测得分只有 `45.54`，明显低于上一轮 `exp474` 的 `88.02`。
- 继续跑 `exp477~exp479` 微调后，结果仍整体偏差：
  - `exp477` 约 `37.87`
  - `exp478` 约 `47.62`
  - `exp479` 约 `36.94`
- 这组结果不能被解释为“参数瞬间退化”，因为实验起跑姿态已经不一致，评分失去横向可比性。

### 直接证据
- `exp474` 起跑时在 `20ms` 就是 `sbh=0x0C0 / s12=000000110000 / lp=47`，属于贴近中心、可比较的标准起跑姿态。
- `exp476` 起跑时在 `20ms` 变成 `sbh=0x170 / lp=-28`，已经是另一种赛道投影。
- `exp477` 起跑时在 `20ms` 则是 `sbh=0x030 / lp=-47`，更明显偏离标准起跑姿态。
- 说明 `--uart-test-seconds 8` 连续运行后，车停在赛道不同位置；下一轮直接从停下的位置再 `#RUN!`，不再是同一起点实验。

### 判断
- 当前自动复测的主问题不是“电量恢复后参数失效”，而是“没有统一回到同一条起跑线/同一朝向”。
- 因此 `exp476~exp479` 更适合视为“不同起跑姿态下的单轮表现”，不适合作为严格的参数优劣对比。
- 如果继续这样自动串跑，AI 会把“起点漂移”误判成“参数需要继续调”，后续调参会偏离真实最优。

### 当前最可信参数
- 目前仍以 `exp474` 对应参数组为最可信基线：
  - `track.lkp=16.2`
  - `track.lkd=6.8`
  - `track.follow_turnin_ratio=0.42`
  - `track.follow_turnin_min=72`
  - `track.error_scale=58`
  - `track.dev_ratio=0.66`
  - `track.dev_step_limit=60`
  - `track.search_turn_fast=400`
  - `track.search_turn_slow=240`
  - `track.static_bias=-8`

## 2026-04-24 ALIGN 预对中链验证

### 关键结论
- 已在 [`User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 增加 `#ALIGN!` 串口链，目标是在每轮 `8s` 自动实验前先把车的投影收回到 `A6/A7` 附近。
- 当前 `ALIGN` 已经具备：
  - `STOP` 态可触发
  - 读 12 路传感器
  - 看得到线时低速前进对中
  - 看不到线时单向 pivot 搜线
  - 输出 `EVT:ALIGN,START/DONE/FAIL`
- 但现阶段它**还不能稳定把车带到同一个起跑姿态**，因此不能拿来替代人工统一摆放。

### 实测现象
- 第一次原地自转版 `ALIGN`：
  - 出现来回翻向
  - 最终 `EVT:ALIGN,FAIL,sb=0x0000`
- 改成低速前进对中版后：
  - 不再严重左右抽动
  - 但仍会丢线，出现 `EVT:ALIGN,FAIL,sb=0x0000`
- 补上“无信号默认单向搜线”后：
  - `ALIGN 1 -> FAIL, sb=0x0030, lp=-0.5`
  - `ALIGN 2 -> FAIL, sb=0x0700, lp=1.8`
- 两次 `FAIL` 终点位型不一致，说明 `ALIGN` 还未形成稳定收敛点。

### 判断
- 当前阻塞已从“参数未知”转成“起跑一致性缺失”。
- 如果继续在这种条件下自动跑 `8s` 评分，AI 仍会把“起点不同”误判成“参数应该继续调”。
- 因此，现阶段继续找“全局最优参数”已经不可靠；要么：
  - 继续强化 `ALIGN`
  - 要么人工把车每次放回同一起跑位后再继续自动测

## 2026-04-24 人工摆正后继续 8 秒调参

### 关键结论
- 在用户明确要求“先不把起跑位置当唯一主因”的前提下，本轮继续用人工摆正后的姿态做了多组 `8s` 参数试验。
- 新一轮纯加硬 `P / KD / dev_ratio` 并没有明显把问题打掉，最好的一组也只有 `37.76`，远低于 [`exp_0474_20260424_185148_UART_T.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_0474_20260424_185148_UART_T.txt) 的 `89.53`。
- 当前在“这轮人工摆正姿态”下，相对最优的是：
  - [`exp_auto_20260424_195944_cand_f_search440_p168.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_195944_cand_f_search440_p168.txt)
  - `LKP=16.8, LKD=6.8, TDR=0.68, STF/STS=440/280, STB=-8`
  - `score=37.76`

### 直接证据
- 纯加硬主链的两组：
  - `cand_a`: `35.93`
  - `cand_b`: `36.17`
  - 说明把 `LKP/LKD/TDR` 一起抬高，只带来很有限改善。
- 正向静态偏置两组：
  - `cand_c (STB=+8)`: `35.44`
  - `cand_d (STB=+16)`: `28.13`
  - 说明把 `static_bias` 从 `-8` 改成正值并不能解决当前问题，反而会让平滑性进一步恶化。
- 继续提搜索自转：
  - `cand_e (440/280)`: `31.01`
  - `cand_f (440/280 + LKP=16.8 + TDR=0.68)`: `37.76`
  - `cand_g (450/290 + 更细调)`: `35.95`
  - 说明适度提高 `search_turn` 有帮助，但继续加到更激进并不会稳定提升。

### 日志判断
- [`exp_auto_20260424_195944_cand_f_search440_p168.txt`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/000Data/serial_runs/experiments/exp_auto_20260424_195944_cand_f_search440_p168.txt) 的评分结构是：
  - `grip_score ≈ 52.83`
  - `speed_smoothness_score ≈ 58.13`
  - `center_score ≈ 21.02`
  - `search_ratio ≈ 32.98%`
  - `loss_ratio ≈ 36.13%`
- 这说明当前限制点依然不是“看见线以后完全拉不回”，而是：
  - 仍然较频繁地掉进 `SEARCH`
  - 一旦掉进去，整体有效中心覆盖仍上不来
- 同时，`cand_d` 把 `STB` 加到 `+16` 后，`center_score` 虽上升，但 `speed_smoothness_score` 掉到极低，说明这种补偿方向会把直线/恢复链打坏。

### 判断
- 在当前这轮姿态和赛道下，单纯继续加 `P` 或继续推正向 `static_bias` 都不是正确主方向。
- 相对正确的局部趋势是：
  - 保持 `STB=-8`
  - `search_turn` 保持在 `440/280` 左右
  - 主链只做轻微增强，落在 `LKP≈16.8, TDR≈0.68`
- 但这条线仍远没有达到“稳定高速巡中线”，所以它只能视为“当前姿态下的局部最优”，不能替代 `exp474` 作为全局最优。

## 2026-04-24 最稳版本筛选

### 关键结论
- 为了区分“单次偶然高分”和“真正稳定”，本轮对 3 组候选做了重复 `8s` 复测。
- 结果很明确：
  - `base474` 平均分最高，同时波动最小
  - `cand_f` 是次优且比较稳
  - `mid166` 虽然单次能冲到更高分，但波动太大，不适合作为最终稳定版

### 统计结果
- `base474 = 16.2 / 6.8 / 0.66 / 400 / 240 / -8`
  - 有效复测 2 次
  - `avg_total = 39.49`
  - `std_total = 0.29`
  - `stability_score = 39.20`
- `mid166 = 16.6 / 6.8 / 0.67 / 430 / 270 / -8`
  - 有效复测 3 次
  - `avg_total = 36.51`
  - `std_total = 4.40`
  - `stability_score = 32.12`
- `cand_f = 16.8 / 6.8 / 0.68 / 440 / 280 / -8`
  - 有效复测 3 次
  - `avg_total = 36.79`
  - `std_total = 0.82`
  - `stability_score = 35.98`

### 判断
- 当前能同时满足“最好”和“最稳”的，不是最近调出来的更激进组，而是 `base474` 这一组。
- 这也说明：
  - 继续把 `LKP/TDR/search_turn` 往上推，并不会稳定提升
  - 目前更激进的参数只是在部分轮次里“看起来更能抓线”，但整体成功率和稳定性没有超过 `base474`

### 当前最终推荐
- 板上已恢复为：
  - `LKP=16.2`
  - `LKD=6.8`
  - `TDR=0.66`
  - `STF=400`
  - `STS=240`
  - `STB=-8`
- 如果后续继续微调，应以 `base474` 为真基线，不再从 `cand_f` 往更硬方向继续推。
