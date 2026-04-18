# PID 参数记录
 
 ## 2026-03-20 — 当前有效版本（已修复启动大幅掉头）
 
 - 目标: 修复“启动先大幅掉头，再蛇形”问题，并建立可重复的低速长时验证参数。
 - 当前版本状态: 已验证有效。
 
 ### 本次确认生效的关键固件修复
 
 - 修复 1: `headingCorr` 到左右轮最终输出的符号反了，现已改为：
   - `outLQ += diffQ`
   - `outRQ -= diffQ`
 - 修复 2: 在最终共同推进基值处保留差速余量，避免实际输出层过早一起顶满。
 - 修复 3: `HB/STAT` 已增加最终实际输出：`OL/OR`。
 - 修复 4: 启动早期减小死区并增加较温和的介入窗口，减轻起步突发偏航。
 
 ### 当前推荐测试参数（低速稳定验证）
 
 - port: COM18
 - baud: 115200
 - so: 90
 - trim: 0
 - pwm-max: 60
 - diff-max: 20
 - hp: 1.2
 - hd: 0
 - hs: 0
 - hi: 0
 - hil: 3.0
 - min: 30
 - kp: 70
 - km: 900
 - ramp: 2
 - at: 0
 
 ### 已验证档位 1：`spd=4`，`ms=6000`
 
 #### 关键分析结果（trajectory_analyzer.py 增强版）
 
 - ed mean: 7.972
 - ed rms: 48.856
 - ratio(er/el): 0.988
 - yaw span(deg): 11.60
 - yaw drift slope robust(deg/s): 0.2524
 - yaw drift per dist(deg/rel_dist): 0.0647
 - path final x/y(rel): 1.391 / -0.005
 - path len(rel): 1.392
 - sinuosity: 1.001
 - net heading change(deg): -0.808
 - out diff abs_mean(OL-OR): 2.726
 
 #### 结论
 
 - `spd=4` 下已经从“持续发散”变为“基本稳定直行”。
 - 该档位可用于低速功能验证。
 
 ### 已验证档位 2：`spd=3`，`ms=8000`
 
 #### 关键分析结果（微偏差分析口径）
 
 - ed mean: 2.359
 - ed rms: 12.854
 - ratio(er/el): 0.996
 - yaw span(deg): 10.53
 - yaw drift slope robust(deg/s): 0.1305
 - yaw drift per dist(deg/rel_dist): 0.0353
 - path final x/y(rel): 1.939 / 0.017
 - path len(rel): 1.940
 - sinuosity: 1.000
 - lateral drift per path: 0.008657
 - net heading change(deg): 0.292
 - curvature p95: 0.399508
 - lateral short-window drift abs_p95: 0.006589
 - out diff abs_mean(OL-OR): 0.789
 
 #### 结论
 
 - `spd=3` 更接近实际需要的慢速档位。
 - 轨迹几乎为直线，能较好反映小车非常细微的偏差。
 - 当前 `TRIM=0` 已可保持，暂不建议修改。
 
 ### 当前推荐使用顺序
 
 - 日常低速验证: `spd=3`
 - 稍快但仍稳的验证: `spd=4`
 - 暂不建议直接回到 `spd=5`
 
 ### 当前推荐命令参数
 
 #### 慢速精细测试
 
 - `spd=3`
 - `ms=8000`
 - `so=90`
 - `trim=0`
 - `pwm-max=60`
 - `diff-max=20`
 
 #### 稍快验证
 
 - `spd=4`
 - `ms=6000`
 - `so=90`
 - `trim=0`
 - `pwm-max=60`
 - `diff-max=20`
 
 ### 当前总体结论
 
 - 启动大幅掉头问题的核心根因已定位并修复：**差速修正方向符号反了**。
 - 修复后，低速长时测试已经表现为**基本稳定直行**。
 - 若后续继续调参，优先在当前符号正确的版本上微调，不要回退该修复。

 ## 2026-03-20 — `spd=1` 第一轮调参（3010，失败样本）

 - exp: 3010
 - 目标: 在 `spd=1` 下压低微弱漂移，并减少手拨后回正的摆动次数。

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 8000
 - so: 90
 - trim: 0
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.90
 - hd: 0.06
 - hs: 0
 - db: 1.0
 - hi: 0
 - hil: 3.0

 ### 关键分析结果

 - ed mean: -10.822
 - ratio(er/el): 1.019
 - yaw span(deg): 1043.39
 - yaw drift slope robust(deg/s): 9.9808
 - path final x/y(rel): 1.760 / 0.312
 - sinuosity: 1.092
 - lateral drift per path: 0.160053
 - out diff abs_mean(OL-OR): 7.886
 - curvature p95: 4.910093
 - lateral short-window drift abs_p95: 0.247992

 ### 结论

 - 该组参数不可用。
 - 在 `spd=1` 下，`hd=0.06` 明显过大，导致低速阶段出现过度阻尼触发的来回修正，后段进一步发散。
 - 说明 `D` 项需要保留，但幅值必须显著降低，不能直接沿用较大的阻尼系数。

 ### 下一轮方向

 - `hd` 大幅降低：`0.06 -> 0.015`
 - `hp` 适度回升：`0.90 -> 1.00`
 - `db` 小幅增大：`1.0 -> 1.5`
 - 继续固定 `trim=0`、`hi=0`

 ## 2026-03-20 — `spd=1` 第二轮调参（3011，仍不理想）

 - exp: 3011
 - 目标: 保留少量 `D` 抑制回正振荡，同时降低 `P` 过度追踪。

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 8000
 - so: 90
 - trim: 0
 - pwm-max: 60
 - diff-max: 20
 - hp: 1.00
 - hd: 0.015
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0

 ### 关键分析结果

 - ed mean: -32.242
 - ratio(er/el): 1.055
 - yaw span(deg): 108.46
 - yaw drift slope robust(deg/s): 10.6911
 - path final x/y(rel): 1.769 / 0.368
 - sinuosity: 1.118
 - lateral drift per path: 0.182333
 - out diff abs_mean(OL-OR): 6.483
 - curvature p95: 3.836077
 - lateral short-window drift abs_p95: 0.283791

 ### 结论

 - 相比 `3010`，极端发散明显收敛，但整体仍不可接受。
 - 低速档下仍存在明显静态偏置：右轮长期偏快，导致航向环持续带差速补偿。
 - 这说明在 `spd=1` 下，问题不只是 `D` 不够或过大，更需要先用 `TRIM` 消除基础偏置。

 ### 下一轮方向

 - 保留极小 `D`：`0.015 -> 0.005`
 - `hp` 略降：`1.00 -> 0.95`
 - 引入小幅静态修正：`trim=0 -> 0.25`
 - 继续保持 `db=1.5`、`hi=0`

 ## 2026-03-20 — `spd=1` 第三轮调参（3012，当前最佳候选）

 - exp: 3012
 - 目标: 先用 `TRIM` 消静态偏置，再用极小 `D` 抑制回正摆动，并避免低速过度追踪。

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 8000
 - so: 90
 - trim: 0.25
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.95
 - hd: 0.005
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0

 ### 关键分析结果

 - ed mean: 6.926
 - ratio(er/el): 0.990
 - yaw drift slope robust(deg/s): 0.1903
 - yaw drift per dist(deg/rel_dist): 0.0210
 - path final x/y(rel): 1.962 / 0.037
 - sinuosity: 1.036
 - lateral drift per path: 0.017988
 - out diff abs_mean(OL-OR): 1.731
 - lateral short-window drift abs_p95: 0.049653

 ### 结论

 - 这是目前 `spd=1` 下最好的参数组合。
 - 相比前两轮，静态偏置已明显压住，航向长期漂移也大幅下降。
 - 仍存在轻微局部抖动和微弱漂移，尚可继续微调。

 ### 下一轮方向

 - 保持 `trim=0.25`
 - 保持 `hp=0.95`
 - 保持 `hd=0.005`
 - 增大死区：`db=1.5 -> 2.0`
 - 目标: 再减少低速微小 hunting 与回正小摆动次数

 ## 2026-03-20 — `spd=1` 第四轮调参（3013，db=2.0 不推荐）

 - exp: 3013
 - 目标: 仅提高死区，验证能否减少低速微小 hunting 与回正小摆动。

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 8000
 - so: 90
 - trim: 0.25
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.95
 - hd: 0.005
 - hs: 0
 - db: 2.0
 - hi: 0
 - hil: 3.0

 ### 关键分析结果

 - ed mean: 49.890
 - ratio(er/el): 0.924
 - yaw drift slope robust(deg/s): 0.3368
 - yaw drift per dist(deg/rel_dist): 0.0376
 - path final x/y(rel): 2.001 / 0.013
 - sinuosity: 1.012
 - lateral drift per path: 0.006307
 - out diff abs_mean(OL-OR): 2.953
 - lateral short-window drift abs_p95: 0.033197

 ### 结论

 - `db=2.0` 不是更优方向。
 - 虽然终点横向偏移更小，但静态轮速失衡反而放大，说明死区过大后系统对微小误差反应过迟，后续补偿更突兀。
 - 现阶段更好的基线仍然是 `3012` 的 `db=1.5` 组合。

 ### 下一轮方向

 - 回到 `db=1.5`
 - 保持 `hp=0.95`
 - 保持 `hd=0.005`
 - 微调 `trim`：`0.25 -> 0.20`
 - 目标: 在保留当前稳定性的同时，进一步压缩微弱静态偏置

 ## 2026-03-20 — `spd=1` 第五轮调参（3014，未超过 3012）

 - exp: 3014
 - 目标: 仅微调 `trim`，验证剩余微弱漂移是否主要来自很小的静态偏置。

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 8000
 - so: 90
 - trim: 0.20
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.95
 - hd: 0.005
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0

 ### 关键分析结果

 - ed mean: 10.520
 - ratio(er/el): 0.984
 - yaw drift slope robust(deg/s): 0.3119
 - yaw drift per dist(deg/rel_dist): -0.0114
 - path final x/y(rel): 1.771 / -0.030
 - sinuosity: 1.104
 - lateral drift per path: -0.015569
 - out diff abs_mean(OL-OR): 1.234
 - lateral short-window drift abs_p95: 0.084550

 ### 结论

 - `trim=0.20` 没有整体超过 `3012`。
 - 虽然部分指标有局部改善，但整体漂移鲁棒性和短窗侧漂移不如 `3012`。
 - 当前 `spd=1` 的最佳基线仍然是 `3012`：
   - `trim=0.25`
   - `hp=0.95`
   - `hd=0.005`
   - `db=1.5`

 ### 下一轮方向

 - 回到 `3012` 基线
 - 保持 `trim=0.25`
 - 保持 `hd=0.005`
 - 保持 `db=1.5`
 - 微降 `hp`：`0.95 -> 0.90`
 - 目标: 在不破坏当前直行性的前提下，进一步减少回正时的小摆动次数

 ## 2026-03-20 — `spd=1` 第六轮调参（3015，当前抑振更优）

 - exp: 3015
 - 目标: 在 `3012` 基线基础上仅降低 `hp`，观察是否能减少手拨后回正时的小摆动次数。

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 8000
 - so: 90
 - trim: 0.25
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.90
 - hd: 0.005
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0

 ### 关键分析结果

 - ed mean: 13.342
 - ratio(er/el): 0.980
 - yaw span(deg): 6.09
 - yaw drift slope robust(deg/s): 0.2846
 - yaw drift per dist(deg/rel_dist): 0.0883
 - path final x/y(rel): 1.909 / 0.035
 - sinuosity: 1.000
 - lateral drift per path: 0.018576
 - out diff abs_mean(OL-OR): 1.561
 - total heading variation(deg): 28.061
 - lateral short-window drift abs_p95: 0.012246

 ### 结论

 - 从“微小摆动次数、短窗侧漂移、总转向波动”角度看，`3015` 比 `3012` 更像当前更优的抑振组合。
 - 虽然 `yaw drift robust` 没有降到最低，但整体波动明显更小，更符合 `spd=1` 下“慢、稳、少摆”的目标。
 - 当前可优先把 `3015` 视作 `spd=1` 的抑振优先候选参数。

 ### 下一轮方向

 - 继续以 `3015` 为基线
 - 保持 `hp=0.90`
 - 保持 `hd=0.005`
 - 保持 `db=1.5`
 - 小步微调 `trim`：`0.25 -> 0.15`
 - 目标: 在保留当前低摆动状态的同时，继续压小静态偏置与长期微漂移

 ## 2026-03-20 — `spd=1` 第七轮调参（3016，当前新的最优候选）

 - exp: 3016
 - 目标: 在 `3015` 的低摆动基线上继续减小 `trim`，观察能否同时压低静态偏置与长期微漂移。

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 8000
 - so: 90
 - trim: 0.15
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.90
 - hd: 0.005
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0

 ### 关键分析结果

 - ed mean: 14.155
 - ratio(er/el): 0.979
 - yaw span(deg): 2.60
 - yaw drift slope robust(deg/s): 0.0430
 - yaw drift per dist(deg/rel_dist): 0.0098
 - path final x/y(rel): 1.905 / 0.048
 - sinuosity: 1.000
 - lateral drift per path: 0.025064
 - out diff abs_mean(OL-OR): 1.446
 - total heading variation(deg): 21.618
 - lateral short-window drift abs_p95: 0.010669

 ### 结论

 - 这是目前 `spd=1` 下新的最优候选组合。
 - 相比 `3015`，长期漂移、总转向波动、短窗侧漂移都进一步下降。
 - 说明 `spd=1` 当前更优方向是：较小 `P`、极小 `D`、较小的正 `trim`。

 ### 下一轮方向

 - 继续以 `3016` 为基线
 - 保持 `hp=0.90`
 - 保持 `hd=0.005`
 - 保持 `db=1.5`
 - 小步微调 `trim`：`0.15 -> 0.10`
 - 目标: 判断当前剩余轻微偏置是否还能再压缩，而不破坏已获得的低摆动状态

 ## 2026-03-20 — `spd=1` 第八轮调参（3017，短时最优均衡）

 - exp: 3017
 - 目标: 在 `3016` 基线上继续减小 `trim`，观察是否能进一步压缩微弱漂移，同时保持低摆动。

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 8000
 - so: 90
 - trim: 0.10
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.90
 - hd: 0.005
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0

 ### 关键分析结果

 - ed mean: 9.064
 - ratio(er/el): 0.986
 - yaw span(deg): 20.23
 - yaw drift slope robust(deg/s): 0.0393
 - yaw drift per dist(deg/rel_dist): 0.0051
 - path final x/y(rel): 1.899 / 0.032
 - sinuosity: 1.000
 - lateral drift per path: 0.016628
 - out diff abs_mean(OL-OR): 1.091
 - total heading variation(deg): 68.284
 - lateral short-window drift abs_p95: 0.011231

 ### 结论

 - `3017` 是目前 `spd=1` 下短时测试里最均衡的一组。
 - 漂移、短窗侧漂移、输出差速都较小，同时没有把小摆动重新带回来。

 ## 2026-03-20 — `spd=1` 15 秒长时复测（3018，首轮长测）

 - exp: 3018
 - 目标: 验证 `3017` 参数在 15 秒长时运行中的稳定性。

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 15000
 - so: 90
 - trim: 0.10
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.90
 - hd: 0.005
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0

 ### 关键分析结果

 - ed mean: 3.920
 - ratio(er/el): 0.994
 - yaw drift slope robust(deg/s): 0.0205
 - yaw drift per dist(deg/rel_dist): 0.1055
 - path final x/y(rel): 3.876 / 0.195
 - sinuosity: 1.045
 - lateral drift per path: 0.048155
 - out diff abs_mean(OL-OR): 1.568
 - lateral short-window drift abs_p95: 0.060335

 ### 结论

 - 首轮 15 秒长测整体可接受，长期漂移很小。
 - 长时统计中出现较大的局部 yaw span/曲率异常，更像角度解包与局部跳变放大，需要用重复试验确认是否可复现。

 ## 2026-03-20 — `spd=1` 15 秒长时复测（3019，重复验证）

 - exp: 3019
 - 目标: 复跑同一组参数，验证 15 秒长时稳定性是否可重复。

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 15000
 - so: 90
 - trim: 0.10
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.90
 - hd: 0.005
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0

 ### 关键分析结果

 - ed mean: 7.233
 - ratio(er/el): 0.989
 - yaw drift slope robust(deg/s): -0.0325
 - yaw drift per dist(deg/rel_dist): 0.0097
 - path final x/y(rel): 4.108 / 0.134
 - sinuosity: 1.001
 - lateral drift per path: 0.032495
 - out diff abs_mean(OL-OR): 2.037
 - lateral short-window drift abs_p95: 0.033362

 ### 结论

 - 第二轮 15 秒复跑继续保持稳定，说明该参数组合具有重复性。
 - 相比 `3018`，这轮长时横向偏移更小、路径更直，整体更可信。

 ### 当前 `spd=1` 推荐参数

 - spd: 1
 - trim: 0.10
 - hp: 0.90
 - hd: 0.005
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0
 - pwm-max: 60
 - diff-max: 20

 ### 当前总体判断

 - 这组参数已经在短时和两轮 15 秒长时复测中表现稳定。
 - 当前主要问题已从“明显掉头/明显摆动”收敛为“很小的长时微漂移”。
 - 对于你关心的“手拨后回正摆动次数太多”，当前组合已经明显优于此前更高 `hp` 或更大 `hd` 的方案。

 ## 2026-03-20 — `spd=1` 15 秒微调验证（3020/3021，trim=0.08 重复性不足）

 - 目标: 在 `trim=0.10` 已较稳的基础上，进一步用更小正向 `trim` 压低轻微右偏。

 ### 共同参数

 - spd: 1
 - ms: 15000
 - so: 90
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.90
 - hd: 0.005
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0
 - trim: 0.08

 ### 第一轮：3020

 - ed mean: 4.277
 - ratio(er/el): 0.994
 - yaw drift slope robust(deg/s): 0.0044
 - path final x/y(rel): 4.147 / 0.148
 - lateral drift per path: 0.035264
 - out diff abs_mean(OL-OR): 2.372
 - side vote: right（偏右投票明显）

 ### 第二轮：3021

 - ed mean: 90.840
 - ratio(er/el): 0.855
 - yaw drift slope robust(deg/s): 0.1155
 - path final x/y(rel): 3.919 / 0.106
 - lateral drift per path: 0.026120
 - out diff abs_mean(OL-OR): 1.591
 - side vote: 方向签名不稳定，重复性明显变差

 ### 结论

 - `trim=0.08` 第一轮看似可用，但第二轮重复性明显变差，不适合作为当前最优参数。
 - 说明当前最优点仍在 `0.10` 附近，而不是继续明显减小到 `0.08`。

 ### 下一轮方向
 
 - 回到 `trim=0.10` 附近
 - 固定 `hp=0.90`、`hd=0.005`、`db=1.5`
 - 做更细步长验证：`trim=0.09`
 - 继续采用 15 秒双轮复测，只有重复性更好才写成新的最优参数

 ## 2026-03-20 — 起步冻结逻辑修复后验证（3025/3026，3026 当前稳定候选）

 - 固件修改: 删除起步冻结阶段 `sys->targetYaw = sys->icm.yaw;`
 - 目的: 防止起步阶段因左右轮摩擦差产生的偏航误差被“吞掉”，让冻结结束后航向环能快速拉回，并继续由 `D` 项压住过冲。

 ### 共同参数

 - spd: 1
 - trim: 0.10
 - hp: 0.90
 - hd: 0.005
 - hs: 0
 - db: 1.5
 - hi: 0
 - hil: 3.0
 - pwm-max: 60
 - diff-max: 20
 - ms: 15000

 ### 第一轮：3025

 #### 起步前 1 秒

 - ed mean: 0.680
 - ratio(er/el): 0.999
 - yaw span(deg): 2.56
 - yaw drift slope robust(deg/s): 2.3144
 - path final x/y(rel): 0.362 / 0.009
 - lateral drift per path: 0.025896

 #### 全程 15 秒

 - ed mean: 3.561
 - ratio(er/el): 0.995
 - yaw drift slope robust(deg/s): 0.0833
 - path final x/y(rel): 4.106 / 0.084
 - lateral drift per path: 0.020470
 - out diff abs_mean(OL-OR): 1.617
 - total heading variation(deg): 51.311
 - lateral short-window drift abs_p95: 0.014780

 ### 第二轮：3026

 #### 起步前 1 秒

 - ed mean: 71.083
 - ratio(er/el): 0.909
 - yaw span(deg): 2.13
 - yaw drift slope robust(deg/s): 2.2948
 - path final x/y(rel): 0.339 / 0.005
 - lateral drift per path: 0.013689

 #### 全程 15 秒

 - ed mean: 8.709
 - ratio(er/el): 0.988
 - yaw drift slope robust(deg/s): -0.0244
 - yaw drift per dist(deg/rel_dist): -0.0048
 - path final x/y(rel): 4.115 / 0.054
 - lateral drift per path: 0.013222
 - out diff abs_mean(OL-OR): 0.701
 - total heading variation(deg): 29.835
 - lateral short-window drift abs_p95: 0.008971

 ### 结论

 - 修复后，起步段偏差不再被参考航向吞掉，冻结结束后能够更积极地拉回。
 - `3026` 是目前启动逻辑修复后的最佳 15 秒样本：
   - 全程漂移更小
   - 总摆动更小
   - 输出差速更温和
 - 当前将 `3026` 作为新的稳定候选，继续进入 30 秒长时复测。

 ## 2026-03-20 — 小偏差更早纠正测试（3029/3030，`db=1.0` 优于 `db=1.5`）

 - 目标: 在不增加摆动的前提下，让小偏差更早进入纠正。
 - 调整策略: 保持 `trim=0.10`、`hp=0.90`、`hd=0.005` 不变，仅将 `db` 从 `1.5` 下调到 `1.0`。

 ### 共同参数

 - spd: 1
 - ms: 15000
 - so: 90
 - trim: 0.10
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.90
 - hd: 0.005
 - hs: 0
 - db: 1.0
 - hi: 0
 - hil: 3.0

 ### 第一轮：3029

 - ed mean: 3.655
 - ratio(er/el): 0.994
 - yaw drift slope robust(deg/s): 0.0564
 - path final x/y(rel): 3.946 / 0.055
 - lateral drift per path: 0.014042
 - out diff abs_mean(OL-OR): 0.846
 - yaw_rate yr rms robust: 2.371
 - micro drift delta/slope: 0.000526 / 0.003926
 - micro side: right(1.000)

 ### 第二轮：3030

 - ed mean: 1.540
 - ratio(er/el): 0.998
 - yaw drift slope robust(deg/s): 0.0470
 - yaw drift per dist(deg/rel_dist): 0.0101
 - path final x/y(rel): 3.966 / 0.054
 - lateral drift per path: 0.013510
 - out diff abs_mean(OL-OR): 1.048
 - yaw_rate yr rms robust: 2.823
 - micro drift delta/slope: 0.000503 / 0.003829
 - micro side: right(0.971)

 ### 与 `3026` 对比结论

 - `db=1.0` 后，小偏差纠正更早，微右偏累计速度进一步减小。
 - 两轮 `micro drift` 都低于此前 `db=1.5` 的代表样本，说明方向正确。
 - 同时 `yr rms` 与 `out diff abs_mean` 仍处在较低水平，没有明显把摆动重新带回来。

 ### 当前判断

 - 在当前固件版本下，`db=1.0` 比 `db=1.5` 更接近目标：
   - 更早纠正小偏差
   - 未显著增加摆动
 - 下一步应继续用这组参数做更长时间验证，确认其长时重复性。

 ## 2026-03-20 — 基于有效段裁剪的结论更新（3031 / 3037）

 - 分析器增强:
   - 增加首次 `yaw` 异常跳变识别
   - 增加跳变前后控制量上下文输出
   - 增加有效段裁剪后的专用统计口径
 - 目的: 把“真实微右偏”与“异常 `yaw` 跳变 / 碰撞失真段”分开，避免错误调参。

 ### 3031（30 秒，`trim=0.10 hp=0.90 hd=0.005 db=1.0`）

 #### 整段结果

 - ed mean: -71.202
 - yaw drift slope robust(deg/s): 2.3031
 - path final x/y(rel): 6.944 / 1.626
 - lateral drift per path: 0.195833
 - total heading variation(deg): 5025.038

 #### 异常跳变识别

 - collision/stuck detected at: 9.310 s
 - first yaw jump detected at: 9.470 s
 - yaw jump delta: 197.850 deg
 - jump prev yaw / cur yaw: 1.150 / 199.000
 - jump prev(outL/outR, ed, trim, yr): 29 / 31, 72, 0.0000, -0.555
 - jump cur(outL/outR, ed, trim, yr): 0 / 0, 0, 0.0000, 0.000

 #### 有效段结果（裁剪后）

 - valid path final x/y(rel): 2.879 / 0.053
 - valid path len(rel): 2.879
 - valid lateral drift per path: 0.018397
 - valid yaw drift slope(deg/s): 0.0082
 - valid yaw drift per dist(deg/rel_dist): 0.0025
 - valid total heading variation(deg): 14.298
 - valid micro drift delta/slope: 0.000601 / 0.005659
 - valid side vote: right(1.000)
 - valid trim suggestion: 保持 `0.10`

 #### 结论

 - `3031` 整段看似很差，但主要是被 9.31 秒之后的异常段污染。
 - 在有效段内，这组参数仍然表现稳定，仅存在轻微右偏，不支持“立即引入 I 项”的结论。

 ### 3037（15 秒，`trim=0.105 hp=0.90 hd=0.005 db=1.0`）

 - ed mean: 8.291
 - ratio(er/el): 0.987
 - yaw drift slope robust(deg/s): 0.0445
 - path final x/y(rel): 3.777 / 0.114
 - lateral drift per path: 0.028831
 - out diff abs_mean(OL-OR): 1.380
 - micro drift delta/slope: 0.001231 / 0.008922
 - collision/stuck detected at: 8.610 s

 ### 更新后的当前最优参数

 - trim: 0.10
 - hp: 0.90
 - hd: 0.005
 - db: 1.0

 ### 当前判断

 - `trim=0.105` 不优于 `trim=0.10`，已排除。
 - 当前主要问题不是“缺少 I 项”，而是需要先用有效段口径持续压低轻微右偏，同时规避异常 `yaw` 跳变对判断的污染。
 - 在未出现更优重复样本前，当前最优参数仍保持为 `trim=0.10 hp=0.90 hd=0.005 db=1.0`。

 ## 2026-03-20 — 基于增强后的有效段口径的复核（3038 / 3039）

 ### 3038（15 秒，`trim=0.10 hp=0.90 hd=0.005 db=1.0`）

 #### 整段结果

 - yaw drift slope robust(deg/s): 0.0023
 - path final x/y(rel): 4.074 / 0.113
 - lateral drift per path: 0.027160
 - micro drift delta/slope: 0.000990 / 0.007459

 #### 异常识别

 - first yaw jump detected at: 9.420 s
 - yaw jump delta: 196.973 deg
 - jump prev(outL/outR, ed, trim, yr): 29 / 31, 10, 0.0996, 2.022
 - jump cur(outL/outR, ed, trim, yr): 30 / 30, 0, 0.0000, 0.039

 #### 有效段结果

 - valid rows: 433 / 624
 - valid path final x/y(rel): 2.870 / 0.066
 - valid lateral drift per path: 0.022981
 - valid yaw drift slope(deg/s): 0.0465
 - valid yaw drift per dist(deg/rel_dist): 0.0141
 - valid total heading variation(deg): 123.954
 - valid micro drift delta/slope: 0.000990 / 0.007459
 - valid trim suggestion: `0.10 -> -0.15`（仅从该轮编码器差看）

 #### 判断

 - 该轮有效段内总变化量仍偏大，与 `3029/3030` 不一致。
 - 更像异常样本，不适合据此直接改参数。

 ### 3039（15 秒，`trim=0.10 hp=0.90 hd=0.005 db=1.0`）

 #### 整段结果

 - yaw drift slope robust(deg/s): 0.0645
 - path final x/y(rel): 4.034 / 0.055
 - lateral drift per path: 0.013567
 - micro drift delta/slope: 0.000514 / 0.004339

 #### 异常识别

 - first yaw jump detected at: 10.990 s
 - yaw jump delta: 195.918 deg
 - jump prev(outL/outR, ed, trim, yr): 29 / 31, -5, 0.0996, 1.647
 - jump cur(outL/outR, ed, trim, yr): 2 / 30, 0, 0.0000, -0.489

 #### 有效段结果

 - valid rows: 504 / 621
 - valid path final x/y(rel): 3.325 / 0.045
 - valid lateral drift per path: 0.013485
 - valid yaw drift slope(deg/s): 0.1013
 - valid yaw drift per dist(deg/rel_dist): 0.0302
 - valid total heading variation(deg): 45.808
 - valid micro drift delta/slope: 0.000514 / 0.004339
 - valid trim suggestion: 保持 `0.10`

 #### 判断

 - `3039` 的有效段结果重新回到接近 `3029/3030` 的水平。
 - 因此 `3038` 更像单轮异常样本，不足以推翻当前最优参数。

 ### 更新后的结论

 - 当前最优参数仍保持为：
   - `trim=0.10`
   - `hp=0.90`
   - `hd=0.005`
   - `db=1.0`
 - 增强后的有效段分析已经证明：
   - 需要优先剔除异常 `yaw` 跳变段再判断参数优劣
   - 在正常有效段内，这组参数仍然是目前最稳的候选

 ## 2026-03-20 — `db=0.8` 双轮验证（3041 / 3042）

 目标：在不提高 `P/D` 的前提下，让更小偏差更早被接管，观察是否能进一步压低长时微右偏。

 参数保持：

 - trim: 0.10
 - hp: 0.90
 - hd: 0.005
 - db: 0.8

 ### 3041

 - yaw span(deg): 9.55
 - yaw drift slope robust(deg/s): -0.0301
 - path final x/y(rel): 4.069 / 0.070
 - lateral drift per path: 0.017191
 - total heading variation(deg): 77.966
 - micro drift delta/slope: 0.000613 / 0.004956
 - out diff abs_mean(OL-OR): 1.360
 - 该轮未出现大的 `yaw` 跳变，但整体摆动较 `db=1.0` 基线略有增大。

 ### 3042

 - yaw span(deg): 269.00
 - yaw drift slope robust(deg/s): 0.0662
 - path final x/y(rel): 3.945 / 0.111
 - lateral drift per path: 0.027613
 - total heading variation(deg): 366.401
 - micro drift delta/slope: -0.000301 / -0.004551
 - first yaw jump detected at: 0.580 s
 - jump delta: -251.555 deg
 - 该轮早期即出现异常 `yaw` 跳变，属于无效坏样本。

 ### 结论
 
 - `db=0.8` 没有明显优于当前 `db=1.0` 基线。
 - 从有效样本看，`db=0.8` 更像“接管更早，但整体更躁一点”。
 - 当前仍不能替换掉 `trim=0.10 hp=0.90 hd=0.005 db=1.0` 这组基线。
 - 下一步更合理的方向不是继续跳到更小 `db`，而是测试中间值 `db=0.9`。

 ## 2026-03-20 — `db=0.9 / 0.95` 中间值验证（3043 / 3044 / 3045）

 目标：确认在 `db=1.0` 与更激进的 `db=0.8` 之间，是否存在更稳的中间值。

 保持参数：

 - trim: 0.10
 - hp: 0.90
 - hd: 0.005

 ### 3043（`db=0.9`）

 - yaw drift slope robust(deg/s): 0.1117
 - path final x/y(rel): 3.975 / 0.016
 - lateral drift per path: 0.003903
 - out diff abs_mean(OL-OR): 0.551
 - micro drift delta/slope: -0.000568 / -0.003056
 - valid rows: 415 / 628
 - valid lateral drift per path: -0.013230
 - valid total heading variation(deg): 118.664
 - 判断：整段平面漂移指标非常亮眼，但有效段内部变化量仍偏大，属于“有潜力但还不够稳”的样本。

 ### 3044（`db=0.9`）

 - yaw drift slope robust(deg/s): -0.0101
 - path final x/y(rel): 4.037 / 0.083
 - lateral drift per path: 0.020374
 - out diff abs_mean(OL-OR): 0.936
 - micro drift delta/slope: 0.000898 / 0.007831
 - total heading variation(deg): 336.470
 - 判断：相比 `3043` 明显退化，重复性不足，不足以替代 `db=1.0`。

 ### 3045（`db=0.95`）

 - yaw drift slope robust(deg/s): 0.0786
 - path final x/y(rel): 3.860 / 0.067
 - lateral drift per path: 0.016737
 - out diff abs_mean(OL-OR): 1.722
 - micro drift delta/slope: 0.000413 / 0.002707
 - first yaw jump detected at: 9.420 s
 - jump delta: 143.078 deg
 - valid rows: 439 / 630
 - valid lateral drift per path: 0.007152
 - valid total heading variation(deg): 181.984
 - 判断：局部指标并不差，但整体摆动和异常段仍偏多，也没有稳定优于 `db=1.0`。

 ### 综合结论
 
 - `db=0.9`：存在单轮亮点，但重复性不足。
 - `db=0.95`：也没有稳定胜出。
 - 截至目前，**当前最优参数仍然保持为：**
   - `trim=0.10`
   - `hp=0.90`
   - `hd=0.005`
   - `db=1.0`

 ## 2026-03-20 — 新固件 30 秒长测（3054，链路已净化，后半程真实失稳）

 - exp: 3054
 - 目标: 在新固件与新日志链路下，验证 `trim=0.10 hp=0.90 hd=0.005 db=1.0` 是否能稳定运行 30 秒以上，并据此判断是否需要进入串级双环。

 ### 本轮使用的固件/链路前提

 - `HB/STAT` 已增加 `txdrop`
 - 普通 `HB` 周期已从 `10ms` 降为 `20ms`
 - `startup_probe.py` 已改为只写完整行，且预清空阶段不写盘
 - `trajectory_analyzer.py` 已跳过明显拼接坏行

 ### 下发参数（PC -> MCU）

 - spd: 1
 - ms: 30000
 - so: 90
 - trim: 0.10
 - pwm-max: 60
 - diff-max: 20
 - hp: 0.90
 - hd: 0.005
 - hs: 0
 - db: 1.0
 - hi: 0
 - hil: 3.0

 ### 遥测链路验证结论

 - 本轮 `run=1` 全程未检测到 `txdrop>0`
 - 未再出现 `yr=-1000`
 - 未再出现 `run=1` 时 `trim=0` 或 `OL=0 OR=0`
 - 结论: 旧样本里的 `yr≈-1000 / trim=0 / out=0` 组合，主因已经可以归到串口文本污染；本轮 30 秒样本可视为真实控制行为。

 ### 前 15 秒结果（仍然稳定）

 - ed mean: 5.965
 - ed rms: 35.339
 - ratio(er/el): 0.991
 - yaw span(deg): 7.23
 - yaw drift slope robust(deg/s): 0.0398
 - yaw drift per dist(deg/rel_dist): 0.0001
 - yaw_rate yr rms robust: 5.107
 - out diff abs_mean(OL-OR): 1.582
 - path final x/y(rel): 567.422 / 10.079
 - lateral drift per path: 0.017756
 - total heading variation(deg): 60.562
 - lateral short-window drift abs_p95: 2.269618
 - micro drift delta/slope: 0.128036 / 0.672472

 ### 后 15 秒结果（真实退化）

 - ed mean: -88.574
 - ed rms: 432.551
 - ratio(er/el): 1.235
 - yaw span(deg): 118.26
 - yaw drift slope robust(deg/s): 2.5312
 - yaw drift per dist(deg/rel_dist): 0.0067
 - yaw_rate yr rms robust: 39.663
 - out diff abs_mean(OL-OR): 14.486
 - path final x/y(rel): 400.002 / 99.285
 - lateral drift per path: 0.211813
 - total heading variation(deg): 322.442
 - lateral short-window drift abs_p95: 33.037791
 - micro drift delta/slope: 1.372479 / 7.417919

 ### 结论

 - 这组参数在**前 15 秒**内仍然表现为可接受的稳定直行。
 - 但在**后 15 秒**里，出现了明确且持续的真实退化：
   - 右轮长期偏快（`ratio(er/el)=1.235`）
   - 差速补偿显著增大（`out diff abs_mean=14.486`）
   - 横向漂移和总转向波动明显放大
 - 因为本轮已经排除了 `txdrop`、`yr=-1000`、`trim=0`、`OL/OR=0` 这类链路污染特征，所以这次后半程失稳应视为**真实控制/车体行为问题**，而不是日志假象。

 ### 是否现在进入串级双环

 - 当前判断: **暂不立刻切换到串级双环**。
 - 原因:
   - 当前结构在前 15 秒内仍能保持较稳直行，说明并非“当前单级结构一上电就完全不成立”。
   - 更像是长时间运行后，一侧轮速偏置/车体状态变化逐步积累，最终把航向修正推入较大幅度工作区。
   - 在还没有把这种“后半程单侧持续偏快”的根因继续拆开前，直接改成串级双环，容易把“真实根因”与“控制结构变化收益”混在一起。

 ### 当前阶段性判断

 - **`trim=0.10 hp=0.90 hd=0.005 db=1.0` 仍然是当前最优基线，但其稳定适用范围更接近 `15s` 量级，而不是已经证明可稳定 `30s+`。**
 - 是否进入串级双环的门槛，更新为：
   - 如果后续多轮“链路干净”的 30 秒长测都重复出现类似的后半程退化，且无法通过继续消除单侧轮速偏置来解决，那么再进入串级双环会更有依据。
 - 因此，当前优先级仍然是：
   - 继续定位 30 秒后半程失稳根因
   - 暂不因为旧的污染样本或单轮长测就直接切换控制架构

