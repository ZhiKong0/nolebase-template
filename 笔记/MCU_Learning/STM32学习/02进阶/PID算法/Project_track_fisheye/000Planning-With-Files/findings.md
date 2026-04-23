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
