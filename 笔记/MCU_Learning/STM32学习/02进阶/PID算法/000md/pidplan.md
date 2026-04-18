# ICM42688_ANGLE_IIC 高精度 PID 升级计划

## 1. 审阅范围

本次已重点审阅以下业务代码：

- `ICM42688_ANGLE_IIC/USER/main.c`
- `ICM42688_ANGLE_IIC/HARDWARE/IMU/*`
- `ICM42688_ANGLE_IIC/HARDWARE/ICM42688/*`
- `ICM42688_ANGLE_IIC/HARDWARE/IIC/*`
- `ICM42688_ANGLE_IIC/HARDWARE/MOTOR_CONTROL/*`
- `ICM42688_ANGLE_IIC/HARDWARE/APP_UTIL/*`
- `ICM42688_ANGLE_IIC/HARDWARE/EEPROM/*`
- `ICM42688_ANGLE_IIC/SYSTEM/usart/*`
- `ICM42688_ANGLE_IIC/SYSTEM/delay/*`
- `ICM42688_ANGLE_IIC/SYSTEM/sys/*`
- `ICM42688_ANGLE_IIC/000log-md/修改日志_直线PID_串口输出_IMU42688.md`

说明：

- `STM32F10x_FWLib`、`CORE`、`OBJ`、`startup` 属于标准库、启动文件或编译产物，本次只确认依赖关系，不作为业务逻辑重点审阅对象。

---

## 2. 当前工程实际控制链路

当前工程已经具备“能跑起来的直线控制骨架”，主链路如下：

1. `TIM4_IRQHandler()` 每 `100us` 累加一次 `nowtime`
2. `main.c` 每 `10ms` 进入一次控制周期
3. `IMU_getYawPitchRoll()` 读取 ICM42688 并做姿态解算
4. `encoder_delta()` 读取左右编码器增量 `dl/dr`
5. `Control_Step10ms()` 用 `yaw` 做外环纠偏，用 `dl/dr` 做双轮速度内环
6. `left()` / `right()` 输出 TB6612 PWM

当前控制结构本质上是：

- 外环：`yaw` 比例纠偏
- 内环：左右轮 PI 速度环
- 附加：静态 `trim_pwm` 补偿 + `pwm_step` 输出斜坡

这套结构适合“先跑直线、先调出车”的阶段，但还不属于高精度、强鲁棒、可扩展的控制框架。

---

## 3. 代码审阅结论

### 3.1 现有实现的优点

- 工程已经完成模块拆分，`PID`、`Control`、`Motor`、`Encoder`、`VOFA` 都有独立文件，后续升级基础是好的。
- 串口发送已经做了环形缓冲 + TXE 中断，调试链路比阻塞式 `printf` 稳定。
- 已经加入了 `trim_pwm`、`yaw_deadband`、`corr_max`、`pwm_step`，说明你已经开始处理“抖动、左右轮差异、输出过猛”这些真实工程问题。
- `encoder_delta()` 利用 `int16_t` 回绕特性处理 16 位编码器差值，思路是对的。

### 3.2 当前主要问题

#### P0：当前 `yaw` 不能被当成长期稳定的绝对航向

位置：

- `HARDWARE/IMU/IMU.c`

原因：

- 当前 AHRS 实际只用了 `acc + gyro`，没有磁力计。
- 在 6 轴条件下，重力只能约束 `roll/pitch`，无法长期校正 `yaw` 漂移。
- 也就是说，当前 `yaw0` 锁定后，短时间内可以用，时间一长一定会漂。

影响：

- 小车短距离走直线可能可用。
- 中长距离直线、低速匀速、反复启停后，航向误差会逐步累积。
- 如果后面直接把这一路 `yaw` 当“高精度航向真值”，PID 再怎么调都到不了真正精密。

结论：

- 后续必须把“航向角控制”升级成“短时 IMU 航向 + 编码器差速约束 + 陀螺零偏跟踪”的融合思路。

#### P0：姿态修正条件写法有问题

位置：

- `HARDWARE/IMU/IMU.c`

当前写法本质上是：

```c
if (ex != 0.0f && ey != 0.0f && ez != 0.0f) {
    ...
}
```

问题：

- 这要求 `ex/ey/ez` 三个量必须同时非零才做修正。
- 实际姿态误差在某一轴刚好接近 0 时，其他两轴依然可能需要修正。
- 这种写法会让姿态修正被错误地跳过。

建议：

- 改为基于加速度模长合法性判断。
- 或改为 `if (!(ax == 0 && ay == 0 && az == 0))` 这类“测量有效”判定。

