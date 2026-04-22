# Progress

## 2026-04-22

- 重新检查了 `TRACK` 模式中心区误差建模，确认 `S4/S5` 单灯被视为 `0` 是直线蛇形的关键原因。
- 已在 `line_track` 中恢复中心单灯误差、收窄中心双灯锁定条件，并新增 `TRACK` 静态差速偏置参数链。
- 已把新参数打通到串口命令、`STAT:` 状态回包、`HB:` 遥测以及 PC 端 `track_dynamic_pid_tool.py`。
- 已完成两轮编译和两次 `pyOCD` 顺序烧录。
- 已在线复测多组参数，当前固化的默认值使用小量静态偏置 `TTR=2.0`。
- 已继续完成两轮自动调参与两轮重复性复测，确认更激进的 `trim4/trim6` 候选不如当前默认组稳定。
- 当前板端运行时参数已经重新下发回默认组，并通过 `STAT:` 读回确认。
- 已在 `TRACK` 速度链中恢复“专用快斜坡但不直通目标速度”，并把轮端输出改回双轮各自过死区 + 轮端斜率限制，避免一侧反复掉进死区造成“慢且一卡一卡”。
- 已完成一次编译、一轮顺序烧录，以及针对“弯道增强组”的二分调参：测试了增强量 `0.5 / 0.25 / 0.125` 三个中间点，结果都比默认组更差，说明这条增强方向本身不稳。
- 已换方向到算法侧：新增启动见线宽限和 `scheduleAlpha` 平滑，避免刚启动就进 `TURN`，并减少动态 steering 参数瞬时跳变。
- 已继续沿 `TTR` 做一维搜索；`3.0` 和 `6.0` 都更差，`4.0` 是当前最稳的静态偏置点，已固化为新的默认值。
- 当前代码默认组为：`SPD=50.0, TKP1=33.0, TDB1=0.05, TCL0=0.72, TCA1=0.63, TTR=4.0, TBG=0.15, TSMR=0.31`。
- 已正式切换到新的 `TRACK` 主误差方案：用“中心优先离散误差”取代连续加权均值作为正常循迹主控制量，目标明确对齐 `S4/S5` 中线。
- 新方案下的一轮较好结果为：
  - `mean_speed_counts = 126.5`
  - `line_rms = 1.920`
  - `edge_dwell_ratio = 8.511%`
  - `score = 55.63`
- 已继续把新方案收敛到更可用的基线：加入中心区时序钳制和“中心区直接离散误差 + 微分弱化”。
- 新方案在 `TTR=2.0` 下两轮复跑分别得到：
  - [track_dynamic_20260422_153132.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_153132.json)
  - [track_dynamic_20260422_153213.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_20260422_153213.json)
- 当前已将默认 `TTR` 从旧方案遗留的 `4.0` 收回到 `2.0`，并同步更新 PC 调参脚本默认值。
- 下一步：继续上板观察主观体感，如果直线仍有轻微蛇形，优先在 `TTR`、`TCA0`、`TDB0` 三个量上做小步微调。
- 本轮曾试过把 `S4|S5` 双灯做成更硬的“中心直控 + 快速回直线档”，已通过编译、烧录和实跑验证，但两轮代表性结果明显变差，因此该方向已撤回，不继续保留为代码主线。
- 已恢复到上一版更稳的“中心优先离散误差 + 中心区直接控制律”主线，并重新编译烧录。
- 在恢复后的基线上重新跑了 1 轮 `autotune(round=1)` 候选搜索，当前最优候选不是更大 `Kp` 或更强 `center lock`，而是 `smooth_damp`：
  - `kp_straight = 16.732`
  - `kd_straight = 11.88`
  - `deadband_straight = 0.27`
  - `center_anchor_straight = 0.26`
  - 代表性结果：
    [track_dynamic_r01_smooth_damp_20260422_155914.json](/F:/Documents/GitHub/nolebase-template/笔记/MCU_Learning/STM32学习/02进阶/PID算法/Project_Refactor_track-421%20-%20副本%20(2)/000Data/track_dynamic_pid/track_dynamic_r01_smooth_damp_20260422_155914.json)
    `score = 40.43`
- 已验证 `steer_trim` 大步改负值会直接恶化结果，因此本轮结束时，板端运行时参数已恢复并确认回到 `smooth_damp + TTR=2.0` 这组，而不是保留坏试验参数。
