# PID Tuning Context — Project_Refactor-0412

> 最后更新: 2026-04-12 18:00 UTC+8
> 主执行 AI: Cascade (Windsurf)
> 状态: **活跃调试中**

---

## 1. 项目概述

STM32F103RC 双轮差速小车，目标是**直线行驶时最小化横向漂移和 yaw 振荡**。

### 硬件
- MCU: STM32F103RC (72MHz, Keil MDK)
- 电机驱动: TB6612 + TIM1 PWM
- IMU: BNO085 (Game Rotation Vector + Calibrated Gyroscope, 50Hz)
- 编码器: TIM2/TIM3 正交编码, 11PPR × 30减速比 × 4倍频 = 1320CPR
- 轮径: 65mm, 轮距: 145mm
- 串口: USART2 @ 115200 (COM18)

### 控制架构
- **速度环**: PID 控制 pwmCore (前进速度)
- **航向环**: PID 控制 headingDiff (左右轮差速)
- 控制周期: 10ms (SysTick → TIM4 1ms → divider)
- 电机输出: `L = base + diff`, `R = base - diff`

### 关键文件
| 文件 | 说明 |
|------|------|
| `Hardware/config.h` | 所有常量定义 (PWM, PID gains, timing) |
| `Hardware/pid_controller.c` | 双环 PID 核心 (`DualLoop_ComputeStraight`) |
| `Hardware/pid_controller.h` | PID 数据结构 |
| `Hardware/motor_driver.c` | 电机驱动 (PWM设置, deadzone, diff限幅) |
| `User/main.c` | 主循环, IMU读取, 传感器融合, 串口命令 |
| `000Project_PC_Control/drift_analyzer.py` | Python 漂移分析工具 |

---

## 2. 已完成的关键改动

### 2.1 PWM 分辨率 ×10 (已完成, 已验证)
**问题**: 原始 PWM 仅 100 级 (ARR=100, PSC=720), base=8 时 diff=±1 改变轮速 12.5%, 导致量化振荡。

**修复**:
```
MOTOR_PWM_PERIOD:    100 → 1000
MOTOR_PWM_PRESCALER: 720 → 72    (保持 1kHz PWM 频率)
MOTOR_PWM_MAX:        60 → 600
MOTOR_DEADZONE:        8 → 80
MOTOR_DIFF_MAX:       20 → 200
```
所有 PID 增益 ×10 适配新刻度:
```
SKP: 0.35 → 3.50    AKP: 0.47 → 4.70
SKI: 0.02 → 0.20    AKI: 0.200 → 2.000
SKD: 0.0  → 0.0     AKD: 0.32 → 3.20
SFF: 1.15 → 11.5    SPEED_OUTPUT_LIMIT: 60 → 600
```

### 2.2 P/D 相位冲突修复 (已完成, 已验证)
**问题**: `predictedYaw = yaw + yawRate × imuAge` 将 P 的输入预测到"现在", 而 D 使用的 `gyroZf` 来自 ~20ms 前。P 看未来, D 看过去, 组合时互相打架。

**诊断证据** (隔离测试):
```
P only  (4.70): yaw_std=0.67° STABLE
D only  (3.20): yaw_std=0.56° STABLE
P+D   (4.70+3.20): yaw_std=4.79° WILD  ← P和D单独都好, 组合就炸
P+I+D (修复后):    yaw_std=0.30° STABLE ← 去掉预测后恢复
```

**修复**: `main.c` 中去掉 yaw 预测, 直接用 `g_imu.yaw`:
```c
// 修复前 (有相位冲突):
predictedYaw = g_imu.yaw + g_imu.yawRate * imuAge;
DualLoop_ComputeStraight(..., predictedYaw, ..., g_fastYawRate, ...);

// 修复后 (P和D同时间戳):
DualLoop_ComputeStraight(..., g_imu.yaw, ..., g_fastYawRate, ...);
```

### 2.3 drift_analyzer 互补滤波 (已完成)
**问题**: 纯 IMU yaw 航迹重建显示 0.2mm 横偏 (假的, 因为 yaw 均值≈0), 而肉眼观察 ~8-9cm。

**修复**: 用互补滤波融合 IMU yaw (高频) + 编码器差速 (低频), alpha=0.05。修复后显示 +216mm, 与肉眼观察匹配。

---

## 3. 当前已知问题 (待修复)

### 3.1 diff_max 过大导致枢转 (待修复 — 最高优先级)
**症状**: 启动阶段 hd 冲到 ±79~113, OL/OR 打到 1/227, 做全力枢转。

**根因**: PWM×10 后 `diff_max = base - 1 = 79` (base=80 是 deadzone), 允许的修正量太大。旧 PWM 时 diff_max=7 实际起到了隐含增益限制器作用。

**推荐修复** (pid_controller.c 第 217 行附近):
```c
// 当前 (过大):
float diff_max = base - 1.0f;
if (diff_max < 30.0f) diff_max = 30.0f;

// 建议改为比例限幅:
float diff_max = base * 0.40f;       // base=80 → diff_max=32
if (diff_max < 10.0f) diff_max = 10.0f;  // 保底
```
这样 base=80 时: L_min=48, L_max=112, 两轮始终前进, 不会枢转。

### 3.2 D 增益受 IMU 延迟限制
**现象**: D alone 在 AKD=3.20 时 STABLE, AKD=8.0 时 WILD。

**根因**: BNO085 陀螺仪数据有 ~20ms 延迟。高 D 增益 + 延迟 = 修正过冲 → 振荡。