#### P1：传感器输出速率与控制速率完全重合，没有余量

位置：

- `HARDWARE/ICM42688/icm42688.c`

当前：

- 加速度计 ODR = `100Hz`
- 陀螺仪 ODR = `100Hz`
- 主控制周期也是 `10ms = 100Hz`

问题：

- 采样频率和控制频率一样，几乎没有滤波和状态估计余量。
- 对高精度控制来说，这会让导数项、角速度估计、扰动抑制都很吃亏。

建议：

- IMU 采样提高到 `500Hz` 或 `1kHz`
- 速度环跑 `200Hz`
- 航向角环跑 `50~100Hz`

#### P1：当前外环不是高精度 PID，只是比例差速映射

位置：

- `HARDWARE/MOTOR_CONTROL/Control.c`

当前：

```c
c = ctx->yaw_k * e;
ref_l = ctx->base_ref - c;
ref_r = ctx->base_ref + c;
```

问题：

- 没有 `yaw rate` 阻尼项。
- 没有积分项去消除慢性偏差。
- 没有轨迹前馈、曲率约束、目标斜坡。
- 没有区分“航向角环”和“航向角速度环”。

影响：

- 车会出现“能纠偏，但纠偏不高级”的典型现象：
- 要么反应慢。
- 要么加大增益后抖。
- 要么左右轮噪声一大就来回修正。

#### P1：速度环还是基础 PI，没有真正工程化

位置：

- `HARDWARE/MOTOR_CONTROL/PID.c`

当前问题：

- `dt` 固定写死为 `0.01f`
- 没有测量低通滤波
- 没有微分滤波
- 没有反算式抗积分饱和
- 没有无扰切换
- 没有前馈项
- 没有死区补偿
- `PID_A/PID_B` 通过全局 `Speed1/Speed2` 取测量值，可维护性较差

结论：

- 现在是“基础能用 PID”
- 不是“高级精密 PID”

#### P1：参数还没有真正形成可修改接口

位置：

- `USER/main.c`
- `HARDWARE/EEPROM/eeprom.c`

问题：

- 关键参数仍散落在 `main.c` 局部变量里。
- `eeprom.h` 已经设计了存储结构，但 `eeprom.c` 还是空的。
- 当前没有统一的“参数加载 / 参数保存 / 参数在线修改 / 参数导出”接口。

结论：

- 如果后面参数增多，工程会越来越难调。
- 高精度控制一定要把参数系统独立出来。

#### P2：编码器速度值仍是“原始计数/10ms”，没有统一物理量

位置：

- `HARDWARE/APP_UTIL/Encoder.c`
- `USER/main.c`

问题：

- 当前直接使用 `dl/dr` 做控制。
- 这对“先跑起来”没问题，但不利于做高级控制。
- 后续如果你要加前馈、路径控制、角速度环，最好统一成：
- `wheel_speed_mps`
- `yaw_rate_dps` 或 `yaw_rate_radps`

#### P2：I2C 驱动里存在重复实现，后续维护成本高

位置：

- `HARDWARE/IIC/myiic.c`
- `HARDWARE/IIC/myiic.h`

问题：

- 同一文件里保留了两套软件 I2C 风格实现。
- 头文件里还声明了 `static` 原型，不适合作为公共接口。

结论：

- 不影响当前能跑。
- 但后续建议清理，避免继续堆叠。

---

## 4. 高精度 PID 的目标定义

建议把目标明确成可验收指标，而不是“感觉更稳”：

- 直线 2m 内横向偏差尽量压到 `3cm ~ 8cm`
- 定速稳态误差尽量小于 `3%`
- 启动到稳定时间尽量小于 `300ms`
- 航向角超调尽量小于 `2°`
- 输出长期不频繁打满 PWM
- 启停切换时不出现明显“猛冲”
- 参数可串口在线修改
- 参数可保存到 Flash
- 日志能同时看到目标值、测量值、误差、饱和状态、滤波后状态

---

## 5. 推荐的升级控制架构

### 5.1 总体架构：三层串级 + 多速率调度

建议改成：

1. 状态估计层
2. 参考生成层
3. 控制执行层

推荐频率：

- IMU 采样：`500Hz`
- 编码器速度估计：`200Hz`
- 速度环：`200Hz`
- 航向角速度环：`100~200Hz`
- 航向角环：`50~100Hz`
- 串口调试输出：`20~50Hz`

### 5.2 状态估计层建议

不要再直接把当前 `ypr[0]` 作为唯一航向依据，建议拆成以下状态：

