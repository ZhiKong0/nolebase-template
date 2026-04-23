# Task Plan: 单链循迹 PID 与自动调参骨架

## Goal
把 `Project_track_fisheye` 当前 `TRACK` 模式从“分层梯度 + 多组比例/阈值耦合链”重构为“单一连续误差 + 单一循迹 PD + 最小找线状态机”，并在此基础上补齐“不扫目标速度、围绕固定 40 速度档做复测驱动联调”的自动调参骨架。

## Current Phase
Phase 22

## Phases

### Phase 1: 锁定参考实现与重构边界
- [x] 对照 `Project_track_infrared` 的单链实现
- [x] 读取当前 `fisheye` 的 `config.h / line_track.h / line_track.c / main.c`
- [x] 确认需要保留的兼容面：`LineTrack_*` 接口、主循环框架、串口遥测
- **Status:** complete

### Phase 2: 重构 TRACK 主链
- [x] 将 `line_track` 收敛为单一 `linePos -> PD -> devSpeed`
- [x] 删除中心/中间/边缘分层增益与差速梯度
- [x] 保留独立找线/交叉状态机，不把它们混进主控制律
- **Status:** complete

### Phase 3: 清理参数面与命令口
- [x] 清理旧的 `center/mid/edge` 相关宏
- [x] 清理旧的短命令口 `#CSR/#CSM/#CMR/#CMM/#EDR/#EDM/#RCD/#CDB/#OCB/#CHT`
- [x] 保留并统一为 `#TDR/#STB/#TDB/#PLF/#DLF/#RCT/#STF/#STS/#STO`
- **Status:** complete

### Phase 4: 编译验证
- [x] 编译 `project.uvprojx`
- [x] 检查 `bsp_uart` 遥测状态字符串与新状态枚举兼容
- [x] 确认工程 `0 Error(s), 0 Warning(s)`
- **Status:** complete

### Phase 5: 文档收口
- [x] 更新 `task_plan.md`
- [x] 更新 `findings.md`
- [x] 更新 `progress.md`
- **Status:** complete

### Phase 6: 自动调参骨架收口
- [x] 将 `track_adaptive_tuner.py` 扩展为“中心稳定 + S 弯稳定 + 搜线恢复”评分骨架
- [x] 将 `config.yaml` 改成固定 `speed_target=40`、三阶段复测骨架
- [x] 完成 `Python` 语法、`YAML` 结构与 `--help` 入口验证
- [ ] 后续如需实跑，再单独启动自动复测，不在本轮直接扫描
- **Status:** in progress

### Phase 8: 提速与停机链清理
- [x] 提高 `TRACK` 默认速度档
- [x] 提高大偏差时的基础速度下限
- [x] 删除 `line_track` 内部自动停车链
- **Status:** complete

### Phase 9: 编译与记录
- [x] 编译 `project.uvprojx`
- [x] 记录本轮“提速 + 关停机链”结论到规划文件
- **Status:** complete

### Phase 10: exp266 限速定位
- [x] 读取 `exp266` 日志
- [x] 确认 `S` 弯拖速主要来自速度斜坡与恢复速度重置
- **Status:** complete

### Phase 11: S 弯恢复提速
- [x] 提高起步斜坡
- [x] 提高找线/转角退出后的恢复目标
- [x] 保持单链 PD 主控制不变
- **Status:** complete

### Phase 12: 编译与记录
- [x] 编译 `project.uvprojx`
- [x] 将 `exp266` 结论写入规划文件
- **Status:** complete

### Phase 22: exp287 主循迹力度复核
- [x] 读取 `exp287` 日志并区分 `SCRV` 与 `EDGE` 区间
- [x] 确认问题主因是大偏差回中力度不足，而不是搜索方向链本身
- **Status:** complete

### Phase 23: 单链参数强化并烧录
- [x] 提高单链 `KP`
- [x] 降低 `TRACK_FOLLOW_ERROR_SCALE`
- [x] 提高 `TRACK_FOLLOW_DEV_RATIO / STEP_LIMIT`
- [x] 编译并烧录到当前板子
- **Status:** complete

## Decisions Made
- 本轮不在旧 `line_track.c` 上继续打补丁，而是直接以 `Project_track_infrared` 为真源迁移单链主实现。
- 自动调参骨架本轮固定 `speed_target=40`，不在本轮搜索目标速度。
- `TRACK` 主控制只保留一套运行时参数：
  - `sensorScale[8]`
  - `lkp / lkd`
  - `dev_ratio`
  - `deadband`
  - `pos_lpf / d_lpf`
  - `static_bias`
  - `recover_ticks`
  - `search_turn_fast / search_turn_slow / search_timeout`
- 为减少联动面，遥测里暂时保留 `dbgScoreEnabled` 字段，但其语义已简化为“搜索/丢线/交叉是否计分”。

## Hot Files
- `Hardware/config.h`
- `Hardware/line_track.h`
- `Hardware/line_track.c`
- `User/main.c`
- `Hardware/bsp_uart.c`
- `000Project_PC_Control/config.yaml`
- `000Project_PC_Control/track_adaptive_tuner.py`