## 2026-03-20 右侧长时问题处理后的恢复验证（3065 / 3066 / 3067）

当前测试口径继续保持：`spd=1 trim=0.10 hp=0.90 hd=0.005 db=1.0 pwm-max=60 diff-max=20 so=90`。

### 3065：8 秒诊断恢复正常

- 日志：`000Data/startup_probe_exp_start_run_3065_20260320_171757.txt`
- `mean|el|: 667.412`
- `mean|er|: 664.311`
- `ratio(er/el): 0.995`
- `yaw span(deg): 8.48`
- `yaw drift slope robust(deg/s): -0.2495`
- `out diff abs_mean(OL-OR): 1.960`
- `path final x/y(rel): 246.369 / 1.005`
- `lateral drift per path: 0.004077`
- 结论：右侧链路在 8 秒诊断内恢复正常，具备继续跑 30 秒复验的条件。

### 3066：30 秒长测单轮表现非常好

- 日志：`000Data/startup_probe_exp_start_run_3066_20260320_171830.txt`
- `mean|el|: 598.847`
- `mean|er|: 596.435`
- `ratio(er/el): 0.996`
- `yaw span(deg): 1.48`
- `yaw drift slope robust(deg/s): 0.0102`
- `out diff abs_mean(OL-OR): 0.727`
- `path final x/y(rel): 957.961 / 14.545`
- `lateral drift per path: 0.015181`
- `total heading variation(deg): 36.636`
- 按 5 秒窗拆分，`00-05s` 到 `25-30s` 的 `ratio(er/el)` 依次为：`0.996 / 0.995 / 0.996 / 0.995 / 0.995 / 0.999`。
- 结论：这是目前恢复后最漂亮的一轮 `30s` 样本，右侧没有再出现上一轮 `3064` 那种“约 10 秒后掉到近零脉冲”的现象。