- `gyro_z_dps`：原始 Z 轴角速度
- `gyro_z_bias`：在线估计的陀螺零偏
- `yaw_rate_fused`：融合后的航向角速度
- `heading_est_deg`：积分得到的短时航向角
- `wheel_speed_l/r_mps`：左右轮速度
- `vehicle_speed_mps`：车体平均速度

推荐做法：

- 陀螺仪负责短时高带宽角速度
- 编码器差速负责给航向变化提供慢速约束
- 静止时自动更新陀螺零偏
- 不把 6 轴 `yaw` 当绝对真值，只当短时估计量

### 5.3 控制层建议

推荐改成三级控制：

1. 最外层：航向角环
   - 输入：`target_heading_deg`
   - 输出：`target_yaw_rate_dps`

2. 中间层：航向角速度环
   - 输入：`target_yaw_rate_dps`
   - 输出：`speed_diff_ref`

3. 最内层：左右轮速度环
   - 输入：`speed_ref_l/r`
   - 输出：`pwm_l/r`

这样做的好处：

- 航向角环负责慢变量，不容易乱抖
- 角速度环负责抑制“转过头”和“回拉慢”
- 速度环专注电机跟踪，不和航向环互相抢活

### 5.4 前馈一定要加

高精度控制不能只靠 PID 纠错，建议加入：

- 速度前馈 `Kv * v_ref`
- 加速度前馈 `Ka * a_ref`
- 静摩擦补偿 `Ks * sign(v_ref)`
- 电池电压补偿 `V_nominal / V_bat`

作用：

- 小车更容易“先到位再微调”
- 减少积分项负担
- 低速起步更稳

### 5.5 参考值也要限速，而不只是 PWM 限速

当前只有输出 `pwm_step` 斜坡，建议再加：

- `base_speed_ref` 斜坡
- `heading_ref` 变化率限制
- `yaw_rate_ref` 变化率限制

说明：

- 高级控制更推荐“限制目标变化速度”
- 而不是只在最后一层“限制输出变化速度”

---

## 6. 推荐的 PID 设计细节

### 6.1 速度环 PID 建议

建议最少具备以下特性：

- 积分限幅
- 反算式抗积分饱和
- 微分对测量而不是对误差
- 微分一阶低通滤波
- 输出限幅
- 参考前馈
- 输出无扰切换

推荐公式：

```c
u = u_ff + Kp * e + Ki * integral - Kd * d_meas
```

说明：

- `d_meas` 对测量值求导更抗“目标跳变冲击”
- `integral` 建议用带限幅和反算的形式

### 6.2 航向角环建议

航向角环不要再直接输出 PWM，应该输出目标角速度：

```c
yaw_rate_ref = PID_heading(target_heading, heading_est)
```

这样可以把“方向角误差”转换成“应该转多快”，比直接差速映射高级很多。

### 6.3 航向角速度环建议

建议加一个专门的 yaw-rate 环：

```c
speed_diff_ref = PID_yaw_rate(yaw_rate_ref, yaw_rate_fused)
```

优点：

- 可显著改善纠偏阻尼
- 可减少单纯靠 `yaw_k` 带来的来回摆
- 对高精度直线控制非常重要

### 6.4 编码器速度估计建议

推荐不要直接用裸 `dl/dr`，而是：

1. 先换算成真实速度
2. 再做一阶低通
3. 再送入速度环

示例：

```c
speed_l_mps = alpha * speed_l_mps + (1.0f - alpha) * raw_speed_l_mps;
```

说明：

- 这样对微分项和高增益闭环更友好

---

## 7. 建议新增的可修改接口

下面是推荐接口，不是要求你一字不差照抄，而是建议你按这个方向重构。

### 7.1 高级 PID 数据结构

```c
typedef struct
{
    float kp;              // 比例系数
    float ki;              // 积分系数
    float kd;              // 微分系数
    float kaw;             // 反算抗积分饱和系数
    float dt;              // 控制周期，单位 s
    float tau_d;           // 微分低通时间常数

    float out_min;         // 输出下限
    float out_max;         // 输出上限
    float i_min;           // 积分下限
    float i_max;           // 积分上限

    float integral;        // 积分状态
    float d_state;         // 微分滤波状态
    float prev_meas;       // 上一拍测量值
    float prev_out;        // 上一拍输出值
} pid_adv_t;
```

### 7.2 底盘控制参数结构

