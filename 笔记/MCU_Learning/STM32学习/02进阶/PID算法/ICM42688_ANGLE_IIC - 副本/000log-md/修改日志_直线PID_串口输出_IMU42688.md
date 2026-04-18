# ICM42688_ANGLE_IIC 修改日志（直线行走 PID + 10ms 串口可视化输出）

## 1. 目标与结果

- **目标**
  - 按 `md/接线总表.md` 的接线与资源占用，将 `ICM42688_ANGLE_IIC` 例程从“仅输出 IMU 姿态角”扩展为：
    - TB6612 双路电机驱动（PWM + 方向 + STBY）
    - MG310 编码器测速（左右轮）
    - 小车走直线控制：左右轮速度闭环（PI）+ 偏航角 yaw 外环纠偏（差速）
    - 串口每 **10ms** 输出一帧数据（大量数据，便于上位机可视化）

- **结果**
  - 工程已集成上述功能。
  - 已处理外设资源冲突（USART、TIM、I2C 引脚）。
  - 适配 ARMCC5（C90）编译规则，避免 C99 语法导致编译失败。

---

## 2. 关键资源占用调整（为匹配接线总表）

### 2.1 串口：USART1 -> USART2
- **原因**
  - 原工程 `SYSTEM/usart/usart.c` 使用 `USART1 (PA9/PA10)`。
  - 接线总表要求 `USART2 (PA2/PA3)`。
  - 且 `PA9` 同时在接线总表中用作 `PWMB (TIM1_CH2)`，USART1 与 PWM 引脚冲突。

- **修改文件**
  - `SYSTEM/usart/usart.c`

- **实现内容**
  - `uart_init()`：初始化改为 **USART2 + PA2/PA3**。
  - `fputc()`：`printf` 重定向改为发送到 **USART2**。
  - 中断服务函数：由 `USART1_IRQHandler()` 改为 `USART2_IRQHandler()`。

---

### 2.2 IMU 时基：TIM3 -> TIM4（释放 TIM3 给右编码器）
- **原因**
  - 原工程 IMU 使用 `TIM3` 中断维护 `nowtime`（100us tick）。
  - 接线总表要求 `TIM3` 用作右轮编码器（`PA6/PA7` -> `TIM3_CH1/CH2`）。

- **修改文件**
  - `SYSTEM/delay/delay.h`
  - `SYSTEM/delay/delay.c`
  - `HARDWARE/IMU/IMU.c`

- **实现内容**
  - `delay.h`：声明由 `Initial_Timer3()` 改为 `Initial_Timer4()`。
  - `delay.c`：
    - 初始化函数改为 `Initial_Timer4()`，使用 `TIM4` 配置 100us tick。
    - 中断改为 `TIM4_IRQHandler()`：每次中断 `nowtime++`。
  - `IMU.c`：`IMU_init()` 内部调用 `Initial_Timer4()`。

---

### 2.3 软件 I2C：PB10/PB11 -> PB12/PB13
- **原因**
  - 接线总表：ICM42688 软件 I2C 使用 `PB12/PB13`。
  - 原例程使用 `PB10/PB11`。
  - 且接线总表中 `PB10` 被 TB6612 右电机方向 `BIN2` 占用，存在冲突。

- **修改文件**
  - `HARDWARE/IIC/myiic.h`
  - `HARDWARE/IIC/myiic.c`

- **实现内容**
  - I2C GPIO 宏与读写宏切换到 `PB12/PB13`。
  - `IIC_Init()` 初始化改为配置 `PB12/PB13`。
  - 修正 `SDA_IN()` / `SDA_OUT()`：
    - 输入：PB13 浮空输入
    - 输出：PB13 开漏输出

---

### 2.4 ICM42688 I2C 地址修正（ADO 接地）
- **原因**
  - 接线总表建议 ADO 接地 -> I2C 地址 `0x68`。
  - 工程使用的是 8-bit 地址（含 R/W 位），因此对应为 `0xD0`。

- **修改文件**
  - `HARDWARE/ICM42688/icm42688.h`

- **实现内容**
  - `#define ICM42688_ADDRESS` 修改为 `0xD0`。

