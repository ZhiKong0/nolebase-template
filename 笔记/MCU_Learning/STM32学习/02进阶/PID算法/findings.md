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

## 本轮换方向后的结论

- 仅靠继续拧 `kp_curve / deadband_curve / center_anchor_curve` 不会稳定下来，因此本轮改成两条新方向：
  - 算法侧：加入启动见线宽限，避免 `sb=0` 时刚起步就立刻 `TURN`。
  - 调度侧：给 `scheduleAlpha` 增加平滑，并降低 `curve_load` 对角速度和边缘灯的敏感度。
- 新日志里首次进入 `TURN` 已推迟到约 `1432ms`，说明启动宽限生效。
- 在新代码基础上，当前最有效的运行时参数不是继续动弯道增强量，而是静态差速偏置 `TTR`：
  - `TTR=3.0`：
    [track_dynamic_20260422_145656.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_145656.json)
  - `TTR=4.0`：
    [track_dynamic_20260422_145453.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_145453.json)
    [track_dynamic_20260422_145555.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_145555.json)
  - `TTR=6.0`：
    [track_dynamic_20260422_145630.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_145630.json)
- 结论：
  - `TTR=6.0` 明显过头。
  - `TTR=3.0` 偏置仍不够。
  - `TTR=4.0` 虽仍有波动，但在当前代码和赛道起始条件下是三者里平均最优的一档，因此已固化为新的默认值。

## 新方案：中心优先离散误差

- 用户要求“不要再左右晃，要尽量钳在 `S4/S5` 中线”，因此本轮不再沿旧的连续加权主链继续补丁，而是切换了主误差模型：
  - `S4|S5` 双灯纯中心：误差直接为 `0`
  - `S4`、`S5` 单灯：小误差
  - `S3/S6`：中等误差
  - `S2/S7`：更大误差
  - `S1/S8`：最大误差
- 同时在中心附近加入了符号翻转迟滞，避免 `S4/S5` 附近一过零就立刻反向抽打。
- 这意味着当前 `TRACK` 正常循迹阶段，已经不再以“连续加权位置均值”作为主控制量，而是以“中心优先离散误差 -> 滤波 -> PD 差速”作为主链。

## 新方案当前结果

- 代表性较好的一轮：
  [track_dynamic_20260422_150830.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_150830.json)
  - `mean_speed_counts = 126.5`
  - `line_rms = 1.920`
  - `edge_dwell_ratio = 8.511%`
  - `score = 55.63`
- 但同组复跑：
  [track_dynamic_20260422_150926.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_150926.json)
  仍然受起跑段状态影响，重复性还不够好。
- 因此当前判断是：
  - 新方案方向正确，已经比旧的连续加权主链更接近“钳住 `S4/S5`”。
  - 但它还不是最终稳定态，后续需要继续压起跑段波动和首段找线分叉。

## 当前更可用的基线

- 在“中心优先离散误差”基础上，继续补了两层中心区稳定手段：
  - 中心位时序钳制：`S4 <-> S5` 连续跳变时按 `S4|S5` 处理
  - 中心区直接离散误差：中心位存在时不再让 `preview + rate` 主导，而是直接按中心位型给小误差，且弱化微分
- 这一版下，`TTR=2.0` 比旧的 `4.0` 更适合作为默认静态偏置，已恢复为默认值。
- 当前两轮代表性复跑：
  - [track_dynamic_20260422_153132.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_153132.json)
  - [track_dynamic_20260422_153213.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_153213.json)
- 结论：
  - 这两轮虽然仍有波动，但已经比旧连续加权方案更接近“围绕 `S4/S5` 打转而不是大幅左右抽”。
  - 因此当前应把这版作为新的主线基线，而不是回退到之前那套连续均值 + 动态调度主干。

## 本轮继续调参后的结论

- 本轮额外验证了一条更激进的代码方向：
  - 把 `S4|S5` 双灯做成更硬的直控
  - 同时在中心区让动态调度更快退回直线档
- 这条方向虽然能编译、烧录、实跑，但代表性结果更差：
  - [track_dynamic_20260422_155033.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_155033.json)
  - [track_dynamic_20260422_155245.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_155245.json)
  - [track_dynamic_20260422_155534.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_155534.json)
- 结论：
  - 这条“更硬的中心直控”会削弱转弯衔接，不值得继续保留为代码主线。
  - 因此本轮已把代码恢复到上一版更稳的“中心优先离散误差 + 中心区直接控制律”主线。

## 当前最优运行时参数

- 在恢复主线后重新跑了 1 轮自动候选搜索：
  - [track_dynamic_r01_smooth_damp_20260422_155914.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_r01_smooth_damp_20260422_155914.json)
- 当前最优候选不是更大的 `Kp`、也不是更强的 `center lock`，而是更“阻尼型”的直线段参数：
  - `kp_straight = 16.732`
  - `kd_straight = 11.88`
  - `deadband_straight = 0.27`
  - `center_anchor_straight = 0.26`
  - `steer_trim = 2.0`
- 这一组的代表性指标：
  - `score = 40.43`
  - `line_rms = 2.765`
  - `straight_center_ratio = 69.231%`
  - `edge_dwell_ratio = 8.333%`
- 另外，本轮单独验证了把 `steer_trim` 大步改负值的方向：
  - [track_dynamic_20260422_160147.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_160147.json)
- 结论：
  - `steer_trim` 大步偏负会直接恶化中心占比和丢线率，不应继续沿这条线大幅搜索。
  - 本轮结束时，板端运行时参数已恢复为 `smooth_damp + TTR=2.0`，没有把坏试验参数留在板上。