```c
typedef struct
{
    float wheel_radius_m;      // 轮半径
    float wheel_track_m;       // 轮距
    float encoder_cpr;         // 编码器每圈计数

    float speed_lpf_hz;        // 速度低通截止频率
    float yaw_lpf_hz;          // 航向角速度低通截止频率

    float ff_kv;               // 速度前馈
    float ff_ka;               // 加速度前馈
    float ff_ks;               // 静摩擦补偿
    float trim_pwm;            // 左右轮静态补偿

    float heading_deadband_deg;// 航向死区
    float yaw_rate_limit_dps;  // 角速度目标限幅
    float accel_limit_mps2;    // 速度目标加速度限幅
} chassis_param_t;
```

### 7.3 控制目标结构

```c
typedef struct
{
    float target_speed_mps;    // 目标前进速度
    float target_heading_deg;  // 目标航向角
    uint8_t heading_hold_en;   // 是否启用航向保持
} control_target_t;
```

### 7.4 控制调试结构

```c
typedef struct
{
    float heading_est_deg;     // 当前航向估计
    float yaw_rate_dps;        // 当前航向角速度
    float speed_l_mps;         // 左轮速度
    float speed_r_mps;         // 右轮速度
    float speed_ref_l_mps;     // 左轮目标速度
    float speed_ref_r_mps;     // 右轮目标速度
    float pwm_l;               // 左轮最终输出
    float pwm_r;               // 右轮最终输出
    uint8_t sat_l;             // 左轮是否饱和
    uint8_t sat_r;             // 右轮是否饱和
} control_debug_t;
```

### 7.5 推荐函数接口

```c
void PID_AdvInit(pid_adv_t* pid, const pid_adv_t* init_cfg);
void PID_AdvReset(pid_adv_t* pid, float meas, float out_init);
float PID_AdvUpdate(pid_adv_t* pid, float ref, float meas, float ff);

void Control_Init(const chassis_param_t* param);
void Control_SetTarget(const control_target_t* target);
void Control_UpdateSensor(float gyro_z_dps,
                          int16_t enc_l_delta,
                          int16_t enc_r_delta,
                          float dt);
void Control_RunFastLoop(void);     // 200Hz
void Control_RunSlowLoop(void);     // 50~100Hz
void Control_GetDebug(control_debug_t* dbg);

void Param_LoadDefault(void);
void Param_LoadFromFlash(void);
void Param_SaveToFlash(void);
void Param_SetByName(const char* key, float value);
float Param_GetByName(const char* key);
```

### 7.6 串口在线调参接口建议

建议支持下面这些简单命令：

- `GET ALL`
- `SET SPD_KP 4.8`
- `SET SPD_KI 1.2`
- `SET YAW_KP 2.0`
- `SET FF_KS 85`
- `SAVE`
- `LOAD`
- `RESET_PID`

说明：

- 这样后面你调 PID 会非常舒服
- 不用每次改代码、编译、烧录

---

## 8. 建议修改到哪些文件

### 8.1 `USER/main.c`

建议保留职责：

- 初始化
- 周期调度
- 启停控制
- 日志输出
- 调参命令入口

不要继续把大量 PID 细节塞回 `main.c`。

### 8.2 `HARDWARE/IMU/IMU.c`

建议修改：

- 修正姿态修正条件判断
- 增加陀螺零偏在线估计状态
- 单独输出 `gyro_z_dps`
- 提供“融合后的航向角速度”和“短时航向角估计”
- 给关键状态加中文注释

### 8.3 `HARDWARE/ICM42688/icm42688.c`

建议修改：

- 提高 ODR 到 `500Hz`
- 增加低通配置
- 导出原始温度值，后面可做温漂补偿
- 提供原始值读取接口和工程单位读取接口

### 8.4 `HARDWARE/MOTOR_CONTROL/PID.c/.h`

建议修改：

- 废弃全局 `Speed1/Speed2`
- `PID_A/PID_B` 可保留兼容层，但底层换成 `pid_adv_t`
- 增加前馈、抗积分饱和、微分滤波、重置接口

### 8.5 `HARDWARE/MOTOR_CONTROL/Control.c/.h`

建议修改：

- 加入航向角环
- 加入航向角速度环
- 把 `base_ref` 改成物理量目标
- 把 `corr` 改成层级化输出，不再直接一把梭映射
- 输出调试量结构体

### 8.6 `HARDWARE/EEPROM/eeprom.c`

建议修改：

- 真正实现参数保存/加载
- 至少保存：
- 速度环 PID
- 航向角环 PID
- 航向角速度环 PID
- 前馈参数
- `trim_pwm`
- 轮半径、轮距、编码器系数