### 3067：继续 30 秒复验时未能重复 3066 的稳定性

- 日志：`000Data/startup_probe_exp_start_run_3067_20260320_171942.txt`
- `mean|el|: 277.593`
- `mean|er|: 636.369`
- `ratio(er/el): 2.292`
- `yaw span(deg): 124.81`
- `yaw drift slope robust(deg/s): -0.5275`
- `out diff abs_mean(OL-OR): 19.502`
- `path final x/y(rel): 782.312 / 274.066`
- `lateral drift per path: 0.301818`
- 5 秒窗拆分显示：
  - `00-05s ratio=3.465`
  - `05-10s ratio=39.724`
  - `10-15s ratio=6.885`
  - `15-20s ratio=2.102`
  - `20-25s ratio=2.137`
  - `25-30s ratio=0.499`
- 结论：`3067` 不是“前半程稳定、后半程慢慢退化”，而是从很早开始就出现了明显的左右轮不一致，因此 `3066` 虽然很优秀，但还不能认定为已经稳定复现。

### 当前阶段性判断更新

- `3066` 已经证明：在右侧问题处理后，系统**可以**重新回到非常接近直线、且能完整维持 `30s` 的状态。
- 但 `3067` 说明：当前还没有达到“同参数、连续重复多轮都稳定”的程度，重复性仍然不足。
- 因而当前最准确的表述应更新为：
  - **`trim=0.10 hp=0.90 hd=0.005 db=1.0` 仍然是当前最优基线；`3066` 是优秀长测样本，但还需要继续做重复性验证，暂不能直接宣布已经彻底稳定。**
