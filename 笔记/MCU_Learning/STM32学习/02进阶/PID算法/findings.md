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
