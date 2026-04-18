# Changelog — PID Tuning (Cascade 主执行)

> 每条记录包含时间(UTC+8)、思考链、执行链、改动清单。
> 监督 AI 请在 SUPERVISOR_NOTES.md 留意见。

---

## 2026-04-12 17:19 — Cascade

### 思考链 (WHY)
- 问题现象: PWM 仅 100 级分辨率，diff=±1 改变轮速 12.5%，导致量化振荡
- 根因分析: ARR=100, PSC=720 → 有效分辨率仅 52 级 (DZ=8 到 MAX=60)
- 决策理由: ×10 分辨率最简单，保持 1kHz PWM 频率不变

### 执行链 (WHAT)
- [x] config.h: PWM_PERIOD 100→1000, PSC 720→72
- [x] config.h: 所有 PWM 常量 ×10 (MAX, DZ, DIFF_MAX)
- [x] config.h: 所有 PID gains ×10 适配新刻度
- [x] 编译烧录验证

### 改动清单 (FILES)
| 文件 | 行号 | 改动摘要 |
|------|------|----------|
| `Hardware/config.h` | L39-43 | PWM_PERIOD=1000, PSC=72 |
| `Hardware/config.h` | L135-163 | 所有 PID/PWM 常量 ×10 |

### 编译/测试结果
- 编译: 0 Error(s)
- 测试: 横偏 -187mm, yaw_std 偏高

---

## 2026-04-12 17:52 — Cascade

### 思考链 (WHY)
- 问题现象: P only mild, D only mild, 但 P+D 组合 WILD — 两项单独可以，合在一起打架
- 根因分析: `predictedYaw = yaw + yawRate * imuAge` 将 P 输入预测到"现在"，D 用 20ms 前的 gyro，P 看未来 D 看过去
- 决策理由: 去掉 yaw 预测，让 P 和 D 都用同一时间戳的 IMU 数据

### 执行链 (WHAT)
- [x] main.c: 去掉 predictedYaw 计算，直接用 g_imu.yaw
- [x] 编译烧录
- [x] 隔离测试: P+I+D yaw_std=0.30° STABLE

### 改动清单 (FILES)
| 文件 | 行号 | 改动摘要 |
|------|------|----------|
| `User/main.c` | L373-383 | 去掉 yaw 预测，直接传 g_imu.yaw |

### 编译/测试结果
- 编译: 0 Error(s)
- 测试: P+I+D (4.70/2.00/3.20) yaw_std=0.30° STABLE ✓
- 但 30s 复测暴露 diff_max 枢转问题

---

## 2026-04-12 18:20 — Cascade (响应 Codex 监督意见)

### 思考链 (WHY)
- 问题现象: Codex 指出 4 个结构性问题，全部验证属实
- 根因分析:
  1. IMU 更新在控制之后 → 控制用旧数据 (main.c L503 vs L509)
  2. gyro LPF 每 10ms 重复滤波旧样本 → 人为衰减 (main.c L382)
  3. gyro 报告率注释 100Hz 实际 40ms/25Hz (sensor_fusion.c L518)
  4. diff_max=base-1=79 让低侧轮掉到 DZ=80 以下 (pid_controller.c L217)
- 决策理由: 4 项全修，diff_max 改为 `base - DEADZONE - 10`，自然形成航向门槛

### 执行链 (WHAT)
- [x] main.c: IMU 更新移到控制前
- [x] main.c: gyro LPF 移入 run_imu_update()，只在新数据时执行
- [x] sensor_fusion.c: gyro 报告率 40000UL→20000UL (50Hz)
- [x] pid_controller.c: diff_max = base - DEADZONE - 10, 积分门控
- [x] 编译烧录

### 改动清单 (FILES)
| 文件 | 行号 | 改动摘要 |
|------|------|----------|
| `User/main.c` | L497-508 | IMU 更新移到控制循环之前 |
| `User/main.c` | L374-376 | 删除 run_control 中的 gyro LPF |
| `User/main.c` | L407-412 | gyro LPF 移入 run_imu_update() |
| `Hardware/sensor_fusion.c` | L512-517 | gyro 报告率 40000→20000 |
| `Hardware/pid_controller.c` | L213-228 | diff_max 按 deadzone 安全约束 |

### 编译/测试结果
- 编译: 0 Error(s)
- 测试: 全部 PIVOT! — **暴露新问题: 速度环 deadzone 跳变**

### 待监督审查
> ⚠️ diff_max 修复正确（hd_max=0 证实），但速度环 core 在 15→28 之间被 motor_driver 提升到 80，实际车速远超 PID 预期，PID 过纠到 core=-130，形成正反转振荡。需要修复前馈的 deadzone 补偿。

---

## 2026-04-12 18:33 — Cascade (诊断速度环 + ff保底尝试)

### 思考链 (WHY)
- 问题现象: core 在 15→28 之间突然暴跌到 -130，OL/OR 变负
- 根因分析: motor_driver 的 apply_deadzone 将 core=28 提升到 80，车速按 80 PWM 走，但速度 PID 认为 target 只需 core=28 → 速度超调 → PID 暴力纠正到 -130
- 尝试: ff 保底 DEADZONE
- 结果: **失败** — ff=80 导致 core 起步就=80+PID修正=105+，更猛的过冲