**结论**: D 增益上限约 3-5 (新刻度), 等效旧刻度 0.3-0.5。这是硬件限制, 无法通过软件解决。如需更高 D, 需要更低延迟的 IMU 或预测补偿。

### 3.3 积分单调增长 (需 HEADING_TRIM)
**现象**: 30s 内 hi 从 0 涨到 +9.5, 说明有持续机械右偏偏置。

**计划**: 先修复 3.1 和稳定 PID, 再通过 `#HTR=` 串口命令设置 HEADING_TRIM 补偿机械偏置。

---

## 4. D 项信号链 (正确性已验证)

```
BNO085 gyroZf → main.c: negate (-gyroZf) → LPF α=0.5 → g_fastYawRate
                                                              ↓
pid_controller.c: hd = -kd × gyroZ
                                    ↓
                  diff = round(hp + hi + hd)
                                    ↓
motor_driver.c: L = base + diff, R = base - diff
```

符号验证:
- 车左转 → gyroZf < 0 → g_fastYawRate > 0 → hd < 0 → diff < 0 → L 慢 R 快 → 纠正右转 ✓
- 车右转 → 反向 ✓

P 项符号:
- 车偏左 (yaw > 0) → headingErr = target - yaw < 0 → hp < 0 → diff < 0 → L 慢 R 快 → 纠正右转 ✓

---

## 5. 串口调试命令

| 命令 | 说明 | 当前值 (新刻度) |
|------|------|----------------|
| `#AKP=<f>!` | 航向 Kp | 4.70 |
| `#AKI=<f>!` | 航向 Ki | 2.000 |
| `#AKD=<f>!` | 航向 Kd | 3.20 |
| `#SKP=<f>!` | 速度 Kp | 3.50 |
| `#SKI=<f>!` | 速度 Ki | 0.20 |
| `#SFF=<f>!` | 速度前馈 | 11.5 |
| `#HTR=<f>!` | Heading Trim (deg) | 0.0 |
| `#RUN!` | 开始运行 | — |
| `#STOP!` | 停止 | — |
| `#STAT!` | 查询当前参数 | — |

遥测帧格式: `HB:t=<ms>,m=S,run=<0/1>,el=<int>,er=<int>,yaw=<float>,yr=<float>,pc=<int>,hd=<int>,dp=<int>,OL=<int>,OR=<int>`

---

## 6. 编译烧录流程

```powershell
# 编译
& "D:\keil\Keil-v5\Arm\UV4\UV4.exe" -b "project.uvprojx" -j0 -t "Target 1" -o "Objects\project.build_log.htm"

# 检查编译结果
Get-Content "Objects\project.build_log.htm" | Select-String "Error"

# 烧录 (需要断电上电冷启动)
& pyocd erase --chip --no-config -t stm32f103rc -M under-reset -f 100000
& pyocd load --no-config -t stm32f103rc -M under-reset -f 100000 -e sector "Objects\project.hex"
& pyocd reset --no-config -t stm32f103rc
```

---

## 7. 给监督 AI 的建议

### 当前最紧急任务
1. **修复 diff_max**: 将 `base - 1` 改为 `base * 0.40` (或其他合理比例), 消除枢转
2. **调 KP/KD**: 在 diff_max 修复后, 用串口快速扫参找最佳组合
3. **30s 复测**: 确认 yaw_std < 2°, 横偏 < 50mm

### 监督要点
- **检查 diff_max**: 确保任何修改都不会让 diff 大到让一个轮子停转
- **检查 D 增益**: AKD 不要超过 5.0 (新刻度), 超过会因 IMU 延迟振荡
- **检查相位**: P 和 D 必须用同一时间戳的传感器数据, 不要重新引入 yaw 预测
- **不要碰 HEADING_TRIM**: 先稳定 PID, 再用 trim 补偿机械偏置

### 如果要回退
最后已知稳定状态 (旧 PWM 100 级):
```
AKP=0.47, AKI=0.200, AKD=0.32
MOTOR_PWM_PERIOD=100, PRESCALER=720
diff_max = base - 1 (base=8)
有 yaw 预测 (predictedYaw)
```

---

## 8. 实验数据位置

| 文件 | 说明 |
|------|------|
| `000Data/drift/raw_*.log` | 原始遥测日志 |
| `000Data/drift/drift_*.csv` | 分析结果 CSV |
| `000Data/drift/drift_*.json` | 分析报告 JSON |
| `000Data/drift/drift_*.txt` | 文本报告 |

---

## 9. 变更时间线

| 时间 | 变更 | 结果 |
|------|------|------|
| 17:19 | PWM ×10 + 30ms 航向环 | 横偏 -187mm, yaw_std 高 |
| 17:30 | 扫 KP/KD (升 D) | 全部 WILD, D 升高反而更差 |
| 17:40 | 尝试分频 D (D@10ms, P+I@30ms) | 更差, 初始化 bug |
| 17:44 | 回退到 10ms 全速率航向环 | 仍 WILD |
| 17:50 | 隔离测试发现 P+D 相位冲突 | P 单独 mild, D 单独 mild, P+D WILD |
| 17:52 | 去掉 yaw 预测, P 和 D 同时间戳 | P+I+D yaw_std=0.30° STABLE! |
| 17:56 | 30s 复测 | 启动 hd=±79~113 枢转, diff_max 过大 |
| 18:00 | **当前**: 需修复 diff_max | — |
