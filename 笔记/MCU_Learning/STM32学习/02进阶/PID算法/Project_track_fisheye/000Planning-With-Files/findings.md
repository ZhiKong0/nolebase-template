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