### 执行链 (WHAT)
- [x] pid_controller.c: ff 保底 DEADZONE (当 target>0 且 ff<DZ 时 ff=DZ)
- [x] 编译烧录测试
- [x] 结果: core 振荡更严重 (pc=-68↔+80)，**撤回此改动**

---

## 2026-04-12 19:05 — Cascade (根因修复: 三管齐下)

### 思考链 (WHY)
- 深入分析发现振荡根因不是单一因素，而是三个问题叠加:
  1. **diff_max=0 消除了航向修正**: base-DZ-10 在 base=80 时 diff_max=0 → hd=0 → 两轮完全对称 → 全部功率前进 → 加速过快
  2. **SPEED_ENTRY=0 导致 ramp 从零开始**: ramp 从 0→10 途中 ff<DZ → motor 被提升到 DZ → 速度PID模型与实际不匹配
  3. **SKP=3.50 启动过冲**: avgSpeed=0 时 err=7→P=24.5 → core=105 → 暴力加速
- 关键洞察: 电机一旦转起来，动摩擦<<静摩擦，低侧轮在 DZ 以下仍可维持转动
- 决策: diff_max 改比例限幅 (base*0.40)；SPEED_ENTRY=7.0；SKP=2.00

### 执行链 (WHAT)
- [x] 撤回 ff deadzone 保底 (pid_controller.c L194)
- [x] config.h: SPEED_ENTRY 0.0→7.0 (=DZ/ff_gain≈80/11.5)
- [x] pid_controller.c: diff_max = base*0.40 (min 5.0)，移除积分门控
- [x] config.h: SKP 3.50→2.00
- [x] 编译烧录测试

### 改动清单 (FILES)
| 文件 | 行号 | 改动摘要 |
|------|------|----------|
| `Hardware/pid_controller.c` | L194 | 撤回 ff deadzone 保底代码 |
| `Hardware/pid_controller.c` | L216-226 | diff_max = base*0.40 (floor 5.0)，积分无门控 |
| `Hardware/config.h` | L160 | SPEED_ENTRY 0.0→7.0 |
| `Hardware/config.h` | L141 | SKP 3.50→2.00 |

### 编译/测试结果
- 编译: 0 Error(s) ✓
- 8s 诊断测试 (86 样本):
  - **Negative core: 1/86** (仅 t=3505 pc=-15 轻微) ✓
  - **yaw 极稳: -0.5° ~ -0.1°** ✓
  - **|hd| ≤ 5** (航向修正小且受控) ✓
  - **无枢转: OL/OR 最低 75** ✓
  - core 平滑上升: 7→25→38→48→56→61→65→74
- **速度环振荡基本消除** ✓

---

## 2026-04-12 19:30 — Cascade (ff_gain/DZ snap 实验 + 最终调优)

### 思考链 (WHY)
- 30s 复测 (SKP=2.0, SKI=0.20, ff_gain=11.5): neg_core=18/328 (5.5%), yaw_std=0.54°
- 尝试 1: ff_gain=8.0 (ff=DZ 精确匹配) → **更差** 45/333 (13.5%), PID 在 DZ 边界反复跳跃
- 尝试 2: ff_gain=9.0 + DZ snap (total∈(0,80)→80) → **更差** 36/333 (10.8%), DZ snap 掩盖积分堆积
- 分析: 负 core 源于积分在启动过冲后累积负值，后续缓慢释放过程中偶尔推 total<0
- 决策: 回到 ff_gain=11.5 (ff>DZ, PID 正常工作)，降 SKI 0.20→0.10 减半积分堆积速率

### 执行链 (WHAT)
- [x] config.h: ff_gain=8.0 + SPEED_ENTRY=10.0 → 编译烧录测试 → **失败**
- [x] pid_controller.c: 添加 DZ snap → 编译烧录测试 → **失败**
- [x] 撤回 DZ snap 和 ff_gain 改动
- [x] config.h: SKI 0.20→0.10, 保持 ff_gain=11.5, SPEED_ENTRY=7.0
- [x] 编译烧录 30s 复测

### 改动清单 (FILES)
| 文件 | 行号 | 改动摘要 |
|------|------|----------|
| `Hardware/config.h` | L142 | SKI 0.20→0.10 |

### 编译/测试结果 (30s 复测, 332 样本)
- **Neg core: 13/332 (3.9%)** ✓ (最佳)
- **Core range: -19 ~ 108** (温和, 无暴力振荡)
- **Yaw: -0.8° ~ 0.7°, std=0.26°, final=0.0°** ✓✓✓
- **Max |hd|: 10** ✓
- **无枢转** ✓

### 当前最优配置
```
SKP=2.00  SKI=0.10  SKD=0.0
AKP=4.70  AKI=2.00  AKD=3.20
ff_gain=11.5  SPEED_ENTRY=7.0  RAMP_RATE=20
diff_max = base * 0.40 (floor 5.0)
DZ=80  PWM_PERIOD=1000  PSC=72
```

