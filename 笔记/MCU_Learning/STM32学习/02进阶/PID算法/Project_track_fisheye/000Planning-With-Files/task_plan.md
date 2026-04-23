# Task Plan: 误入找线修正与 IMU 失败显示收口

## Goal
先压掉直线段误入 `FNDL/FNDR` 的状态机问题，再处理 `IMU:3` 时 OLED 被诊断页独占的问题，保证后续可以继续正常看状态并复测。

## Current Phase
Phase 4

## Phases

### Phase 1: 复盘误左转链路
- [x] 读取 `exp_0257`
- [x] 对照 `line_track.c` 的丢线进入/退出链
- [x] 确认根因是“误丢线 -> 右找线短退 -> 恢复期再次丢线 -> 左找线锁死”
- **Status:** complete

### Phase 2: 重构防误入找线链路
- [x] 提高中心附近全灭进入找线的确认阈值
- [x] 收紧找线退出条件，禁止扫到反侧一闪就退出
- [x] 在恢复窗口内优先沿上次找线方向继续
- [x] 防止恢复期对侧瞬时位型重写方向历史
- **Status:** complete

### Phase 3: 编译烧录并验证串口
- [x] 编译 `project.uvprojx`
- [x] 通过 `pyOCD` 擦写并复位
- [x] 串口验证 `#STAT!/#MODE=TRACK!` 正常
- **Status:** complete

### Phase 4: IMU 失败显示收口
- [x] 读取 `#IMU?!`，确认当前仍是 `IMU=3 fail=5`
- [x] 判断最近补丁未触碰 `sensor_fusion`，不是新的 IMU 回归
- [x] 调整 OLED idle 显示策略：即使 IMU 未 ready，仍显示主状态页，仅第 4 行保留 IMU 诊断
- [x] 重新编译烧录并恢复 `experiment_logger`
- **Status:** complete

## Decisions Made
- 当前不继续深挖 `BNO085 product id` 握手，只先解决“显示卡在 IMU 诊断页”。
- 直线段防误入找线优先改状态机，不先继续硬推 `PID`。
- `experiment_logger` 保持常驻接回 `COM18`。

## Hot Files
- `Hardware/config.h`
- `Hardware/line_track.h`
- `Hardware/line_track.c`
- `User/main.c`