---

## 3. 直线行走 PID 控制与电机/编码器实现

### 3.1 控制主逻辑位置
- **文件**
  - `USER/main.c`

- **说明**
  - 为避免 Keil 工程“新增文件未加入工程导致不编译”，核心控制逻辑集中写入 `main.c`。
  - 控制周期为 **10ms**（由 `nowtime` 100us tick 计数得到）。

---

### 3.2 TB6612 驱动（方向 + STBY + PWM）
- **文件**
  - `USER/main.c`

- **函数与作用**
  - `tb6612_gpio_init()`
    - 初始化 TB6612 方向引脚与 STBY：
      - `PB0`：`STBY`
      - `PA4/PA5`：左电机方向 `AIN1/AIN2`
      - `PB1/PB10`：右电机方向 `BIN1/BIN2`
    - 默认 `PB0=1` 使能驱动。

  - `tim1_pwm_init(uint16_t arr)`
    - 初始化 `TIM1` PWM 输出：
      - `PA8`：`TIM1_CH1` -> `PWMA`（左 PWM）
      - `PA9`：`TIM1_CH2` -> `PWMB`（右 PWM）
    - `Prescaler=72-1` 形成 1MHz 计数，`arr=1000` 时 PWM 频率约 1kHz。

  - `motor_set(int16_t left_pwm, int16_t right_pwm, uint16_t pwm_max)`
    - 设置左右电机方向 + PWM 占空比。
    - `left_pwm/right_pwm` 可正可负：
      - 正：正转
      - 负：反转
    - 内部限幅到 `[-pwm_max, pwm_max]`。

---

### 3.3 编码器（MG310）测速
- **文件**
  - `USER/main.c`

- **函数与作用**
  - `encoder_tim2_init()`
    - 配置 `TIM2` 为编码器模式：
      - `PA0/PA1`：左轮编码器 A/B

  - `encoder_tim3_init()`
    - 配置 `TIM3` 为编码器模式：
      - `PA6/PA7`：右轮编码器 A/B

  - `encoder_delta(uint16_t now, uint16_t* last)`
    - 计算 16-bit 计数器增量（利用 `int16_t` 自然溢出实现环绕）。
    - 用于每 10ms 得到 `dl/dr`（计数增量）。

---

### 3.4 PID（速度内环 + yaw 外环纠偏）
- **文件**
  - `USER/main.c`

- **数据结构与函数**
  - `pid_t`
    - `kp/ki/kd`：PID 参数
    - `integral`：积分项累积
    - `prev_err`：上一次误差
    - `out_min/out_max`：输出限幅

  - `pid_update(pid_t* p, float err)`
    - 计算 PID 输出（当前实现为 PI/PID 通用形式）。
    - 输出带限幅。

- **控制周期**
  - 由 `nowtime`（100us）决定：
    - 当 `(now_tick - last_ctrl_tick) >= 100`，即 10ms 执行一次控制。

- **控制结构**
  - **yaw 外环**（差速纠偏）
    - 首次进入控制环记录 `yaw0 = ypr[0]`。
    - `yaw_err = wrap_deg(yaw - yaw0)`（限制在 -180~180）。
    - `corr = yaw_k * yaw_err`。
    - 左右参考：
      - `ref_l = base_ref - corr`
      - `ref_r = base_ref + corr`

  - **速度内环**（左右 PI）
    - 测量：`dl/dr`（10ms 内编码器计数增量）。
    - 误差：
      - `err_l = ref_l - dl`
      - `err_r = ref_r - dr`
    - 输出：
      - `out_l = pid_update(&pid_l, err_l)`
      - `out_r = pid_update(&pid_r, err_r)`
    - 执行：`motor_set(out_l, out_r, 1000)`。

---

## 4. 串口输出（10ms 一帧：文本 / JustFloat 双模式）

- **文件**
  - `USER/main.c`

- **输出周期**
  - 每 10ms 输出 1 行。

### 4.1 模式切换（Firewater可读 / VOFA画图）