- 后续优先级：
  - 继续追加 `30s` 复验，确认 `3066` 是可重复状态还是偶发优样本
  - 如果重复性仍差，再回到“起步状态 / 单侧轮速链路 / 车体机械一致性”的排查，而不是仅靠继续细调 PID

## 2026-03-20 冻结运行中 bias 跟踪版本验证（3071 / 3072 / 3073 / 3074 / 3075）

本轮固件改动为：在 `ICM42688` 中增加 `ICM42688_SetBiasTrackEnabled()`，并在 `Control_Start()` 后关闭运行期间的动态零偏跟踪，在 `Control_Stop()` 后恢复，以验证此前 `20s+` 异常是否由运行中的 bias 跟踪引入。

测试口径保持：`spd=1 trim=0.10 hp=0.90 hd=0.005 db=1.0 pwm-max=60 diff-max=20 so=90`。

### 3071：8 秒诊断正常

- 日志：`000Data/startup_probe_exp_start_run_3071_20260320_184718.txt`
- `mean|el|: 624.245`
- `mean|er|: 621.485`
- `ratio(er/el): 0.996`
- `yaw span(deg): 3.96`
- `yaw drift slope robust(deg/s): 0.1022`
- `out diff abs_mean(OL-OR): 0.914`
- `path final x/y(rel): 245.408 / 1.170`
- `lateral drift per path: 0.004767`
- 结论：改动后的短时诊断正常，具备继续做 `30s` 长测的条件。

