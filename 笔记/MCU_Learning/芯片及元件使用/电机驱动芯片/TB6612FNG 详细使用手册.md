# TB6612FNG 双路直流电机驱动芯片 详细使用手册

> **文档版本**: 1.0  
> **基于**: Toshiba TB6612FNG Datasheet (2014-10-01)  
> **适用芯片**: TB6612FNG (SSOP24封装)

---

## 目录

1. [芯片概述](#1-芯片概述)
2. [电气规格](#2-电气规格)
3. [引脚定义与功能](#3-引脚定义与功能)
4. [控制逻辑真值表](#4-控制逻辑真值表)
5. [典型应用电路](#5-典型应用电路)
6. [PWM调速控制](#6-pwm调速控制)
7. [STM32驱动代码](#7-stm32驱动代码)
8. [多个TB6612FNG级联](#8-多个tb6612fng级联)
9. [故障排除与注意事项](#9-故障排除与注意事项)

---

## 1. 芯片概述

### 1.1 芯片简介

TB6612FNG是东芝(Toshiba)推出的**双路直流电机驱动IC**，采用低导通电阻的**LD MOS输出结构**，适用于电池供电的便携设备和机器人应用。

### 1.2 关键特性

| 参数 | 规格 | 说明 |
|------|------|------|
| **电源电压 (VM)** | 2.5V - 13.5V (典型15V Max) | 电机供电 |
| **逻辑电压 (VCC)** | 2.7V - 5.5V | 控制逻辑供电 |
| **输出电流** | 1.2A (平均) / 3.2A (峰值) | 每通道 |
| **导通电阻** | 0.5Ω (典型) | @ VM≥5V, 上下桥总和 |
| **待机电流** | < 1μA | 省电模式 |
| **封装** | SSOP24 | 0.65mm引脚间距 |
| **工作温度** | -20°C ~ +85°C | |

### 1.3 内置功能

- ✅ **双H桥驱动** - 可同时驱动2个直流电机
- ✅ **4种控制模式** - 正转/反转/制动/停止
- ✅ **待机省电模式** - <1μA待机电流
- ✅ **热关断保护 (TSD)** - 过热自动关闭
- ✅ **低电压锁定 (UVLO)** - 防止低电压误操作
- ✅ **PWM调速支持** - 支持20kHz以上PWM

### 1.4 应用场景

- 机器人底盘驱动
- 智能小车
- 电动玩具
- 相机自动对焦
- 打印机/扫描仪进纸机构
- 无人机云台控制

---

## 2. 电气规格

### 2.1 绝对最大额定值

| 参数 | 符号 | 最小 | 最大 | 单位 |
|------|------|------|------|------|
| 电机电源电压 | VM | -0.2 | 15.0 | V |
| 逻辑电源电压 | VCC | -0.2 | 6.0 | V |
| 输出电流 (平均) | IOUT | - | 1.2 | A |
| 输出电流 (峰值) | IOUT(peak) | - | 3.2 | A |
| 功耗 | PD | - | 1.0 | W |
| 工作温度 | Toper | -20 | +85 | °C |
| 存储温度 | Tstg | -55 | +150 | °C |

### 2.2 推荐工作条件

| 参数 | 符号 | 最小 | 典型 | 最大 | 单位 |
|------|------|------|------|------|------|
| 电机电源电压 | VM | 2.5 | 5.0 | 13.5 | V |
| 逻辑电源电压 | VCC | 2.7 | 5.0 | 5.5 | V |
| 高电平输入电压 | VIN(H) | 0.7VCC | - | VCC | V |
| 低电平输入电压 | VIN(L) | 0 | - | 0.3VCC | V |
| PWM频率 | fPWM | - | 10k | 100k | Hz |

### 2.3 电气特性 (@ VCC=5V, VM=5V, Ta=25°C)

| 参数 | 条件 | 最小 | 典型 | 最大 | 单位 |
|------|------|------|------|------|------|
| **导通电阻** |  |  |  |  |  |
| 高侧 + 低侧 | VM≥5V, IOUT=0.5A | - | 0.5 | 0.8 | Ω |
| 高侧 | IOUT=0.5A | - | 0.25 | - | Ω |
| 低侧 | IOUT=0.5A | - | 0.25 | - | Ω |
| **待机电流** | STBY=L, VM=15V | - | 0 | 10 | μA |
| **静态电流** | 待机模式 | - | 1.5 | 3.0 | mA |
| **输入电流** | VIN=5V | - | 5 | 20 | μA |

### 2.4 开关特性

| 参数 | 条件 | 典型 | 单位 |
|------|------|------|------|
| 输出上升时间 | RL=15Ω | 0.25 | μs |
| 输出下降时间 | RL=15Ω | 0.25 | μs |
| 死区时间 | 内置 | 1.5 | μs |
| 传播延迟 | 输入到输出 | 0.5 | μs |

---

## 3. 引脚定义与功能

### 3.1 引脚分配图 (SSOP24)

```
        +------------------------+
   VM1 -| 1                   24 |- VM1
   VM2 -| 2                   23 |- PWMA
   GND -| 3                   22 |- AIN1
   GND -| 4                   21 |- AIN2
   VM3 -| 5                   20 |- VCC
   VM2 -| 6                   19 |- STBY
   PGND1 -| 7                 18 |- GND
   PGND1 -| 8                 17 |- BIN1
   AO1   -| 9                 16 |- BIN2
   AO2   -| 10                15 |- PWMB
   BO2   -| 11                14 |- VM3
   PGND2 -| 12                13 |- BO1
        +------------------------+
```

### 3.2 引脚功能详细说明

#### 电源引脚

| 引脚 | 名称 | 类型 | 功能说明 |
|------|------|------|----------|
| 1, 24 | VM1 | 电源 | A通道电机供电 (VM1=VM2=VM3) |
| 2, 5, 6, 14 | VM2, VM3 | 电源 | B通道电机供电 |
| 20 | VCC | 电源 | 逻辑电路供电 (2.7-5.5V) |
| 3, 4, 18 | GND | 地 | 逻辑地 |
| 7, 8 | PGND1 | 地 | A通道功率地 |
| 12 | PGND2 | 地 | B通道功率地 |

**电源连接注意事项**:
- VM1, VM2, VM3应连接在一起
- PGND和GND应在一点连接，避免地环路
- VCC与VM可以不同电压 (如VCC=3.3V, VM=12V)

#### 控制输入引脚

| 引脚 | 名称 | 类型 | 功能 | 内部下拉 |
|------|------|------|------|----------|
| 22 | AIN1 | 输入 | A通道输入1 | 200kΩ |
| 21 | AIN2 | 输入 | A通道输入2 | 200kΩ |
| 23 | PWMA | 输入 | A通道PWM输入 | 200kΩ |
| 17 | BIN1 | 输入 | B通道输入1 | 200kΩ |
| 16 | BIN2 | 输入 | B通道输入2 | 200kΩ |
| 15 | PWMB | 输入 | B通道PWM输入 | 200kΩ |
| 19 | STBY | 输入 | 待机控制 (L有效) | 200kΩ |

**内部下拉电阻**: 所有控制输入引脚内部集成200kΩ下拉电阻，未连接时默认为低电平。

#### 输出引脚

| 引脚 | 名称 | 类型 | 功能 |
|------|------|------|------|
| 9 | AO1 | 输出 | A通道输出1 |
| 10 | AO2 | 输出 | A通道输出2 |
| 13 | BO1 | 输出 | B通道输出1 |
| 11 | BO2 | 输出 | B通道输出2 |

---

## 4. 控制逻辑真值表

### 4.1 A通道控制逻辑

| STBY | AIN1 | AIN2 | PWMA | AO1 | AO2 | 模式 |
|:----:|:----:|:----:|:----:|:---:|:---:|:----:|
| L | X | X | X | Z | Z | **待机** (Standby) |
| H | L | L | H | L | L | **停止/制动** (Short Brake) |
| H | H | L | H | H | L | **正转** (CW) |
| H | L | H | H | L | H | **反转** (CCW) |
| H | H | H | H | Z | Z | **停止** (Stop) |
| H | H | L | PWM | PWM | L | **正转调速** |
| H | L | H | PWM | L | PWM | **反转调速** |

### 4.2 B通道控制逻辑

| STBY | BIN1 | BIN2 | PWMB | BO1 | BO2 | 模式 |
|:----:|:----:|:----:|:----:|:---:|:---:|:----:|
| L | X | X | X | Z | Z | **待机** (Standby) |
| H | L | L | H | L | L | **停止/制动** (Short Brake) |
| H | H | L | H | H | L | **正转** (CW) |
| H | L | H | H | L | H | **反转** (CCW) |
| H | H | H | H | Z | Z | **停止** (Stop) |

### 4.3 模式说明

| 模式 | 说明 | 电流路径 |
|------|------|----------|
| **待机 (Standby)** | 芯片关闭，功耗<1μA | 高阻态 |
| **停止 (Stop)** | 输出高阻，电机自由滑行 | 开路 |
| **制动 (Short Brake)** | 电机两端短接，快速制动 | AO1→PGND1→AO2 |
| **正转 (CW)** | 电流从AO1流向AO2 | VM1→AO1→电机→AO2→PGND1 |
| **反转 (CCW)** | 电流从AO2流向AO1 | VM1→AO2→电机→AO1→PGND1 |

---

## 5. 典型应用电路

### 5.1 基本应用电路 (单电机)

```
                    VM (7.2V-12V)
                      |
    +-----------------+-----------------+
    |                 |                 |
   VM1               VM2               VM3
    |                 |                 |
+---+---+       +-----+-----+     +-----+-----+
|       |       |           |     |           |
| AO1   |       |  Motor A  |     |   Motor B |
|       |       |           |     | (可选)    |
|       |       |           |     |           |
|       |       +-----+-----+     +-----+-----+
|       |             |                 |
| AO2   |            AO2               BO2
|       |             |                 |
|  TB6612FNG          |                 |
|       |             |                 |
|       +-------------+-----------------+
|                   |
| AIN1 <------------+---- MCU GPIO
| AIN2 <------------------ MCU GPIO
| PWMA <------------------ MCU PWM
|                   |
| STBY <------------------ MCU GPIO (可接VCC常使能)
|                   |
| VCC <------------------- 3.3V/5V
|                   |
+----+--------------+
     |
    GND
```

### 5.2 推荐外围电路

```
              VM (5V-12V)
                |
    +-----------+-----------+
    |           |           |
   C1         VM1          C2
  10µF         |           100nF
    |           |           |
    +-----+-----+     +-----+
          |               |
       TB6612FNG         |
          |               |
    +-----+---------------+-----+
    |                           |
   C3                         C4
  10µF                       100nF
    |                           |
    +------------+--------------+
                 |
                GND

C1, C3: 电解电容或陶瓷电容 (≥10µF)
C2, C4: 陶瓷电容 (100nF), 靠近芯片放置
```

### 5.3 与STM32连接电路

```
STM32F103                    TB6612FNG
                    
PA0 (GPIO) ---------> AIN1
PA1 (GPIO) ---------> AIN2
PA2 (TIM2_CH3) ------> PWMA
PA3 (GPIO) ---------> STBY
PB0 (GPIO) ---------> BIN1
PB1 (GPIO) ---------> BIN2
PB6 (TIM4_CH1) ------> PWMB
                    
3.3V ---------------> VCC
GND ---------------> GND
                    
5V/12V -------------> VM1/VM2/VM3
```

**电平匹配说明**:
- TB6612FNG的VCC可以接3.3V或5V
- 当VCC=3.3V时，高电平阈值约2.3V，STM32的3.3V输出可直接驱动
- STBY建议接STM32 GPIO，方便控制待机

---

## 6. PWM调速控制

### 6.1 PWM频率选择

| 应用场景 | 推荐频率 | 说明 |
|----------|----------|------|
| 普通直流电机 | 1kHz - 5kHz | 人耳听不到噪音 |
| 静音要求 | > 20kHz | 超过人耳听力范围 |
| 高效率驱动 | 10kHz - 20kHz | 开关损耗与响应平衡 |

### 6.2 PWM调速原理

```
正转调速模式:
AIN1 = H, AIN2 = L, PWMA = PWM信号

PWM占空比 -> 平均电压 -> 电机转速
   0%     ->   0V      ->  停止
   50%    ->   VM/2    ->  半速
   100%   ->   VM      ->  全速
```

### 6.3 双向PWM调速 (高级)

使用锁定反相PWM (Locked Anti-Phase PWM):
```
AIN1 = PWM, AIN2 = ~PWM (反相)

占空比 > 50%: 正转，速度 ∝ (占空比 - 50%)
占空比 = 50%: 停止，两端短接 (制动)
占空比 < 50%: 反转，速度 ∝ (50% - 占空比)

优点: 单PWM线控制方向和速度
缺点: 50%时持续制动，发热较大
```

---

## 7. STM32驱动代码

### 7.1 HAL库驱动代码

```c
#include "stm32f1xx_hal.h"

/* TB6612FNG引脚定义 */
#define AIN1_PIN        GPIO_PIN_0
#define AIN1_PORT       GPIOA
#define AIN2_PIN        GPIO_PIN_1
#define AIN2_PORT       GPIOA
#define PWMA_PIN        GPIO_PIN_2
#define PWMA_PORT       GPIOA
#define PWMA_CHANNEL    TIM_CHANNEL_3

#define BIN1_PIN        GPIO_PIN_0
#define BIN1_PORT       GPIOB
#define BIN2_PIN        GPIO_PIN_1
#define BIN2_PORT       GPIOB
#define PWMB_PIN        GPIO_PIN_6
#define PWMB_PORT       GPIOB
#define PWMB_CHANNEL    TIM_CHANNEL_1

#define STBY_PIN        GPIO_PIN_3
#define STBY_PORT       GPIOA

TIM_HandleTypeDef htim2;  // 用于PWMA
TIM_HandleTypeDef htim4;  // 用于PWMB

/* 初始化GPIO */
void TB6612_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // 使能时钟
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // AIN1, AIN2, STBY - 推挽输出
    GPIO_InitStruct.Pin = AIN1_PIN | AIN2_PIN | STBY_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(AIN1_PORT, &GPIO_InitStruct);
    
    // BIN1, BIN2 - 推挽输出
    GPIO_InitStruct.Pin = BIN1_PIN | BIN2_PIN;
    HAL_GPIO_Init(BIN1_PORT, &GPIO_InitStruct);
    
    // PWMA, PWMB - 复用推挽 (PWM输出)
    GPIO_InitStruct.Pin = PWMA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PWMA_PORT, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = PWMB_PIN;
    HAL_GPIO_Init(PWMB_PORT, &GPIO_InitStruct);
    
    // 初始状态: 待机
    HAL_GPIO_WritePin(STBY_PORT, STBY_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AIN1_PORT, AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AIN2_PORT, AIN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BIN1_PORT, BIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BIN2_PORT, BIN2_PIN, GPIO_PIN_RESET);
}

/* 初始化PWM (10kHz) */
void TB6612_PWM_Init(void) {
    // TIM2用于PWMA (PA2 - TIM2_CH3)
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 72 - 1;      // 72MHz / 72 = 1MHz
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 100 - 1;        // 1MHz / 100 = 10kHz PWM
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&htim2);
    
    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;  // 初始占空比0%
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, PWMA_CHANNEL);
    HAL_TIM_PWM_Start(&htim2, PWMA_CHANNEL);
    
    // TIM4用于PWMB (PB6 - TIM4_CH1)
    __HAL_RCC_TIM4_CLK_ENABLE();
    htim4.Instance = TIM4;
    htim4.Init.Prescaler = 72 - 1;
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4.Init.Period = 100 - 1;
    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&htim4);
    
    HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, PWMB_CHANNEL);
    HAL_TIM_PWM_Start(&htim4, PWMB_CHANNEL);
}

/* 设置占空比 (0-100) */
void MotorA_SetSpeed(int16_t speed) {
    // speed: -100 ~ +100 (负值反转，正值正转)
    if (speed > 100) speed = 100;
    if (speed < -100) speed = -100;
    
    if (speed > 0) {
        // 正转
        HAL_GPIO_WritePin(AIN1_PORT, AIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(AIN2_PORT, AIN2_PIN, GPIO_PIN_RESET);
        __HAL_TIM_SET_COMPARE(&htim2, PWMA_CHANNEL, speed);
    } else if (speed < 0) {
        // 反转
        HAL_GPIO_WritePin(AIN1_PORT, AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(AIN2_PORT, AIN2_PIN, GPIO_PIN_SET);
        __HAL_TIM_SET_COMPARE(&htim2, PWMA_CHANNEL, -speed);
    } else {
        // 停止 (制动)
        HAL_GPIO_WritePin(AIN1_PORT, AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(AIN2_PORT, AIN2_PIN, GPIO_PIN_RESET);
        __HAL_TIM_SET_COMPARE(&htim2, PWMA_CHANNEL, 0);
    }
}

void MotorB_SetSpeed(int16_t speed) {
    if (speed > 100) speed = 100;
    if (speed < -100) speed = -100;
    
    if (speed > 0) {
        HAL_GPIO_WritePin(BIN1_PORT, BIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(BIN2_PORT, BIN2_PIN, GPIO_PIN_RESET);
        __HAL_TIM_SET_COMPARE(&htim4, PWMB_CHANNEL, speed);
    } else if (speed < 0) {
        HAL_GPIO_WritePin(BIN1_PORT, BIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BIN2_PORT, BIN2_PIN, GPIO_PIN_SET);
        __HAL_TIM_SET_COMPARE(&htim4, PWMB_CHANNEL, -speed);
    } else {
        HAL_GPIO_WritePin(BIN1_PORT, BIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BIN2_PORT, BIN2_PIN, GPIO_PIN_RESET);
        __HAL_TIM_SET_COMPARE(&htim4, PWMB_CHANNEL, 0);
    }
}

/* 待机控制 */
void TB6612_Standby(uint8_t standby) {
    if (standby) {
        HAL_GPIO_WritePin(STBY_PORT, STBY_PIN, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(STBY_PORT, STBY_PIN, GPIO_PIN_SET);
        HAL_Delay(1);  // 退出待机后等待1ms
    }
}

/* 初始化TB6612 */
void TB6612_Init(void) {
    TB6612_GPIO_Init();
    TB6612_PWM_Init();
    TB6612_Standby(0);  // 退出待机
}

/* 停止电机 (制动) */
void MotorA_Stop(void) {
    HAL_GPIO_WritePin(AIN1_PORT, AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AIN2_PORT, AIN2_PIN, GPIO_PIN_RESET);
    __HAL_TIM_SET_COMPARE(&htim2, PWMA_CHANNEL, 100);  // 100%制动
}

void MotorA_Brake(void) {
    MotorA_Stop();  // Short Brake
}

void MotorB_Stop(void) {
    HAL_GPIO_WritePin(BIN1_PORT, BIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BIN2_PORT, BIN2_PIN, GPIO_PIN_RESET);
    __HAL_TIM_SET_COMPARE(&htim4, PWMB_CHANNEL, 100);
}
```

### 7.2 标准库驱动代码 (SPL)

```c
#include "stm32f10x.h"

/* 简化版标准库实现 */
void Motor_Init(void) {
    // GPIO初始化
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM4, ENABLE);
    
    GPIO_InitTypeDef GPIO_InitStructure;
    
    // AIN1, AIN2, STBY
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // TIM2 CH3 (PA2) - PWM
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // TIM2配置: 10kHz PWM
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructure.TIM_Period = 99;
    TIM_TimeBaseStructure.TIM_Prescaler = 71;  // 72MHz/72=1MHz
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
    
    // PWM模式
    TIM_OCInitTypeDef TIM_OCInitStructure;
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC3Init(TIM2, &TIM_OCInitStructure);
    
    TIM_Cmd(TIM2, ENABLE);
    TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);
    
    // 退出待机
    GPIO_SetBits(GPIOA, GPIO_Pin_3);  // STBY = H
}

void Motor_SetSpeed(int8_t speed) {
    // speed: -100 ~ +100
    if (speed > 0) {
        GPIO_SetBits(GPIOA, GPIO_Pin_0);    // AIN1 = H
        GPIO_ResetBits(GPIOA, GPIO_Pin_1);  // AIN2 = L
        TIM_SetCompare3(TIM2, speed);
    } else if (speed < 0) {
        GPIO_ResetBits(GPIOA, GPIO_Pin_0); // AIN1 = L
        GPIO_SetBits(GPIOA, GPIO_Pin_1);   // AIN2 = H
        TIM_SetCompare3(TIM2, -speed);
    } else {
        GPIO_ResetBits(GPIOA, GPIO_Pin_0 | GPIO_Pin_1);  // 制动
        TIM_SetCompare3(TIM2, 0);
    }
}
```

### 7.3 差速转向控制 (小车底盘)

```c
typedef struct {
    int16_t left_speed;   // 左轮速度: -100 ~ +100
    int16_t right_speed;  // 右轮速度: -100 ~ +100
} CarSpeed_t;

/* 小车运动控制 */
void Car_Move(int8_t linear, int8_t angular) {
    // linear: 前进/后退速度 (-100 ~ +100)
    // angular: 旋转速度 (-100 ~ +100, 左负右正)
    
    int16_t left = linear + angular;
    int16_t right = linear - angular;
    
    // 限制在有效范围
    if (left > 100) left = 100;
    if (left < -100) left = -100;
    if (right > 100) right = 100;
    if (right < -100) right = -100;
    
    MotorA_SetSpeed(left);   // 左电机
    MotorB_SetSpeed(right);  // 右电机
}

/* 预设动作 */
void Car_Forward(uint8_t speed) {
    Car_Move(speed, 0);
}

void Car_Backward(uint8_t speed) {
    Car_Move(-speed, 0);
}

void Car_TurnLeft(uint8_t speed) {
    Car_Move(0, -speed);  // 原地左转
}

void Car_TurnRight(uint8_t speed) {
    Car_Move(0, speed);   // 原地右转
}

void Car_Stop(void) {
    MotorA_Stop();
    MotorB_Stop();
}
```

---

## 8. 多个TB6612FNG级联

### 8.1 4电机驱动方案

使用2个TB6612FNG驱动4个电机:

```
MCU GPIO分配:

TB6612 #1 (左前 + 左后):
  - AIN1  <- PA0
  - AIN2  <- PA1  
  - PWMA  <- PA2 (TIM2_CH3)  -> 左前电机
  - BIN1  <- PA4
  - BIN2  <- PA5
  - PWMB  <- PA3 (TIM2_CH4)  -> 左后电机
  - STBY  <- 共用 (PA15)

TB6612 #2 (右前 + 右后):
  - AIN1  <- PB0
  - AIN2  <- PB1
  - PWMA  <- PB6 (TIM4_CH1)  -> 右前电机
  - BIN1  <- PB12
  - BIN2  <- PB13
  - PWMB  <- PB7 (TIM4_CH2)  -> 右后电机
  - STBY  <- 共用 (PA15)

所有TB6612共享:
  - VM  <- 12V (电机电源)
  - VCC <- 3.3V (逻辑电源)
```

---

## 9. 故障排除与注意事项

### 9.1 常见问题

| 问题 | 可能原因 | 解决方法 |
|------|----------|----------|
| 电机不转 | STBY为低电平 | 检查STBY引脚电平 |
| | PWM占空比为0 | 检查PWM输出 |
| | 电源电压不足 | 确保VM > 电机额定电压 |
| | 电机接线错误 | 检查AO1/AO2接线 |
| 电机抖动 | PWM频率太低 | 提高PWM频率到>1kHz |
| | 电源纹波过大 | 加大电源滤波电容 |
| 芯片发热 | 电流过大 | 检查电机电流，散热 |
| | 频繁正反转 | 增加切换延迟 |
| 速度不均 | 电机差异 | 单独校准或使用编码器反馈 |

### 9.2 设计注意事项

1. **电源去耦**
   - VM和GND之间必须放置≥10µF电解电容+100nF陶瓷电容
   - 电容尽量靠近芯片引脚放置

2. **地线布局**
   - 功率地(PGND)和逻辑地(GND)应分开走线
   - 在一点连接，避免大电流影响逻辑电路

3. **散热考虑**
   - 连续电流>0.8A时考虑散热片
   - 峰值电流3.2A仅适用于短暂启动

4. **EMI抑制**
   - 电机输出端可串联小电感(10-100µH)
   - 电机两端并联104电容吸收尖峰

5. **保护措施**
   - VM电源建议串联保险丝或PTC
   - 电机输出端并联TVS管防止反电动势

### 9.3 安全警告

⚠️ **重要提醒**:
- MOS结构对静电敏感，操作时请使用防静电手环
- 避免输出端短路到电源或地
- 切换方向前建议先停止100ms，减少冲击电流
- 峰值电流不得超过3.2A，否则可能损坏芯片

---

## 附录

### A. 参数速查表

| 参数 | 值 | 单位 |
|------|-----|------|
| VM电压范围 | 2.5 - 13.5 | V |
| VCC电压范围 | 2.7 - 5.5 | V |
| 输出电流 (平均) | 1.2 | A |
| 输出电流 (峰值) | 3.2 | A |
| 导通电阻 | 0.5 | Ω |
| 待机电流 | < 1 | μA |
| 死区时间 | 1.5 | μs |
| 开关频率 | < 100 | kHz |

### B. 引脚速查表

| 引脚 | 功能 | 类型 | 内部下拉 |
|------|------|------|----------|
| 1, 24 | VM1 | 电源 | - |
| 9 | AO1 | 输出 | - |
| 10 | AO2 | 输出 | - |
| 22 | AIN1 | 输入 | 200kΩ |
| 21 | AIN2 | 输入 | 200kΩ |
| 23 | PWMA | 输入 | 200kΩ |
| 13 | BO1 | 输出 | - |
| 11 | BO2 | 输出 | - |
| 17 | BIN1 | 输入 | 200kΩ |
| 16 | BIN2 | 输入 | 200kΩ |
| 15 | PWMB | 输入 | 200kΩ |
| 19 | STBY | 输入 | 200kΩ |
| 20 | VCC | 电源 | - |

---

*本文档基于Toshiba TB6612FNG Datasheet整理*
*整理日期: 2026年3月*