- **位置**：`USER/main.c`
- **宏**：`USE_TEXT_LOG`
  - `USE_TEXT_LOG = 1`：输出可读文本（适合 Firewater / 串口助手）
  - `USE_TEXT_LOG = 0`：输出 VOFA JustFloat 二进制帧（适合 VOFA+ 画图；串口助手会显示“乱码”）

### 4.2 文本模式（`USE_TEXT_LOG=1`）

- **停机状态（run=0）**：仍输出 IMU + 编码器，便于你手转轮验证编码器是否接对
  - `y=... ty=... dl=... dr=... diff=... tick=... run=...`
  - `dl/dr`：左右轮 10ms 编码器增量（计数/10ms）
  - `diff = dr - dl`：实际左右轮速度差（你最关心的）

- **运行状态（run=1）**：输出 IMU + 外环纠偏 + 编码器差值
  - `y=... ty=... yaw_e=... corr=... dl=... dr=... diff=... tick=... run=...`
  - `yaw_e`：`wrap_deg(yaw - yaw0)`（偏航误差）
  - `corr`：外环纠偏量（用于构造左右轮参考速度差）

> 说明：之前日志里写的 CSV 字段已不再适用，当前工程用“键值对文本”更方便快速确认每个量的含义。

---

## 5. 编译器兼容性修复（ARMCC5/C90）

- **问题**
  - ARMCC5 默认按 C90 语法限制：
    - 不允许“语句后再声明变量”（error #268）
    - 不支持 C99 指定成员初始化（`.kp=...`）

- **修改文件**
  - `USER/main.c`

- **修复方式**
  - 将变量声明统一移动到函数/代码块顶部。
  - `pid_t` 初始化改为逐字段赋值。
  - `pid_update()` 内部 `out` 变量声明前置。

---

## 6. 可调参数（必须按你的车调参）

- **文件**
  - `USER/main.c`

- **参数**
  - `base_ref = 80.0f`
    - 解释：10ms 内编码器计数目标（与轮径/减速比/编码器线数相关）。
  - `yaw_k = 1.2f`
    - 解释：yaw 外环纠偏增益。
  - `pid_l.kp/ki`、`pid_r.kp/ki`
    - 解释：左右轮速度内环 PI 参数。

---

## 7. 注意事项

- `USART2` 输出引脚为 `PA2(TX) / PA3(RX)`，请按接线总表连接 USB-TTL。
- 软件 I2C 使用 `PB12/PB13`，建议确认上拉电阻（模块若自带可先不外接）。
- 若出现小车抖动：先减小 `yaw_k` 或减小 `base_ref`，再调 PI。
- 当前 yaw 解算为 6轴（acc+gyro）互补滤波风格，yaw 可能随时间慢慢漂移，建议每次启动直线段重新锁定 `yaw0`（代码已在首次进入控制时锁定）。

---

## 8. 运行现象修复记录（左轮过快、右轮抖动）

### 8.1 现象

- **现象**
  - 左轮转动明显偏快。
  - 右轮出现左右抖动（PWM 来回变化）。

### 8.2 改动策略（抑振 + 补偿）

- **改动文件**
  - `USER/main.c`

- **改动点 1：速度 PI 降增益 + 积分限幅（anti-windup）**
  - `pid_t` 增加字段：`integral_min / integral_max`
  - `pid_update()` 内对 `integral` 做 `clampf()` 限幅
  - 初始化参数调整：
    - `pid_l.kp/ki`：`8.0/0.2` -> `5.0/0.08`
    - `pid_r.kp/ki`：`8.0/0.2` -> `5.0/0.08`
    - `pid_*.integral_min/max`：`-5000 ~ 5000`
  - **目的**：降低来回打满导致的抖动，并防止积分累积过大。

- **改动点 2：yaw 纠偏加入死区 + 限幅**
  - 新增参数：
    - `yaw_deadband = 1.0f`（度）
    - `corr_max = 25.0f`（纠偏对 `ref_l/ref_r` 的最大影响，单位=计数/10ms）
    - `yaw_k = 0.6f`（从 1.2 下调）
  - **目的**：避免 IMU 小抖动引发纠偏方向频繁翻转，从源头减少右轮抖动。

