# Task Plan

## Goal

在 [Project_Refactor-0415-real](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real) 上继续收敛角点处理逻辑，当前优先级是：

1. 让弯道/角点处理更快、更顺
2. 减少 `R90` 误触发与 `R90->CSR` 坏链
3. 压低 `R90` handoff 后第一帧 `lp` 过大的问题
4. 在多次 `12s` 串口实测下尽量稳定

交叉线 `CRS` 问题暂缓，不是当前主线。

## Current Phase

当前处于：

- `line_track` 角点状态机多轮小修后的**稳定性验证/局部修正阶段**
- 不是大改架构阶段
- 当前源码已经包含：
  - 普通 `TRK` 弯道增强
  - `R90` 入口连续确认与位置阈值
  - `R90` 专属 entry boost / arc / handoff 限幅
  - 统一角点执行器的雏形，但并未完全收敛到单一执行链

## Remaining Phases

### Phase 1: 固定当前基线并继续验证

- 确认当前源码在板子上的真实行为是否和最新日志一致
- 必要时再做一轮 `12s` 或 `4x12s` 复测

### Phase 2: 只修一个主问题

当前最值得继续单点收敛的问题：

- `AWN->ATN->ARC->CSR` 这条链偏长
- `R90` handoff 后首帧 `lp` 仍然时常偏大

不要同时再碰 `TRK`、`R90`、`CSR` 三条链。

### Phase 3: 等角点逻辑收口后，再回头看交叉线

- 当前 `0415-real` 没有 `CRS`
- 交叉线问题要等角点链稳定以后再回到主线

## Key Decisions

### 已经确定的决策

- 后续所有代码都在 `0415-real` 上改，不再回到 `0412`
- 不取消锐角/直角提示，但尽量减少“分类后走完全不同执行逻辑”
- 已明确放弃一个方向：
  - `R90` 中途降级到 `CSR` 的 fallback
  - 日志证明这个方向会把纯 `R90` 长链变成更长的 `R90->CSR` 复合链
- 当前更偏好的策略：
  - `R90` 问题尽量在 `R90` 自己内部解决
  - handoff 问题尽量在 handoff 点解决

### 当前源码的关键参数状态

这些参数在 [config.h](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/config.h) 中，说明当前源码不是最原始版本，而是多轮调过的状态：

- `PID_TRACK_SPEED_TARGET = 22.0f`
- 普通弯道增强参数：
  - `TRACK_BEND_BOOST_POS_START`
  - `TRACK_BEND_KP_SCALE_MAX`
  - `TRACK_BEND_DEV_RATIO_MAX`
- `R90` 入口过滤：
  - `TRACK_RIGHT_ANGLE_TRIGGER_POS_MIN`
  - `TRACK_RIGHT_ANGLE_TRIGGER_STABLE_COUNT`
- `R90` 起转/接线参数：
  - `TRACK_RIGHT_ANGLE_ENTRY_BOOST_MS`
  - `TRACK_RIGHT_ANGLE_ENTRY_PWM_SCALE`
  - `TRACK_RIGHT_ANGLE_ARC_MIN_YAW`
  - `TRACK_RIGHT_ANGLE_ARC_PWM_SCALE`
  - `TRACK_RIGHT_ANGLE_HANDOFF_BASE_MAX`
  - `TRACK_RIGHT_ANGLE_HANDOFF_POS_MAX`
- 通用 handoff/接线参数：
  - `TRACK_CORNER_ARC_ENTER_POS_MAX`
  - `TRACK_CORNER_PROGRESS_EXIT_POS_MAX`
  - `TRACK_CORNER_PROGRESS_STABLE_COUNT`

## Blockers

### 1. 行为波动大

- 同一固件、多次 `12s` 测试下，状态链结构可能明显不同
- 用户每轮手动摆车，位置偏差会引入波动

### 2. 板上验证成本高

- 每次调完都要：
  - Keil 构建
  - pyOCD 烧录
  - 串口复测
- 真实结论必须依赖板上日志，不适合只靠静态推理

### 3. 工作树外部很脏

- `git status` 显示工作树外有大量不相关改动
- 新 AI 不要尝试清理整个 repo，只盯当前项目目录内目标文件

## Immediate Next Step

新 AI 接手后，先不要继续大改结构。优先做：

1. 读当前源码里的 [config.h](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/config.h)、[line_track.h](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/line_track.h)、[line_track.c](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor-0415-real/Hardware/line_track.c)
2. 读最新单次日志与多次日志，确认当前板上行为
3. 只围绕一个问题继续收敛：
   - `AWN->ATN->ARC->CSR` 长链
   - 或 `R90` handoff 仍不稳