### 8.7 `SYSTEM/usart/usart.c`

建议修改：

- 增加简单命令解析
- 支持参数读取/设置/保存
- 支持输出当前控制调试结构

---

## 9. 推荐分阶段实施顺序

### 阶段 0：先做测量可信化

目标：

- 确认电机方向
- 确认编码器正负方向
- 确认左右轮速度换算正确
- 确认陀螺 Z 轴方向正确

建议输出：

- `gyro_z_dps`
- `speed_l_mps`
- `speed_r_mps`
- `heading_est_deg`

### 阶段 1：先单独做好双轮速度环

做法：

- 暂时关闭航向角环
- 固定左右轮同目标速度
- 调好左右轮速度闭环

目标：

- 左右轮都能稳定跟踪目标速度
- 不抖、不持续打满

### 阶段 2：加入静摩擦补偿和速度前馈

做法：

- 测试小车最低起步 PWM
- 提取 `Ks`
- 根据速度建立 `Kv`

结果：

- 起步更稳
- 积分压力更小

### 阶段 3：加入航向角速度环

做法：

- 先不用航向角环
- 手动给 `yaw_rate_ref`
- 看小车转向响应是否平顺

目标：

- 不过冲
- 不来回摆

### 阶段 4：加入航向角环

做法：

- 航向角环输出 `yaw_rate_ref`
- 航向角速度环再输出左右轮差速

目标：

- 直线纠偏更柔和
- 不再靠一个 `yaw_k` 生硬硬拽

### 阶段 5：加入在线调参 + Flash 存储

结果：

- 不用每次改代码
- 每次调好参数可直接保存

### 阶段 6：最后再做高阶优化

可选项：

- 电池电压补偿
- 温漂补偿
- 自适应 `trim`
- 卡尔曼或互补融合增强
- 轨迹跟踪而不是只走直线

---

## 10. 调参建议

推荐按下面顺序调，不要一上来全开：

1. 先只调左右轮速度环
2. 再调前馈
3. 再调航向角速度环
4. 最后调航向角环

### 10.1 速度环调参方法

- 先让 `ki = 0`
- 只调 `kp` 到“响应快但不抖”
- 再慢慢增加 `ki`
- `kd` 最后再考虑，而且必须建立在速度值已经滤波的前提下

### 10.2 航向环调参方法

- 先调 `yaw_rate` 环
- 再调 `heading` 环
- 外环一定比内环慢

### 10.3 不建议直接做的事情

- 不建议直接继续增大当前 `yaw_k`
- 不建议直接对原始 `dl/dr` 上 `kd`
- 不建议继续长期依赖当前 6 轴 `yaw` 当绝对航向

---

## 11. 日志与注释要求

你要求“要提供注释、要可修改、要高级精准”，这里给出明确建议。

### 11.1 注释建议

每个核心结构体字段都要说明：

- 单位
- 作用
- 什么时候更新
- 是否可在线调参

每个控制函数都要说明：

- 输入是什么物理量
- 输出是什么物理量
- 频率是多少
- 是否允许在中断或主循环里调用

### 11.2 必须保留的调试量

建议至少输出这些量：

- `heading_ref`
- `heading_est`
- `heading_err`
- `yaw_rate_ref`
- `yaw_rate_meas`
- `speed_ref_l/r`
- `speed_meas_l/r`
- `pwm_l/r`
- `sat_l/r`
- `gyro_bias_z`
- `dt`

这样后面定位问题会非常快。

---

## 12. 最终建议结论

### 12.1 这套工程当前适合做什么

- 可以继续作为“小车直线控制验证平台”
- 可以作为高精度 PID 升级的基础工程
- 不建议再在现有“单层 yaw 比例纠偏”上无限堆参数

### 12.2 我最建议你优先改的 6 件事

1. 修正 `IMU.c` 里的姿态修正条件判断
2. 不再把当前 6 轴 `yaw` 当长期绝对航向
3. 把 IMU 采样率从 `100Hz` 提高到 `500Hz`
4. 把速度环升级成带前馈和抗饱和的高级 PID
5. 增加 `yaw_rate` 中间环
6. 把参数系统独立出来并实现 Flash 保存

### 12.3 最推荐的改造方向

一句话总结：

把当前“yaw 比例差速 + 双轮 PI”的结构，升级成“状态估计 + 航向角环 + 航向角速度环 + 双轮高级速度环 + 参数系统”的工程化框架。

这样才能真正向“高级、精准、可维护、可调参”的 PID 靠近。