- **改动点 3：PWM 输出斜坡限速（ramp）**
  - 新增函数：`ramp_i16(target, current, step)`
  - 新增参数：`pwm_step = 25`（每 10ms 最大 PWM 变化量）
  - 引入状态量：`pwm_l_out / pwm_r_out`（实际输出），从 0 缓慢跟随 `cmd`
  - **目的**：抑制“来回打”的快速 PWM 跳变，减少机械抖动。

- **改动点 4：左右轮静态补偿 trim**
  - 新增参数：`trim_pwm = 30`
  - 作用方式：
    - `pwm_l_cmd = pid_out - trim_pwm`
    - `pwm_r_cmd = pid_out + trim_pwm`
  - **解释**：正值含义=压左抬右；当左轮天生偏快时使用正值。

### 8.3 串口输出格式更新（便于调参）

- **位置**：`USER/main.c` 控制周期内 `printf()`
- **更新后的 CSV 字段**
  - `t(ms),yaw,yaw0,yaw_err,corr,dl,dr,ref_l,ref_r,pidL,pidR,cmdL,cmdR,outL,outR`
- **新增字段含义**
  - `corr`：yaw 外环纠偏量（已限幅）
  - `cmdL/cmdR`：加入 `trim_pwm` 后的目标 PWM
  - `outL/outR`：经过 `ramp_i16` 限速后的最终输出 PWM

### 8.4 新增参数的含义（你调参时看这里）

- **`trim_pwm`（左右轮静态补偿）**
  - 目的：补偿左右电机/减速箱/轮胎摩擦差异造成的“同样控制量但速度不同”。
  - 当前实现：`cmdL = pidL - trim_pwm`，`cmdR = pidR + trim_pwm`。
  - 使用建议：
    - 左轮偏快：`trim_pwm` 取 **正值**（继续增大直到 `dl ≈ dr`）。
    - 右轮偏快：`trim_pwm` 取 **负值**。

- **`yaw_deadband`（yaw 死区，单位：度）**
  - 目的：抑制 IMU 微小抖动导致的“频繁纠偏”。
  - 使用建议：
    - 右轮仍抖：优先把死区从 `1.0` 提到 `1.5~3.0`。

- **`yaw_k`（yaw 纠偏增益）**
  - 目的：让车回到 `yaw0` 的速度。
  - 使用建议：
    - 纠偏太慢：增大 `yaw_k`。
    - 纠偏导致抖动：减小 `yaw_k`，或先增大 `yaw_deadband`。

- **`corr_max`（纠偏限幅，单位：计数/10ms）**
  - 目的：限制外环对左右参考速度 `ref_l/ref_r` 的最大影响，防止“纠偏过头”。
  - 使用建议：
    - 车转向很猛/抖动：减小 `corr_max`。
    - 车偏航大但纠不回来：增大 `corr_max`（同时注意 `yaw_k`）。

- **`pwm_step`（PWM 斜坡步进，单位：PWM/10ms）**
  - 目的：限制 PWM 跳变速度，让机械响应更平滑。
  - 使用建议：
    - 抖动明显：减小 `pwm_step`（例如 10~20）。
    - 车起步/加速太肉：增大 `pwm_step`（例如 30~60）。

### 8.5 推荐调参顺序（按这个来最快）

1. **先把 yaw 外环基本“关小”**
   - 设置 `yaw_k` 小一些（例如 `0.2~0.6`），`yaw_deadband` 大一些（例如 `2~3`），先让车尽量不抖。

2. **调 `trim_pwm` 让直线时 `dl ≈ dr`**
   - 只看 `dl/dr`（10ms 计数增量），目标是两者尽量接近。

3. **调速度 PI 让 `dl/dr` 能稳定跟随 `ref_l/ref_r`**
   - 如果 `pidL/pidR` 经常顶到 ±1000：
     - 降低 `base_ref`，或降低 `kp`/`ki`。
   - 如果速度跟不上（`dl/dr` 总是明显小于 `ref`）：
     - 适当增大 `kp`，再微调 `ki`。