### 3072：无效样本（碰撞）

- 日志：`000Data/startup_probe_exp_start_run_3072_20260320_184800.txt`
- 分析上曾显示约 `21.24s` 出现明显异常，但用户确认这是**小车撞到东西**导致。
- 结论：`3072` 不能用于判断这次固件改动的有效性。

### 3073：无碰撞 30 秒长测表现优秀

- 日志：`000Data/startup_probe_exp_start_run_3073_20260320_185401.txt`
- `mean|el|: 585.071`
- `mean|er|: 582.966`
- `ratio(er/el): 0.996`
- `yaw span(deg): 4.80`
- `yaw drift slope robust(deg/s): 0.0271`
- `out diff abs_mean(OL-OR): 1.253`
- `path final x/y(rel): 982.130 / 14.892`
- `lateral drift per path: 0.015160`
- 结论：这是一轮干净且稳定的 `30s` 长测，未再复现此前那种 `20s+` 的明显突跳。

### 3074：无效样本（人为踢车测试回正）

- 日志：`000Data/startup_probe_exp_start_run_3074_20260320_185505.txt`
- 分析上 `20-25s` 出现一次偏离后又恢复，但用户确认这是**人为踢了一下小车**观察回正效果。
- 结论：`3074` 不纳入固件长时稳定性判断。