---

## 2026-04-12 19:30 — Cascade (调参扫描 + SKP=1.5 突破)

### 思考链 (WHY)
- 30s 复测 baseline (SKP=2.0, SKI=0.10) 稳定在 ~4-7% neg core
- neg core 源于速度环极限环: P 项与 DZ 非线性交互，core 在 DZ=80 边界周期性振荡
- 尝试了 5 种方案，只有 SKP=1.5 彻底消除了极限环

### 实验记录
| 配置 | neg_core | 结果 |
|------|----------|------|
| baseline SKP=2.0, SKI=0.10 | 3.9-6.9% | 参考基线 |
| + integralLimit=50 | 7.6% | ❌ 更差 |
| + SKD=0.5, ramp=5 | 39.9% | ❌ 灾难性失稳 |
| SKI=0.05 | 8.3% | ❌ 更差 |
| **SKP=1.5**, SKI=0.10 | **0.0%** (x2) | ✅ 突破！|

### 执行链 (WHAT)
- [x] config.h: SKP 2.00→1.50
- [x] 编译烧录 30s 测试 → neg_core=0/334
- [x] 重复测试 → neg_core=0/341 确认

### 改动清单 (FILES)
| 文件 | 行号 | 改动摘要 |
|------|------|----------|
| `Hardware/config.h` | L141 | SKP 2.00→1.50 |

### 编译/测试结果 (2x 30s 复测)
- Run 1: **neg_core=0/334 (0.0%)**, core=2~118, yaw_std=0.37°
- Run 2: **neg_core=0/341 (0.0%)**, core=0~104, yaw_std=0.32°, yaw_final=0.0°
- **速度环极限环彻底消除** ✓

### 最终最优配置
```
SKP=1.50  SKI=0.10  SKD=0.0
AKP=4.70  AKI=2.00  AKD=3.20
ff_gain=11.5  SPEED_ENTRY=7.0  RAMP_RATE=20
diff_max = base * 0.40 (floor 5.0)
DZ=80  PWM_PERIOD=1000  PSC=72
```

---

## 2026-04-12 19:50 — Cascade (航向 PID 调参 + 偏航精细分析)

### 思考链 (WHY)
- drift_analyzer.py 30s 精细分析揭示：漂移方向在左/右间随机，不是机械偏差
- 根因是**启动过冲**：前 2s 的航向扰动方向决定整次运行的漂移方向
- AKP=4.70 时航向 P 修正太弱（1° 误差仅产生 hd=4.7），来不及在启动阶段纠偏
- TRIM=+0.10° 实验导致 -1506mm 灾难性偏移（物理原因：车跑出平坦区域）

### 实验记录
| 配置 | 终点横偏 | 拟合线 RMS | 启动峰值 yaw |
|------|---------|-----------|-------------|
| AKP=4.70, AKI=2.00 (基线) | -13mm / +39mm | ~10mm | -1.3° |
| TRIM=+0.10° | -1506mm | N/A | 失控 |
| **AKP=6.00**, AKI=2.00 | +28mm | **4.77mm** | **+0.3°** |
| **AKP=6.00, AKI=3.00** | -29mm | **5.37mm** | ±0.6° |

### 执行链 (WHAT)
- [x] drift_analyzer.py 30s 采集 + 5 面板可视化 + 分段/逐秒统计
- [x] 修复 drift_analyzer 实时采集 bug（串口解析时序问题）
- [x] TRIM=+0.10° 实验 → 失败，回滚
- [x] AKP 4.70→6.00: 启动过冲从 -1.3° 降到 +0.3°
- [x] AKI 2.00→3.00: 积分恢复更快
- [x] diag_30s 确认 neg_core=0%, min_OL=70

### 改动清单 (FILES)
| 文件 | 行号 | 改动摘要 |
|------|------|----------|
| `Hardware/config.h` | L144 | AKP 4.70→6.00 |
| `Hardware/config.h` | L145 | AKI 2.00→3.00 |
| `000Project_PC_Control/drift_analyzer.py` | L875-896 | 修复实时采集 run=1 过滤 |
| `000Project_PC_Control/diag_30s.py` | 新增 | 30s 诊断采集脚本 |

### 关键发现
- **车走得很直** — 最佳拟合线 RMS 仅 5mm，说明 PID 控制良好
- **净横偏 ~30mm 来自起步随机偏差** — ±0.1° 初始航向误差在 30s 内积累
- **漂移方向不固定** — 多次运行左右随机，HEADING_TRIM 无法修复
- 进一步改善需要结构性改动（起步航向初始化/ramp）

### 当前最优配置
```
SKP=1.50  SKI=0.10  SKD=0.0
AKP=6.00  AKI=3.00  AKD=3.20  TRIM=0.0
ff_gain=11.5  SPEED_ENTRY=7.0  RAMP_RATE=20
diff_max = base * 0.40 (floor 5.0)
DZ=80  PWM_PERIOD=1000  PSC=72
```
