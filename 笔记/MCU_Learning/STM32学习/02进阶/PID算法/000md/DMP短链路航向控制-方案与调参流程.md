# DMP 短链路航向控制方案与调参流程（本工程可直接接手版）

本文档面向“新窗口/新AI接手调试”场景：只需要读完本文档，就能理解本工程当前控制架构、数据流、串口命令、调参顺序、常见故障定位方法，以及如何编译烧录。

---

## 1. 项目目标与核心思路

### 1.1 目标

- 让小车在**无轨迹/无摄像头**场景下尽可能“直行”。
- 外环用 IMU 的 **yaw（航向角）** 做航向锁定。
- 内环用编码器做左右轮速度闭环。
- 全过程支持串口在线调参，便于路测快速收敛。

### 1.2 核心思路（为什么能直）

- 使用 **MPU6050 DMP** 直接输出 yaw（姿态融合在芯片内完成），避免“裸陀螺积分漂移 + 软件融合”导致的漂移/抖动。
- 航向外环采用**短链路**：
  - `yawErr = targetYaw - yaw`（归一化到 `[-180, 180]`）
  - `headingCorr ≈ yawErr`（再叠加 yawRate 阻尼、slew 限速、死区）
  - `headingCorr` 通过差速注入影响左右轮 PWM

这比“复杂模糊PID/多级融合”更稳定、可解释、可调。

---

## 2. 工程关键路径与模块分工

### 2.1 工程根目录

- `f:\Documents\GitHub\nolebase-template\笔记\MCU_Learning\STM32学习\02进阶\PID算法\Project`

### 2.2 关键源文件

- `Hardware/Control.c`：控制系统主逻辑（外环/内环/串口命令/日志）
- `Hardware/Motor.c`：TB6612 电机驱动（STBY + PWM + 方向）
- `Hardware/MPU6050.c/.h`：旧 MPU 读数/工具函数（其中 `MPU6050_GetYawError()` 仍用于 yaw 误差归一化）
- `mpu6050dmp/inv_mpu.c/.h`：DMP 初始化与读取接口
- `User/main.c`：主循环、按键启停、OLED 显示

### 2.3 数据流（强烈建议按此理解）

1. `Control_Init()`
   - 清零 `ControlSystem_t`
   - **必须调用 `PID_Init()` 初始化 PID 参数**（本次已补齐，否则 PWM 永远为 0）
   - 初始化外设：`Encoder_Timer_Init()`、`Motor_Init()`、`VOFA_Init()`
   - 初始化 DMP：`mpu_dmp_init()`

2. `Control_Tick()`（主循环 1ms 调一次）
   - 外环每 20ms：`mpu_dmp_get_data()` -> 更新 `sys->mpu.yaw` 与 `sys->mpu.yawRate`
   - 外环每 20ms：`Control_OuterLoop()` -> 更新 `sys->headingCorr`
   - 内环每 10ms：`Control_InnerLoop()` -> 编码器速度闭环 -> 输出 PWM -> `Motor_SetDiffSpeed()`

---

## 3. DMP 接入说明

### 3.1 DMP API

- 初始化：`mpu_dmp_init()`
- 读取：`mpu_dmp_get_data(float *pitch, float *roll, float *yaw)`

说明：`yaw` 单位为角度（度）。

### 3.2 yawRate 计算

在 `Control_Tick()` 外环中：

- `dy = yaw - prevYaw`
- 处理跨界：将 `dy` 归一化到 `[-180, 180]`
- `yawRate = dy / dt`

### 3.3 yawErr 归一化

外环误差使用：

- `sys->yawErr = MPU6050_GetYawError(sys->targetYaw, sys->mpu.yaw);`

该函数负责把误差压到 `[-180, 180]`，避免 ±180 跨界时误差突变。

---

## 4. 控制架构（当前实现）

### 4.1 外环：短链路航向控制（20ms）

外环函数：`Control_OuterLoop()`