### 3075：再次取得无干预 30 秒干净样本

- 日志：`000Data/startup_probe_exp_start_run_3075_20260320_185935.txt`
- `mean|el|: 549.504`
- `mean|er|: 547.199`
- `ratio(er/el): 0.996`
- `yaw span(deg): 1.86`
- `yaw drift slope robust(deg/s): 0.0231`
- `out diff abs_mean(OL-OR): 0.889`
- `path final x/y(rel): 937.536 / 14.006`
- `lateral drift per path: 0.014937`
- 结论：在无碰撞、无人为干预口径下，再次复现了接近直线且可完整维持 `30s` 的状态。

### 当前阶段性判断更新

- 这次“冻结运行中 bias 跟踪”的固件改动，**没有被 `3072`、`3074` 这两个无效样本否定**。
- 在有效口径下，已经获得两轮干净 `30s` 长测：`3073` 与 `3075`，两者都表现稳定，且都没有再出现此前那类典型的 `20s+` 明显突跳。
- 因而当前最准确的表述应更新为：
  - **参数仍保持 `trim=0.10 hp=0.90 hd=0.005 db=1.0`；当前推荐固件为“运行期间冻结 ICM42688 动态 bias 跟踪”的版本；在该版本上，`3073` 与 `3075` 已经给出了两轮有效 `30s` 稳定样本，重复性较之前明显改善。**
