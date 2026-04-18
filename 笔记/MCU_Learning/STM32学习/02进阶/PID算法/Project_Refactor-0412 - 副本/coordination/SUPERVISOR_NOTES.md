# 监督意见

> 其他 AI 在此文件中留下对主执行 AI (Cascade) 的纠正意见和建议。
> Cascade 每次执行前会检查此文件。

---

<!-- 模板:
## [日期时间] — [AI名称]
### 问题
[描述发现的问题]

### 建议
[具体修改建议，最好包含代码片段]

### 优先级
- [ ] 必须立即处理
- [ ] 下次执行时处理
- [ ] 仅供参考

### 状态
- [ ] 待处理
- [ ] 已处理
-->

## [2026-04-12 18:35] — Codex Supervisor

### 问题

- Cascade 抓到了两个真实问题，但后半段开始把重点放错了：

  1. `predictedYaw` 确实会额外放大 P/D 相位差；
  2. `diff_max = base - 1` 在 PWM ×10 后确实把单侧近停转/枢转问题放大了；
  3. 但当前主矛盾已经不是“继续扫 `AKP/AKD`”，而是 **deadzone、动态 `diff_max`、IMU 时序** 三者耦合，导致 PID 参数本身失去可解释性。
- 关键误判 1：`diff_max = base * 0.40` 不是根治。

  - `Hardware/config.h:43` 里 `MOTOR_DEADZONE = 80`
  - `Hardware/motor_driver.c:137-142` 是先对 `core` 做 deadzone，再直接做 `L = base + diff`, `R = base - diff`
  - 这意味着只要 `base - |diff| < 80`，低侧轮就会重新掉回物理死区以下，虽然命令值还是正的，但轮子很可能已经不转了。
  - 所以 `base=80` 时，`diff_max=32` 也不安全，因为低侧只有 `48`，仍低于 `80`。
  - 结论：**你现在的问题不是“diff 太大导致一个轮子变成 1”这么简单，而是 deadzone-on-core 结构决定了启动阶段根本没有多少安全航向余量。**
- 关键误判 2：你认为“去掉 yaw 预测后，P 和 D 已同时间戳”，这在当前代码里并不成立。

  - `Hardware/sensor_fusion.c:515-518` 注释写的是 “Use 10ms interval (100Hz)”，但实际代码是 `bno_enable_report(BNO_SENSOR_GYRO, 40000UL)`，也就是 **40ms / 25Hz**
  - `Hardware/config.h:73` 的 `GAME_ROT_VEC` 还是 `20000us`，也就是 **20ms / 50Hz**
  - `User/main.c:503-509` 里是先 `run_control(now)`，再 `run_imu_update()`，控制本拍用到的是旧 IMU 样本
  - `User/main.c:382` 又在每个 10ms 控制周期都对同一份 `gyroZf` 重复做一次 LPF，进一步增加 stale-signal 行为
  - 结论：**当前 P 与 D 仍不是严格同相同龄数据，D 通道比你文档里写的更慢、更旧。**
- 关键误判 3：后面继续扫 `AKD` 的方法本身不成立。

  - 你现在每改一次 `AKD`，其实同时在和下面这些东西一起互动：
    - `core` 还在 ramp
    - `diff_max` 绑定 `core`
    - deadzone 只加在 `core` 上，不加在低侧单轮上
    - gyro 实际更新率比你预期慢
  - 这不是“标准线性 PID 植物”，所以出现 “P only mild / D only mild / P+D wild” 不能只解释成 PID 参数问题。
- 直接证据：

  - `raw_20260412_175611.log`
    - `t=414, pc=55, hd=-79, OL=1, OR=159`
    - `t=861, pc=112, hd=-111, OL=1, OR=223`
    - `t=1049, pc=114, hd=-113, OL=1, OR=227`
  - 这几拍不是一般意义上的“调大了一点”，而是 **低侧已经坍成 1，系统进入单轮几乎停转/枢转区**。
  - `raw_20260412_173000.log` 也能看到较轻版本的同类问题：
    - `t=1687, pc=89, hd=43, OL=132, OR=46`
    - 这里低侧 `46` 也已经低于 deadzone `80`，只是还没像 `175611` 那样彻底炸开。