逻辑要点：

- 仅 `RUN=1` 且 `TEST_MODE_NORMAL` 时执行
- 计算 `yawErr`
- **死区 DB**：`|yawErr| < DB` 时，`headingCorr` 平滑回零（不会突然清零）
- 主纠偏：`corr = yawErr`
- **yawRate 阻尼 HD**：`corr -= yawRate * HD`
- 限幅：根据 `targetSpeed` 给纠偏留余量，避免打满抖动
- **slew HS**：每 20ms 限制 `headingCorr` 的变化速度，防止蛇形抖动

### 4.2 内环：左右轮速度闭环（10ms）

内环函数：`Control_InnerLoop()`

- 先 `Encoder_UpdateSpeed()` 更新左右轮速度
- 根据 `targetSpeed` 与 `headingCorr` 算左右目标速度 `leftTarget/rightTarget`
- 计算速度误差 `leftErr/rightErr`
- `PID_CalculateFull()` 输出 `leftOut/rightOut`
- PWM ramp（每 10ms 最大变化 `PWM_RAMP_STEP`）
- 输出到电机：`Motor_SetDiffSpeed(outL, outR)`

---

## 5. 串口命令与在线调参

串口解析入口：`Control_ParseVOFA()`

### 5.1 常用命令

- `#RUN!`：启动
- `#STOP!`：停止
- `#SPD=nnn!`：设置目标速度（建议从 5 开始）
- `#CAL!`：现场“锁航向”（停止后把当前 yaw 作为目标）
- `#STAT`：打印状态（用于定位：run/spd/y/yr/e/c/L/R/ok/fail 等）

### 5.2 外环调参命令（关键）

- `#HP=xx!`：`headingCorr -> diffPwm` 的增益（差速注入强度）
- `#HD=xx!`：yawRate 阻尼系数（抑制过冲）
- `#DB=xx!`：yawErr 死区（度）
- `#HS=xx!`：headingCorr slew 步进（每 20ms 最大变化）

（具体命令名以 `Control.c` 当前实现为准；如果后续有人改了命令表，要同步更新此文档。）

---

## 6. 推荐调参流程（路测顺序）

### 6.1 上车前 10 秒安全检查（悬空）

1. 上电，OLED 显示正常
2. 手转车身，`yaw` 变化
3. `#SPD=30!`
4. `#RUN!`
5. 看 OLED：`L/R` 应该从 0 开始爬升（如果一直 0，见“故障定位”）

### 6.2 方向判定（避免一跑就转圈）

悬空拿车，手动扭动车头：

- 正确：电机会输出“反扭回去”的趋势
- 错误：电机会“帮你越扭越转”

如果方向错：

- 修改 `Hardware/PID.h`：`HEADING_CORR_SIGN` 由 `1` 改为 `-1`（或反过来）
- 重新编译烧录

### 6.3 外环参数建议起步值（可作为基线）

- `#SPD=5!`（先低速）
- `#DB=1.0!`
- `#HD=0.0010!`
- `#HS=0.6!`
- `#HP` 从小到大递增（例如 4 -> 6 -> 8）

调参规则：

- `HP` 太小：纠偏不够，直线跑偏
- `HP` 太大：左右差速过猛，出现蛇形摆动
- `HD` 太小：回正容易过冲摆动
- `HD` 太大：纠偏响应变钝，转向/回正迟缓
- `HS` 太大：外环变化太快，容易抖
- `HS` 太小：外环跟不上，拐弯/回正滞后
- `DB` 太小：一直微纠偏，容易“抖动/啸叫”
- `DB` 太大：有明显角度误差也不纠偏

---

## 7. OLED 显示规范（避免越界）

本工程 OLED 为 16x4。

注意：

- `OLED_ShowSignedNum()` 会多打印一个符号位（`+`/`-`），总宽度 = `1 + Length`
- 对“永远非负”的字段（tick/ok/fail/run）应使用 `OLED_ShowNum()`，避免多占 1 列导致越界