- 后续优先级：
  - 继续追加少量无干预 `30s` 复验，确认 `3073/3075` 不是偶发优样本
  - 若后续仍出现有效样本中的异常，再继续区分“姿态解算本体问题”与“起步/机械一致性问题”

## 2026-03-20 继续复验：3076 / 3077 / 3078

### 3076：从起步早期就异常的坏样本

- 日志：`000Data/startup_probe_exp_start_run_3076_20260320_190514.txt`
- `mean|el|: 412.355`
- `mean|er|: 516.424`
- `ratio(er/el): 1.252`
- `yaw span(deg): 125.16`
- `yaw drift slope robust(deg/s): 0.5248`
- `out diff abs_mean(OL-OR): 12.462`
- `path final x/y(rel): 852.860 / 79.752`
- `lateral drift per path: 0.087415`
- 5 秒窗拆分显示：
  - `00-05s ratio=1.232 yaw=17.96 c=-3.95`
  - `05-10s ratio=0.799 yaw=-9.63 c=2.78`
  - `10-15s ratio=0.816 yaw=-6.81 c=2.48`
  - `15-20s ratio=1.769 yaw=2.50 c=-2.25`
  - `20-25s ratio=1.708 yaw=9.79 c=-4.88`
  - `25-30s ratio=2.238 yaw=24.64 c=-10.00`
- 结论：`3076` 不是此前那种“前半程稳定、后半程才突然坏掉”的形态，而是从起步早期就已经明显异常，更像单次不良起跑 / 外部瞬态因素，而不是长时累积失稳再次稳定复现。

### 3077：紧跟 3076 后的 8 秒诊断已恢复正常

- 日志：`000Data/startup_probe_exp_start_run_3077_20260320_190632.txt`
- `mean|el|: 598.195`
- `mean|er|: 596.384`
- `ratio(er/el): 0.997`
- `yaw span(deg): 7.50`
- `yaw drift slope robust(deg/s): -0.2908`
- `out diff abs_mean(OL-OR): 1.677`
- `path final x/y(rel): 234.034 / -0.337`
- `lateral drift per path: -0.001438`
- 结论：`3076` 之后系统能立即恢复到正常短时状态，说明 `3076` 更像单次异常起跑，而不是进入了持续坏状态。

### 3078：再次取得稳定的 30 秒有效样本

- 日志：`000Data/startup_probe_exp_start_run_3078_20260320_190714.txt`
- `mean|el|: 558.636`
- `mean|er|: 557.076`
- `ratio(er/el): 0.997`
- `yaw span(deg): 2.82`
- `yaw drift slope robust(deg/s): 0.0427`
- `out diff abs_mean(OL-OR): 0.844`
- `path final x/y(rel): 944.759 / 11.736`
- `lateral drift per path: 0.012421`
- 结论：在 `3076` 这一轮单次异常之后，又再次获得了稳定的 `30s` 有效长测，进一步支持“长时 `20s+` 典型突跳问题已明显改善”的判断。

### 当前阶段性判断再次更新

- 当前推荐固件仍然是：**运行期间冻结 `ICM42688` 动态 bias 跟踪** 的版本。
- 在无碰撞、无人为干预的有效口径下，目前已经拿到 `3073 / 3075 / 3078` 三轮稳定 `30s` 样本。
- `3076` 说明：当前系统还不能说“每次一跑都同样漂亮”，起步重复性仍需继续观察。
- 因而当前最准确的表述应进一步更新为：
  - **参数保持 `trim=0.10 hp=0.90 hd=0.005 db=1.0`；推荐固件为“运行期间冻结 ICM42688 动态 bias 跟踪”的版本；在该版本上，长时 `20s+` 的典型突跳问题已明显改善，并已取得 `3073 / 3075 / 3078` 三轮有效 `30s` 稳定样本，但仍存在像 `3076` 这样的单次异常起跑，起步重复性尚未完全收敛。**
