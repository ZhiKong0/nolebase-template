# Task Plan: 全局关闭 IMU 初始化

## Goal
把 `IMU` 在当前工程里彻底关掉，确保上电不初始化、主循环不更新、OLED 不再显示 IMU 初始化页，同时不破坏直线、循迹和基础串口状态页。

## Current Phase
Phase 4

## Phases

### Phase 1: 定位 IMU 入口
- [x] 确认 `BNO085_Init()`、`run_imu_update()`、`BNO085_ResetAttitude()`、`#IMU?!` 的调用链
- [x] 确认 `OLED` 仍会在 `!BNO085_IsReady()` 时显示 IMU 诊断
- **Status:** complete

### Phase 2: 实现统一关停开关
- [x] 在 [`Hardware/config.h`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/config.h) 增加 `IMU_ENABLE`
- [x] 在 [`Hardware/sensor_fusion.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/Hardware/sensor_fusion.c) 为 `BNO085_*` 提供禁用分支
- [x] 在 [`User/main.c`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/User/main.c) 切断启动初始化、周期更新和 IMU 显示入口
- **Status:** complete

### Phase 3: 编译、烧录、串口验证
- [x] 编译 [`project.uvprojx`](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_track_fisheye/project.uvprojx)
- [x] 通过 `pyOCD` 完成 `erase -> load -> reset`
- [x] 串口验证 `#IMU?!` 和 `#STAT!`
- **Status:** complete

### Phase 4: 记录结果并收口
- [x] 更新 `task_plan.md`
- [x] 更新 `findings.md`
- [x] 更新 `progress.md`
- **Status:** complete

## Decisions Made
- 本轮采用编译期开关 `IMU_ENABLE=0`，而不是继续修 `BNO085` 初始化链。
- 禁用后保留 `BNO085_*` 接口桩，避免主循环和命令路径大量改签名。
- 烧录阶段继续使用 `pyOCD`，但对当前 `Horco CMSIS-DAP` 需要加 `PYOCD_CMSIS_DAP_LIMIT_PACKETS=1` 才稳定。

## Hot Files
- `Hardware/config.h`
- `Hardware/sensor_fusion.c`
- `User/main.c`
