# Findings

## 本轮定位

- 当前 `TRACK` 的蛇形主因是中心区误差建模错误：
  - [line_track.c](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/Hardware/line_track.c) 之前把 `S4` 单灯、`S5` 单灯和 `S4|S5` 双灯都视为 `0` 偏差。
  - 这会让中线附近只剩“等偏到外侧灯后再猛拉”的行为，直线表现就是来回蛇形。

## 本轮结论

- 静态偏置需要，但只需要小量。
  - 在线复测里 `steer_trim=2.0` 比 `0.0` 和 `3.0` 更稳，说明底盘存在轻微固定偏向。
  - 但 `straight_bias_mean` 并不大，说明静态偏置不是唯一主因，更不是主骨架。
- 当前更重要的修复是：
  - 恢复 `S4` / `S5` 单灯的可观测偏差
  - 只在 `S4|S5` 双灯纯中心命中时做硬锁定
  - 把静态差速偏置接入 `#TTR`、`STAT:ttr`、`HB:ltr`

## 当前固化参数

- `SPD=46.0`
- `TKP0=17.8`
- `TKP1=33.0`
- `TKD0=11.0`
- `TKD1=9.2`
- `TDB0=0.22`
- `TDB1=0.05`
- `TCL0=0.72`
- `TCL1=4.80`
- `TCA0=0.24`
- `TCA1=0.63`
- `TTR=2.0`
- `TBG=0.17`
- `TSMR=0.31`

## 当前最好的一轮

- 运行日志：
  [track_dynamic_20260422_134822.txt](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_134822.txt)
- 摘要：
  [track_dynamic_20260422_134822.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_134822.json)

## 最新重复性结论

- 单次自动调参里 `trim6` 曾出现最高分 `58.93`，但重复 3 次后的平均分只有 `33.88`，不具备稳定复现性。
- 当前默认组 `trim2_default` 在两轮重复性筛选里平均表现最好：
  - 第一轮重复性摘要：
    [repeatability_20260422_autotune_round2.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/repeatability_20260422_autotune_round2.json)
  - 第二轮局部精调摘要：
    [repeatability_20260422_refine_round3.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/repeatability_20260422_refine_round3.json)
- 因此本轮最终保留当前默认组，不切到更激进的 `trim4/trim6` 或混合弯道加强组。

## 本轮速度慢与顿挫修复结论

- 这轮“整车变慢 + 左右晃 + 一卡一卡”的主因，不是单纯 `KP/KD` 不够，而是三层速度/轮端约束互相打架：
  - `TRACK` 里把 `speedRampTarget` 直接推到目标速度，弯后油门抽升过猛。
  - 轮端改成“仅 base 过死区”，导致某一侧小 PWM 经常掉到低效区甚至 `0`。
  - 同时存在速度环 `pwmCore` 斜率和轮端 PWM 斜率，输出一边抽一边掉。
- 修复后，`TRACK` 速度链改为：
  - `DualLoop_ApplySpeedRamp()` 使用 `TRACK_SPEED_RAMP_UP_RATE / DOWN_RATE`
  - `main.c` 不再直通 `speedRampTarget = targetSpeed`
  - `line_track.c` 恢复双轮各自过死区和双轮斜率限制
  - 中心区额外限制差速预算，避免一侧轮子掉进死区边缘导致顿挫

## 弯道增强组二分结论

- 以默认组 `33.0 / 0.05 / 0.72 / 0.63` 为 `0.0`，以弯道增强组 `35.64 / 0.04 / 0.62 / 0.68` 为 `1.0`，对 `kp_curve / deadband_curve / load_low / center_anchor_curve` 做了二分。
- 已测试：
  - `0.5` 增强量：
    [track_dynamic_20260422_143552.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_143552.json)
  - `0.25` 增强量：
    [track_dynamic_20260422_143624.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_143624.json)
  - `0.125` 增强量：
    [track_dynamic_20260422_143657.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_143657.json)
- 结论：
  - 这三个中间点都没有把“弯道增强”修回稳定，反而都出现更高的 `line_loss_ratio`、`edge_dwell_ratio` 或更低的有效速度。
  - 因此这不是“再回一点点就稳”的问题，而是这条增强方向整体不对，当前应保留默认组并换别的维度继续调。