### 建议

- **先停掉 D 扫参**：在 IMU 时序修正前，把 `AKD` 先压回 `0` 或极低值，不要继续靠扫 `KD` 找解。
- **先修结构，再调 PID**：

  1. 给航向环加启用门槛，而不是一启动就介入。
     - 建议条件至少满足其一：
       - `core >= MOTOR_DEADZONE + headroom`
       - 左右编码器都连续 N 个周期高于“已滚动”阈值
     - 在这个门槛之前，`diff = 0`
  2. 如果继续保留 `deadzone-on-core` 方案，`diff_max` 必须按“低侧不掉回 deadzone”来算，而不是按 `base-1` 或 `0.4*base`
     - 安全约束应接近：
       - `base - |diff| >= MOTOR_DEADZONE + margin`
     - 这也意味着启动早期 `base=80` 时，安全 `diff` 几乎就是 `0`
  3. 把 IMU 更新顺序改掉：
     - `run_imu_update()` 应在控制前执行
     - `g_fastYawRate` 只应在 **新 gyro 样本到达时** 更新一次，而不是每个控制 tick 都重复滤波旧样本
  4. 重新确认 gyro 报告周期
     - 现在代码和注释不一致；如果 25Hz 是故意的，那当前 D 项不值得继续投入时间
     - 如果本意是 100Hz/50Hz fresh gyro，需要先验证软 I2C 带宽是否真能承受
- **重排调试顺序**：

  1. 速度环单独稳定
  2. 固定 `core` 做航向测试，确认低速滚动区外的 heading 响应
  3. 再做 speed + heading 叠加
  4. 最后再看 `headingTrim` / 积分补偿
- **不要把当前 `hi` 的单调变化直接解释成机械 trim**

  - 在 `175611` 这种饱和/枢转条件下，`hi` 已经混入了结构性失真，暂时不能当纯机械偏置估计。

### 优先级

- [X] 必须立即处理
- [ ] 下次执行时处理
- [ ] 仅供参考

### 状态

- [X] 待处理
- [ ] 已处理

---

## 2026-04-12 19:33 — Supervisor (状态更新)

- `CHANGELOG.md` 最新记录已经表明：`SKP=1.50, SKI=0.10, SKD=0.0` 在两次 30s 复测里都实现了 `neg_core = 0`
- 我之前围绕“残余反转”的那一组立即修复建议，**现在按最新数据视为已过时**
- 后续监督重点切换到两件事：
  1. 数据分析代码是否能正确区分“方向偏差”和“直线性”
  2. 偏航/横偏指标是否建立在干净日志之上（去重、时间戳复位、路径定义正确）

---

## [2026-04-12 19:26] — Codex Supervisor

### 问题

- 针对“车走着走着轮子会反转”，我补一个更直接的结论：

  - `Cascade` 说“speed 积分在启动过冲后累积负值，后续偶尔把 total 推到负数”，这个方向是对的；
  - 但 **真正把现象放大成肉眼可见反转的，不是积分本身，而是 `core` 过零后被 motor deadzone 直接放大成 `-80` 级实际反向 PWM。**
- 直接证据来自最新可见诊断数据：

  - `000Data/diag/20260412_191435_final_SKP2_SKI01_ff115/raw.txt:19`
    - 前一拍：`pc=29, OL=80, OR=80`
    - 下一拍：`pc=-27, OL=-79, OR=-81`
  - `raw.txt:119`
    - `pc=-1, hd=0, OL=-80, OR=-80`
  - `raw.txt:336`
    - `pc=-1, hd=1, OL=-79, OR=-81`
  - 这说明 **只要 `pc` 略微穿过 0，实际电机命令不是“小幅反向”，而是立刻跳到接近 `-MOTOR_DEADZONE`。**
  - 更新确认：
    - `000Data/diag/20260412_192153_intlim50_SKI01/summary.txt`
    - 统计仍是 `neg_core = 25 / 331 (7.6%)`
    - `core_min = -19`, `min_OL = -87`, `min_OR = -87`
    - 说明即便把 `SKI=0.10` 且 speed integral limit 收紧到 `50`，反转机制依然存在，且没有被结构性消除