- 后续优先级：
  - 继续做少量无干预 `30s` 复验，判断 `3076` 这类异常起跑的实际出现频度
  - 若异常起跑仍反复出现，优先转向“起步冻结 / 软介入 / 机械起跑一致性”的专项排查，而不再把重点放在 `20s+` 长时突跳上

### 3079-3083：加入起步阶段 `headingCorr` 斜率限制后的重复性复测

- 本轮固件改动：在 `Control.c` 起步阶段加入 `START_HEADING_SLEW_MS=2500ms` 与 `START_HEADING_SLEW_STEP=0.08`，限制 `headingCorr` 在开跑前 `2.5s` 内的变化斜率，避免像 `3076` 那样单次 `yaw/yr` 突跳后把输出层差速瞬间拉满。
- 参数保持不变：`trim=0.10 hp=0.90 hd=0.005 db=1.0 pwmMax=60 diffMax=20 so=90 spd=1`

### 3079：新固件首轮 8 秒样本，可用但不算最优

- 日志：`000Data/startup_probe_exp_start_run_3079_20260320_192200.txt`
- `0-8s max|y|: 5.261`
- `0-8s max|yr|: 24.876`
- `0-8s max|c|: 4.731`
- `0-8s out diff abs_mean(OL-OR): 3.159`
- 结论：相较 `3076` 已明显改善，没有再出现 `2s` 左右直接把 `headingCorr` 拉到 `±10` 的坏起跑，但短时起步偏差仍明显大于 `3078`。

### 3080：8 秒样本进一步收敛

- 日志：`000Data/startup_probe_exp_start_run_3080_20260320_193005.txt`
- `0-8s max|y|: 2.579`
- `0-8s max|yr|: 11.918`
- `0-8s max|c|: 2.290`
- `0-8s out diff abs_mean(OL-OR): 1.613`
- 结论：已明显接近稳定样本区间，说明新增斜率限制并未破坏正常起步，反而让重复性向稳定区间收敛。

### 3081：8 秒样本接近 3078 的稳定水平

- 日志：`000Data/startup_probe_exp_start_run_3081_20260320_193053.txt`
- `0-8s max|y|: 1.640`
- `0-8s max|yr|: 10.498`
- `0-8s max|c|: 1.482`
- `0-8s out diff abs_mean(OL-OR): 0.954`
- 结论：已经接近 `3078` 这类优样本的起步表现，支持“问题核心在起步期固件容错，而不是参数本身随机失效”的判断。

### 3082：30 秒长测保持稳定，起步中等、长时正常

- 日志：`000Data/startup_probe_exp_start_run_3082_20260320_193140.txt`
- `0-30s max|y|: 3.970`
- `0-30s max|yr|: 19.995`
- `0-30s max|c|: 3.524`
- `0-30s out diff abs_mean(OL-OR): 1.569`
- 结论：在加入起步斜率限制后，`30s` 内未见新的长时 `yaw/yr` 突跳，说明该保护没有破坏此前“冻结运行期 bias 跟踪”版本已经取得的长时稳定性。

### 3083：30 秒长测非常稳，达到当前最佳档位之一

- 日志：`000Data/startup_probe_exp_start_run_3083_20260320_193250.txt`
- `0-30s max|y|: 2.004`
- `0-30s max|yr|: 17.635`
- `0-30s max|c|: 1.834`
- `0-30s out diff abs_mean(OL-OR): 0.720`
- 结论：`3083` 已达到与 `3078` 同一档位的稳定水平，并且是在新增起步斜率限制固件上的复现结果，说明当前增强方向是有效的。

### 对“为什么参数一样，有时会偏”的当前判断更新

 - 现有证据更支持：**同参数下的波动并不主要来自 PID 参数名义值本身，而是来自起步阶段对瞬时 `yaw/yr` 扰动的容错不足。**
 - 在旧固件中，像 `3076` 这种样本会在起步早期出现单次 `yaw/yr` 突跳，随后 `headingCorr` 很快被拉大，进一步把左右输出拉开，形成一次坏起跑。
 - 在新增 `headingCorr` 起步斜率限制后，同样参数下连续得到 `3079 / 3080 / 3081 / 3082 / 3083` 多轮可用乃至优秀样本，说明**固件起步保护**比继续盲调 `hp/hd/db` 更接近真正根因。

### 当前阶段性判断再次更新

 - 当前推荐方案更新为：**运行期间冻结 `ICM42688` 动态 bias 跟踪 + 起步阶段 `headingCorr` 斜率限制**。
 - 在该版本上，已连续获得 `3080 / 3081` 两轮稳定 `8s` 样本，以及 `3082 / 3083` 两轮稳定 `30s` 样本；其中 `3083` 已达到当前最佳档位之一。
 - 因而当前最准确的表述应更新为：
 - **参数暂保持 `trim=0.10 hp=0.90 hd=0.005 db=1.0`；推荐固件为“运行期间冻结 ICM42688 动态 bias 跟踪 + 起步阶段 headingCorr 斜率限制”的版本；该版本在保持长时稳定改善的同时，已明显增强起步重复性，说明当前剩余问题更偏向起步期固件容错，而不再是单纯参数值不足。**

## 2026-03-22 — `spd=2` 当前首选基线记录（6050）

- exp: 6050
- 目标: 在 `spd=2` 下记录当前现场体感较稳的首选参数，作为后续 35s 复测与 45s 长稳验证参考基线。

### 下发参数（PC -> MCU）

- spd: 2
- ms: 35000
- so: 90
- trim: 0.0956
- pwm-max: 60
- diff-max: 20
- hp: 10.75
- hd: 0.005
- hs: 0
- db: 1.0
- hi: 0.005
- hil: 0.5
- min: 12
- kp: 21
- km: 250
- ramp: 2
- at: 0
- port: COM18
- baud: 115200

### 关键分析结果

- yaw delta(deg): -0.000
- yaw rate(deg/s): -0.000
- ed mean: 1.394
- mean|el| / mean|er|: 352.877 / 351.471
- out mean L/R: 12.099 / 12.217
- motion proxy dist/speed: 4145.085 / 118.431
- turn hint: straight-ish
- wheel hint: balanced
- lateral final(rel): 3.781622
- lateral drift per path: 0.009124
- out diff abs_mean(OL-OR): 0.498990
- lateral short-window drift abs_p95: 0.201767
- total heading variation(deg): 24.663181

### 当前阶段判断

- `6050` 是当前 `spd=2` 口径下现场体感较稳的一组首选参数。
- 后续 `6053` 与 `6054` 复测表明，这组参数已进入“中线附近小波动区”。
- 下一步重点不再是粗调 `trim`，而是验证左右回正能力是否存在动态不对称。