当前 `User/main.c` 已按该原则修复。

---

## 8. 常见故障定位（最重要：新AI接手要会用）

### 8.1 现象：`RUN=1` 但电机完全不转，OLED `L/R` 一直是 0

高概率原因：速度 PID 未初始化导致 `outputLimit=0`，PID 输出被夹成 0。

定位方法：

- 看 `Control_Init()` 是否在清零结构体后调用了：
  - `PID_Init(&sys->leftSpeedPID, ...)`
  - `PID_Init(&sys->rightSpeedPID, ...)`

本工程已修复：`Control_Init()` 中补齐 `PID_Init()`。

### 8.2 现象：电机不转，但 `L/R` 已非 0

说明控制有输出，但驱动链路断：

- TB6612 `STBY` 是否被拉高（本工程为 PB0）
- PWM 引脚是否对应（本工程 TIM1：PA8/PA9）
- VM 电机供电是否上电
- 地线是否共地

### 8.3 现象：按键按下后“回不去/停不了”

原因：本工程 `Key_GetNum()`：

- 短按返回 1
- 长按返回 2

若只有一个按键，长按很容易触发 2。

本工程已修复：运行时 `keyNum==2` 也会优先 `Stop`。

### 8.4 现象：yaw 跨过 ±180 时纠偏突变

原因：yawErr 未归一化。

修复：使用 `MPU6050_GetYawError(targetYaw, yaw)`。

---

## 9. 编译与烧录（Keil + CubeProgrammer CLI）

### 9.1 固定路径（本机已验证）

- Keil：`D:\keil\Keil-v5\Arm\UV4\UV4.exe`
- CubeProgrammer CLI：`E:\STMcubeProgrammer\programmer\bin\STM32_Programmer_CLI.exe`
- 工程：`{ProjRoot}=...\Project`，工程文件：`project.uvprojx`
- 日志：`Objects\project.build_log.htm`
- HEX：`Objects\project.hex`

### 9.2 编译命令（PowerShell）

```powershell
& "D:\keil\Keil-v5\Arm\UV4\UV4.exe" -b "{ProjRoot}\project.uvprojx" -j0 -t "Target 1" -o "{ProjRoot}\Objects\project.build_log.htm"
```

### 9.3 编译成功判据（必须检查）

- `0 Error(s)`
- `creating hex file`

### 9.4 HEX 更新时间验证（避免烧录旧代码）

- `project.hex` 修改时间必须晚于 `User/Hardware/System/mpu6050dmp` 下最新 `.c/.h`。

### 9.5 烧录命令（Under Reset 推荐）

```powershell
& "E:\STMcubeProgrammer\programmer\bin\STM32_Programmer_CLI.exe" -c port=SWD freq=4000 mode=UR reset=HWrst -w "{ProjRoot}\Objects\project.hex" -v -rst
```

---

## 10. 后续工作建议（可选的迭代方向）

- 将 `FuzzyPID` 与旧 MPU 融合路径完全下线（清理依赖，减少复杂度）
- 把 DMP yaw 接口再封装一层（例如 `IMU_Init()/IMU_ReadYawYawRate()`）以隔离驱动细节
- 增加“开环强制PWM测试命令”（例如 `#RAW=30!`）用于现场快速判硬件

---

## 11. 本次调试过程中的关键结论（给新AI看的）

- **DMP yaw 链路已跑通**：手转 yaw 会变化
- OLED 越界问题已解决：对非负字段使用 `OLED_ShowNum()`
- 单按键长按导致无法 Stop 的问题已修复：运行时长按也会 Stop
- “电机不转但 RUN=1、L/R=0”的根因是 **PID 未初始化**，已在 `Control_Init()` 中补上 `PID_Init()`

---

> 维护建议：若后续修改 `Control_Init()` 的清零/结构体成员，请第一时间检查 PID、串口命令、OLED 输出是否仍符合本文档约束。