- 根因链条在代码里非常清楚：

  1. `Hardware/pid_controller.c:192-199`
     - speed 环先算 `total = ff + pid_out`
     - 然后直接四舍五入成 `core`
     - 当前没有 zero-cross hysteresis，也没有“直行模式禁止反转”
  2. `Hardware/motor_driver.c:112-116`
     - `apply_deadzone(-1)` 会直接变成 `-80`
  3. `Hardware/motor_driver.c:140-149`
     - `base = apply_deadzone(core)`
     - 当 `core < 0` 时，左右轮都会保留负值，不会被夹回 0
  4. 所以：
     - `pc = -1` 不是“轻微刹一下”
     - 而是 `OL/OR ≈ -80`
- 更重要的是，这不是一次偶发 bug，而是当前结构决定的 **零点翻转放大器**：

  - 正侧：`pc=1..79` 最终都会被抬成 `+80`
  - 负侧：`pc=-1..-79` 最终都会被抬成 `-80`
  - 也就是说，在 straight 模式下，电机层对 speed 环暴露出来的实际执行量接近：
    - `0`
    - `+80`
    - `-80`
  - 而不是连续的小幅度 PWM
  - 这会天然制造 limit cycle：前进过快 → PID 想“小幅减一点” → 无法在 `0..80` 内细调 → 一旦穿零就变成 `-80` 反冲 → 又被拉回正向。
- 还有一个容易漏掉的边角问题：

  - `000Data/diag/.../raw.txt:132`
    - `pc=0, hd=2, OL=2, OR=-2`
  - `Hardware/motor_driver.c:143-149`
    - 只有 `core > 0` 和 `core < 0` 才做方向夹紧
    - `core == 0` 时不夹
  - 所以 **即使 `pc==0`，只要 `diff != 0`，也可能出现单轮反向。**
  - 这个幅值不大，但从结构上说明 straight 模式现在并不是真正的“forward-only”。
- 对 `Cascade` 当前 19:30 结论的修正：

  - “降 `SKI` 可以减少 neg_core 频率”是对的
  - 但这只是 **降低触发概率**
  - 不是消除触发机制
  - 只要还允许 `core` 在 `0` 附近正负穿越，而 motor driver 仍把小负值放大成 `-80`，反转脉冲就还会存在。
- 另一个需要更正的上下文：

  - 我上一条 19:11 监督意见里提到过 speed-side DZ snap
  - 当前文件 `Hardware/pid_controller.c` 的最新版本已经撤回那段逻辑
  - 所以对“反转”的最新判断，应以 **当前代码 + 19:15 diag raw** 为准，而不是再按那版 DZ snap 模型解释

### 建议

- **如果 straight 模式本来就不允许倒车，最直接的结构修复是：禁止小负 `core` 被放大成反转**

  1. 在 straight 模式里，把 `core < 0` 先夹到 `0`
  2. 或者至少做零点滞回：
     - 只有当 `core < -MOTOR_DEADZONE` 或持续 N 个周期明确要求反向时，才允许进入反向
     - `-79..+79` 全部视为“停/滑行区”
- **把 deadzone 补偿改成“启动 breakaway”而不是“任意小命令都对称抬 DZ”**

  - 当前 `apply_deadzone()` 对正负两侧都做对称提升
  - 这对 forward run 的 speed loop 来说太激进
  - 更合理的是：
    - 从静止启动正向时，才做一次 breakaway boost
    - 进入滚动后，保持连续 PWM，不要把小负值直接抬成 `-80`
- **给 speed 环增加真正的 anti-windup / zero-cross guard**

  - 当前只有限幅 `SPEED_INTEGRAL_LIMIT`
  - 但没有“执行器被 deadzone 量化后，冻结/回推积分”的机制
  - 建议至少在以下情况冻结 speed 积分：
    - `0 < core < MOTOR_DEADZONE`
    - `-MOTOR_DEADZONE < core < 0`
    - 或实际输出与 `core` 差距过大时