4. **最后再把 yaw 外环“加回来”**
   - 逐步减小 `yaw_deadband`，逐步增大 `yaw_k`，观察 `yaw_err` 能否收敛且不引起 `cmd/out` 快速抖动。

### 8.6 用 CSV 字段快速定位问题

- **看 `cmdR` 抖但 `outR` 不抖**
  - 说明 `ramp` 在工作，抖动更多来自外环/PI 的目标在跳。

- **看 `outR` 也在快速正负切换**
  - 优先增大 `yaw_deadband`，或减小 `yaw_k`，或减小 `pwm_step`。

- **看 `dl-dr` 长期偏一边**
  - 说明左右轮固有差异未补偿好，继续调 `trim_pwm`。

---

## 9. PB5 按键启停（短按切换运行状态）

### 9.1 功能说明

- **硬件**
  - 按键接 `PB5`，另一端接 `GND`（上拉输入模式）。
  - 逻辑：松开=高电平(1)，按下=低电平(0)。

- **行为**
  - **短按一次**：切换 `run_enable`（停止 <-> 运行）。
  - **停止状态**：
    - 立即 `left(0)` / `right(0)` 清零 PWM
    - `Motor_Enable(0)` 让 TB6612 待机（更安全）
    - 仍然每10ms输出一行文本/JustFloat（便于你观察 yaw / 编码器是否正常）
  - **运行状态**：
    - `Motor_Enable(1)` 允许驱动输出
    - 复位控制状态：`Control_Reset(&ctrl)`（内部会清空 PID 积分/误差，并重新锁定 `yaw0`）

### 9.2 代码位置

- **修改文件**
  - `USER/main.c`

- **新增/修改函数**
  - `key_pb5_init()`：初始化 PB5 上拉输入
  - `key_pb5_read_raw()`：读取 PB5 原始电平（1松开/0按下）
  - `Motor_Enable(uint8_t en)`：控制 TB6612 `STBY`（PB0）

- **消抖方式**
  - 10ms 周期轮询
  - 需连续约 **30ms**（3次）检测到变化才更新 `key_stable`
  - 仅在 `key_last==1 && key_stable==0`（松开->按下沿）时触发一次切换

### 9.3 注意事项

- 若你想“上电默认就能跑”，把 `run_enable=0` 改成 `1`，并把 `Motor_Enable(0)` 改成 `1`。
- 如果按键抖动仍明显：可以把消抖计数阈值从 `3` 提高到 `5`（约50ms）。

---

## 11. 代码结构重构：控制/PID模块下沉到 HARDWARE/MOTOR_CONTROL

### 11.1 目标

- 避免 `USER/main.c` 与 `HARDWARE` 中存在重复的 PID/电机/PWM/控制实现。
- 让 `main.c` 只承担：初始化、10ms 调度、按键启停、串口输出。
- 控制计算（yaw外环 + 速度环PID + trim + ramp）由 `HARDWARE/MOTOR_CONTROL` 统一实现。

### 11.2 新增目录与文件

- `HARDWARE/MOTOR_CONTROL/`
  - `PWM.c/.h`
    - `PWM_Init(arr)`
    - `PWM_SetCompare1()` / `PWM_SetCompare2()`
  - `Motor.c/.h`
    - `Motor_Init(pwm_arr)`
    - `Motor_Enable(en)`
    - `left(output)` / `right(output)`（正反转 + 限幅 + 写PWM，接口风格对齐参考工程）
  - `PID.c/.h`
    - 精密 PID 结构体：`pid_speed_t`
    - `PID_SpeedInit()` / `PID_SpeedReset()` / `PID_SpeedUpdate()`
    - 保留参考工程风格入口：`PID_A()` / `PID_B()` 与 `PID_ResetAB()`
  - `Control.c/.h`
    - `Control_Reset()` / `Control_Step10ms()`

### 11.3 精密 PID 实现要点（对比旧版本）

- 速度环 PID 由“离散误差累加”改为“带 `dt` 的积分/微分”
  - 当前 `dt = 0.01`（对应 10ms 控制周期）
  - `integral += err * dt`
  - `deriv = (err - prev_err) / dt`
- 带限幅：
  - 积分限幅：`integral_min ~ integral_max`
  - 输出限幅：`out_min ~ out_max`
