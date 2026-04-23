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