- **补充 speed 侧遥测，否则现在只能猜**

  - 建议新增：
    - `speedErr`
    - `speedPID.integral`
    - `speedRampTarget`
    - `ff`
    - `core_before_motor`
    - `actualBaseAfterDZ`
  - 现在日志只有 `pc`
  - 无法区分到底是 `P` 过零、`I` 过零，还是 `ff`/ramp 在切换
- **修 straight 模式的 `core==0` 漏夹**

  - `core == 0` 时也应避免 `diff` 把单轮打成负值
  - 否则 `pc=0, diff!=0` 仍可能出现单轮反向/原地拧动

### 优先级

- [X] 必须立即处理
- [ ] 下次执行时处理
- [ ] 仅供参考

### 状态

- [X] 待处理
- [ ] 已处理

---

## [2026-04-12 19:11] — Codex Supervisor

### 问题

- 第二轮复核后，当前主风险已经从“参数不合适”转成 **记录、代码、分析工具三者不同步**：

  1. `coordination/CHANGELOG.md` 的 19:05 记录与实际代码不一致
  2. `000Data` 里没有对应的 30s 复测或新的原始日志，当前“8s 已稳定”的结论不可审计
  3. 上位机分析脚本仍在用旧 PWM 标度和旧控制假设，容易把你带偏
- 关键失配 1：`CHANGELOG` 写的参数，不是当前固件里的参数。

  - `Hardware/config.h:141-163`
    - `PID_STRAIGHT_SPEED_KP = 2.00`
    - `SPEED_ENTRY = 10.0`
    - `SPEED_FEEDFORWARD_GAIN = 9.0`
  - 但 `CHANGELOG.md` 19:05 写的是：
    - `SPEED_ENTRY = 7.0`
    - `SKP = 2.00`
    - 只提到按 `DZ/ff_gain≈80/11.5`
  - 结论：**你现在解释现象时，脑中的固件版本和仓库里的真实版本已经分叉了。**
- 关键失配 2：你文档里还在谈 “30ms 航向环”，但当前代码里根本没有这个分频。

  - `Hardware/config.h:136` 还有 `HEADING_LOOP_MS = 30`
  - 但搜索当前工程，`HEADING_LOOP_MS` 没有被实际使用
  - `User/main.c:367-388` 每个 10ms 控制周期都会执行 `DualLoop_ComputeStraight()`
  - `Hardware/pid_controller.h:45-46` 的 `headingResidual / headingAccumDt` 也是遗留字段，当前计算没有在用
  - `DualLoop_ComputeStraight(..., yawRate, gyroZ, ...)` 里 `yawRate` 形参也未使用
  - 结论：**当前真实架构是“10ms 全速率航向计算”，不是你文档中的“30ms 航向环”。**
- 关键失配 3：你认为 “gyro LPF 现在只在新 IMU 数据到达时更新”，当前代码仍然不能严格支撑这个说法。

  - `User/main.c:405-411`
    - 无条件执行 `BNO085_ReadAll(&g_imu);`
    - 无条件执行 `BNO085_UpdateYaw(&g_imu, dt);`
    - 无条件执行 `g_fastYawRate += 0.5 * (-g_imu.gyroZf - g_fastYawRate);`
  - `Hardware/sensor_fusion.c:631-647`
    - `BNO085_ReadAll()` 在 `ahrsInited && !got` 时也会返回 `1`
    - 但这不代表本拍有 fresh gyro
  - `Hardware/sensor_fusion.h:13-14`
    - 只有 `yawSampleUpdated`，没有 `gyroSampleUpdated`
  - `Hardware/sensor_fusion.c:401-416`
    - 解析到 gyro 报文时不会置任何“gyro fresh”标志
  - `Hardware/sensor_fusion.c:674-704`
    - `BNO085_UpdateYaw()` 每次都会重写 `yaw/prevYaw`，即使没有新 yaw 样本
  - 结论：**“只在新样本更新”的注释现在仍大于代码事实。P/D 的年龄差虽然缩小了，但没有被严谨消除。**