- 复位策略：
  - `Control_Reset()` 内部调用 `PID_ResetAB()`，启停切换时自动清除积分与历史误差，减少冲击。

### 11.4 main.c 职责变化（去重复）

- `USER/main.c` 不再包含：
  - `pid_update()` / `ramp_i16()` / `motor_set()` / `tim1_pwm_init()` / `tb6612_*` 等重复实现
- `USER/main.c` 运行时关键调用链：
  - 采样：`IMU_getYawPitchRoll()` + `encoder_delta()`
  - 控制：`Control_Step10ms(&ctrl, yaw, dl, dr, &pwmL, &pwmR, &yaw_err, &corr)`
  - 执行：`left(pwmL)` / `right(pwmR)`

### 11.5 Keil 工程文件更新

- `USER/ICM42688_IMU.uvprojx`
  - `IncludePath` 增加：`..\\HARDWARE\\MOTOR_CONTROL`
  - `HARDWARE` 分组加入编译：
    - `..\\HARDWARE\\MOTOR_CONTROL\\PWM.c`
    - `..\\HARDWARE\\MOTOR_CONTROL\\Motor.c`
    - `..\\HARDWARE\\MOTOR_CONTROL\\PID.c`
    - `..\\HARDWARE\\MOTOR_CONTROL\\Control.c`

---

## 12. main.c 辅助函数下沉：HARDWARE/APP_UTIL

### 12.1 目标

- 进一步减少 `USER/main.c` 中的“工具函数块”，让主文件更易读。
- 将与业务控制无关的通用功能（VOFA封包、按键、编码器初始化/求增量）统一放入 `HARDWARE` 子目录。

### 12.2 新增目录与文件

- `HARDWARE/APP_UTIL/`
  - `VOFA.c/.h`
    - `VOFA_SendJustFloat4()`
    - `VOFA_SendJustFloat5()`
  - `Key.c/.h`
    - `key_pb5_init()`
    - `key_pb5_read_raw()`
  - `Encoder.c/.h`
    - `encoder_tim2_init()` / `encoder_tim3_init()`
    - `encoder_delta()`

### 12.3 main.c 的变化

- `USER/main.c` 删除了对应的 `static` 实现，改为：
  - `#include "VOFA.h"`
  - `#include "Key.h"`
  - `#include "Encoder.h"`
- 主循环逻辑不变，仅调用接口。

### 12.4 Keil 工程文件更新

- `USER/ICM42688_IMU.uvprojx`
  - `IncludePath` 增加：`..\\HARDWARE\\APP_UTIL`
  - `HARDWARE` 分组加入编译：
    - `..\\HARDWARE\\APP_UTIL\\VOFA.c`
    - `..\\HARDWARE\\APP_UTIL\\Key.c`
    - `..\\HARDWARE\\APP_UTIL\\Encoder.c`

### 12.5 迁移前/迁移后对照（便于你快速核查）

- **迁移前（旧）**
  - `USER/main.c` 内部包含大量 `static` 函数：
    - VOFA JustFloat封包发送
    - PB5按键初始化/读电平
    - TIM2/TIM3 编码器初始化
    - `encoder_delta()`
  - 结果：`main.c` 前半段被工具函数淹没，主循环逻辑不突出。

- **迁移后（新）**
  - `USER/main.c` 删除上述 `static` 实现，仅保留：
    - `#include "VOFA.h"` / `"Key.h"` / `"Encoder.h"`
    - `key_pb5_init()` / `encoder_tim2_init()` / `encoder_tim3_init()` 等初始化调用
    - 10ms循环里继续调用 `encoder_delta()`、`VOFA_SendJustFloat4()`
  - 结果：`main.c` 的“主流程”更集中：初始化 -> 10ms调度 -> 按键启停 -> 采样 -> 控制 -> 输出。

### 12.6 模块职责与依赖关系

- `APP_UTIL/VOFA.*`
  - **职责**：把 float 通道数据封包成 JustFloat 帧，并通过 `USART2_SendByte/USART2_SendBuffer` 发出。
  - **依赖**：`SYSTEM/usart/usart.h`

- `APP_UTIL/Key.*`
  - **职责**：PB5 按键硬件抽象（初始化 + 原始电平读取）。
  - **依赖**：标准外设库 GPIO/RCC
  - **注意**：消抖逻辑仍放在 `main.c`（因为它是“调度策略”，不是硬件驱动）。

- `APP_UTIL/Encoder.*`
  - **职责**：
    - TIM2/TIM3 编码器模式初始化
    - 16位计数差分 `encoder_delta()`（利用 `int16_t` 溢出自然处理回绕）
  - **依赖**：`stm32f10x_tim.h`（编码器接口配置）

### 12.7 编译验证结果

- Keil 编译结果：
  - `0 Error(s), 4 Warning(s)`
  - 说明：本次重构为“文件移动/封装”，理论上不应引入功能性错误；若你想进一步消除 Warning，请把 Keil 的 Warning 列表贴出来，我再逐条处理。

### 12.8 后续可继续下沉的内容（可选）

- 如果你希望 `main.c` 更“像框架”，还可以继续把以下内容下沉：
  - 10ms 消抖策略封装为 `KeyDebounce`（状态机结构体）
  - `HB2` 心跳与 `ENABLE_RX2_HEX` 调试输出封装为 `SerialDebug` 模块

---

## 10. 串口输出链路改造（参考 Project_Refactor/Hardware/VOFA.c）

### 10.1 背景

- 现象：VOFA/串口助手收不到 `printf` 文本输出。
- 处理思路：参考 `Project_Refactor/Hardware/VOFA.c` 的实现，将发送改为 **TXE中断 + 环形缓冲**，并将数据输出改为 **VOFA JustFloat** 二进制帧（稳定、吞吐高）。

### 10.2 代码改动

- **USART2 发送改为环形缓冲**
  - 文件：`SYSTEM/usart/usart.c` / `SYSTEM/usart/usart.h`
  - 新增接口：
    - `USART2_SendByte()`
    - `USART2_SendBuffer()`
    - `USART2_SendString()`
  - `USART2_IRQHandler()`：在 `TXE` 分支中出队发送；队列为空自动关闭 `TXE` 中断。
  - `fputc()`：改为调用 `USART2_SendByte()`（避免阻塞发送）。

- **数据输出支持 VOFA JustFloat（二进制帧）**
  - 文件：`USER/main.c`
  - 新增函数：
    - `VOFA_SendJustFloat5(ch0..ch4)`
    - `VOFA_SendJustFloat4(ch0..ch3)`
  - 帧格式：连续发送 N 个 `float`（小端），最后追加尾巴 `00 00 80 7F`。
  - **当前（最新）JustFloat4 发送的4路数据（用于你要的4条曲线）**：
    - `ch0 = 目标速度差`（当前固定为 `0.0`）
    - `ch1 = 实际速度差`（`dr - dl`）
    - `ch2 = 目标偏航角`（当前固定为 `0.0`，等价于“目标偏航误差=0”）
    - `ch3 = 实际偏航角（误差）`（`yaw_err = wrap_deg(yaw - yaw0)`）
  - 如需回到5通道（yaw/yaw_err/dl/dr/corr）可切回调用 `VOFA_SendJustFloat5()`。
  - 启动时额外发送字符串：`VOFA_READY\r\n`（用于验证串口链路）。

### 10.3 使用说明（非常重要）

- **如果你用普通串口助手**
  - 你会看到“乱码/不可读字符”，这是正常的，因为现在是 **二进制 float 帧**，不是CSV文本。

- **如果你用 VOFA+**
  - 请选择/配置为 **JustFloat** 模式（5通道），并确认尾标识为 `00 00 80 7F`。
  - 波特率：115200。
  - 串口仍然是 `USART2`：`PA2(TX)->USBTTL RX`、共地。

### 10.4 数据流（从控制循环到PC端）

- **数据产生点**：`USER/main.c` 的 10ms 控制循环
  - 每 10ms 计算一次 `yaw/yaw_err/dl/dr/corr`
  - 调用 `VOFA_SendJustFloat5()` 将 5 路 float 打包成二进制帧