- 关键失配 4：你现在看到的“8s 看起来平滑”，很可能不只是 PID 变好了，而是速度环 deadzone 处理已经变成另一套非线性系统了。

  - `Hardware/pid_controller.c:197-203`
    - 新增了 speed-side deadzone snap：`0 < total < DZ` 时直接抬到 `DZ`
  - `Hardware/motor_driver.c:137-142`
    - motor driver 仍然保留 `deadzone-on-core` 再做 `base ± diff`
  - 这意味着当前系统同时存在：
    - speed PID 输出级 deadzone snap
    - motor driver core deadzone snap
    - `diff_max = 0.40 * base`
  - 结论：**这已经不是 19:05 文字里描述的那套系统了。现在 8s 变稳，可能是因为 core 很快被抬到 80~90 区间，而不一定是 `0.40*base` 这个思路本身被充分验证。**
- 关键失配 5：`000Project_PC_Control/pid_tuner.py` 目前不适合继续给这版固件下判断。

  - `pid_tuner.py:663-665`
    - `analyze_straight(..., pwm_max=60, diff_max=20, ...)`
    - 但固件当前是 `MOTOR_PWM_MAX = 600`, `MOTOR_DIFF_MAX = 200`
  - `pid_tuner.py:343-389`
    - `compute_speed_response()` 用 `pc` 当主速度指标
    - 但 rise/settle 阈值却仍用 `target_speed`
    - 当前 `pc` 已经是 PWM-like 量，而且还被 `SFF` 和 deadzone snap 改写过
  - `pid_tuner.py:349`
    - 注释自己也承认 `pc ≈ SFF * target`
    - 但后面 `thr10/thr90/band` 还是按 `target_speed` 算
  - 结论：**当前脚本的 rise time、settle、pwm saturation、hd saturation 等指标都可能是伪信号。不要再拿它做核心决策依据。**
- 证据缺口：

  - `coordination/CHANGELOG.md` 说 19:05 已完成 8s 诊断并待 30s 长跑
  - 但当前 `000Data/drift` 没有看到对应时间的新 `raw/report` 对
  - 没有原始日志，就无法确认：
    - 负 core 是否真的只出现 1 次
    - `OL/OR 最低 75` 是否覆盖整个 run
    - `|hd| <= 5` 是否只是短窗口现象

### 建议

- **先校正文档，再继续实验**

  1. 每次改完代码后，先用仓库里的实际值回填 `CHANGELOG`
  2. 如果配置文件与记录不一致，优先相信源码，不要相信脑内版本
- **把“样本 freshness”做成显式信号**

  1. 在 `IMU_Data_t` 里新增 `gyroSampleUpdated`
  2. `bno_parse_sensor_report()` 解析到 gyro 时置位
  3. `run_imu_update()` 里只有 fresh yaw 才 `BNO085_UpdateYaw()`
  4. 只有 fresh gyro 才更新 `g_fastYawRate`
- **不要再用当前 `pid_tuner.py` 的分数来判断这版固件优劣**

  1. 至少先把 `pwm_max/diff_max` 默认值改到当前量级
  2. `compute_speed_response()` 要么改用真实速度指标，要么把 `pc` 的参考值切到 `SFF*target + pid_ss`
  3. 在脚本修正前，实验判断请以原始 `HB` 日志和人工阶段分析为准
- **为当前非线性链路增加可观测量**

  - 建议新增遥测字段，至少打印：
    - `ff`
    - `speed_pid_out`
    - `core_raw_before_snap`
    - `core_after_snap`
    - `diff_max`
    - `gyroFresh/yawFresh`
  - 否则你现在只能看到结果 (`pc/hd/OL/OR`)，看不到是哪个非线性先起作用
- **在没有 30s 原始日志前，不要宣布“已稳定”**

  - 下一次结论必须附：
    - 一份 `raw_*.log`
    - 一份对应 `report/json/txt`
    - 明确绝对时间戳
- **低优先级但建议尽快清理代码漂移**

  - 删除或恢复真正使用 `HEADING_LOOP_MS`
  - 删除未使用的 `headingResidual / headingAccumDt / yawRate` 形参，避免后续分析继续被旧架构名词误导
  - `User/headfile.h` 还是旧工程遗留文件，容易让协作 AI 误读项目结构

### 优先级

- [X] 必须立即处理
- [ ] 下次执行时处理
- [ ] 仅供参考

### 状态

- [X] 待处理
- [ ] 已处理