- **发送链路**：`USART2_SendByte()` -> TX ring buffer -> `USART2_IRQHandler(TXE)`
  - `USART2_SendByte()`：把字节入队（环形缓冲），并打开 `TXE` 中断
  - `USART2_IRQHandler()`：每次 `TXE` 触发，出队 1 字节写入 `USART2->DR`
  - 队列为空时自动关闭 `TXE` 中断，避免空转占用CPU

- **PC端接收**：VOFA+ 读取串口字节流
  - 以 `00 00 80 7F` 作为一帧结束标识
  - 每帧解析出 5 个 float 并绘图

### 10.5 为什么要用“TXE中断 + 环形缓冲”（而不是阻塞printf）

- **阻塞printf常见问题**
  - 发送大量数据时卡住主循环，导致控制周期抖动
  - 某些等待条件（例如等TC）会让输出不稳定或直接卡死

- **环形缓冲的好处**
  - 主循环只负责“把数据放进队列”，速度快
  - 真正的串口发送由中断硬件节拍驱动，吞吐稳定
  - 队列满会丢字节（本工程计数 `g_txDropBytes`），但不会把主循环卡死

### 10.6 关键函数与职责（便于你查代码）

- `SYSTEM/usart/usart.c`
  - `USART2_SendByte(b)`：入队1字节 + kick TXE
  - `USART2_SendBuffer(buf,len)`：循环入队
  - `USART2_SendString(s)`：循环入队字符串
  - `USART2_IRQHandler()`：
    - TXE分支：出队并写DR
    - RXNE分支：保留原工程接收解析（当前未用于VOFA绘图）

- `USER/main.c`
  - `VOFA_SendJustFloat5(ch0..ch4)`：
    - 把每个 float 作为 4 字节（小端序）发送
    - 最后发送帧尾 `00 00 80 7F`

### 10.7 VOFA JustFloat 帧格式（字节级说明）

- **JustFloat5 一帧总长度**：`5*4 + 4 = 24` 字节

1. `ch0`（float32，小端）4字节
2. `ch1`（float32，小端）4字节
3. `ch2`（float32，小端）4字节
4. `ch3`（float32，小端）4字节
5. `ch4`（float32，小端）4字节
6. 尾标识：`00 00 80 7F`

- **JustFloat4 一帧总长度**：`4*4 + 4 = 20` 字节

1. `ch0`（float32，小端）4字节
2. `ch1`（float32，小端）4字节
3. `ch2`（float32，小端）4字节
4. `ch3`（float32，小端）4字节
5. 尾标识：`00 00 80 7F`

> 备注：如果你用普通串口助手看，会看到“乱码”，这是因为它不是ASCII文本。

### 10.8 VOFA+ 配置步骤（按这个操作）

1. 打开 VOFA+
2. 选择串口：`COM18`
3. 波特率：`115200`
4. 协议/显示方式：选择 `JustFloat`
5. 通道数：
   - 使用 `VOFA_SendJustFloat4()`：设置为 `4`
   - 使用 `VOFA_SendJustFloat5()`：设置为 `5`
6. 帧尾：设置为 `00 00 80 7F`
7. 打开串口后，应该能看到对应通道数的曲线滚动

### 10.9 常见故障排查清单

- **完全没数据**
  - 检查接线：`PA2(TX)` 必须接 USB-TTL 的 `RX`
  - 必须共地：`GND`-`GND`
  - 检查波特率：115200

- **有数据但曲线不动/全是0**
  - 当前工程默认 `run_enable=0`，需按PB5启动后才进入控制输出
  - 但停止状态下也会发帧：此时 `yaw_err/corr` 为0，`dl/dr` 取决于你手动转轮

- **曲线乱跳（明显不是正常数值）**
  - VOFA+ 的帧尾/通道数设置不对（必须与代码一致：4通道或5通道 + `00 00 80 7F`）
  - 串口线序接反（PA2没接到TTL-RX）

- **掉帧/断续**
  - 发送太密/队列溢出导致丢字节（可把输出降频或减少通道）
  - USB-TTL质量或线太长，尝试换线/降波特率
