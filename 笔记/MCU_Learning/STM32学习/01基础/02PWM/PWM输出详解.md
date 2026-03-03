# STM32 PWM 输出详解

## 一、PWM 基础概念

### 1.1 什么是 PWM？

**PWM**（Pulse Width Modulation，**脉宽调制**）是一种通过改变方波信号的**占空比**来控制输出电压平均值的技术。

```
PWM 波形示意：

高电平时间(脉宽)
    ←─────→
    ┌────┐      ┌────┐      ┌────┐
    │    │      │    │      │    │
────┘    └──────┘    └──────┘    └──────
    └────────────┘
       周期 T

占空比 = 脉宽 / 周期 = Ton / T
```

### 1.2 关键参数

| 参数 | 符号 | 说明 | 公式 |
|:----:|:----:|:-----|:-----|
| **周期** | T | 一个完整波形的时间 | T = 1 / f |
| **频率** | f | 每秒的周期数 | f = 1 / T |
| **脉宽** | Ton | 高电平持续时间 | - |
| **占空比** | D | 高电平占周期的比例 | D = Ton / T × 100% |

**占空比与平均电压关系**：
```
平均电压 = 峰值电压 × 占空比

例如：3.3V峰值，50%占空比
平均电压 = 3.3V × 0.5 = 1.65V
```

### 1.3 常见占空比波形

```
0% 占空比（始终低电平）：
───────

25% 占空比：
┌──┐      ┌──┐      ┌──┐
│  │      │  │      │  │
───┘      └──┘      └──┘

50% 占空比（方波）：
┌───┐     ┌───┐     ┌───┐
│   │     │   │     │   │
───┘ └───┘ └───┘ └───┘

75% 占空比：
┌─────┐   ┌─────┐   ┌─────┐
│     │   │     │   │     │
──┘   └───┘   └───┘   └──

100% 占空比（始终高电平）：
┌────────────────────────
│
```

---

## 二、STM32 PWM 原理

### 2.1 PWM 产生方式

STM32 通过 **定时器的输出比较（Output Compare）** 功能产生 PWM：

```
定时器计数器(CNT)与比较值(CCR)比较：

CNT:  0    100   200   300   400   500   600   700   800   900   1000(ARR)
      ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
      │     │     │     │     │     │     │     │     │     │     │
      ▼     ▼     ▼     ▼     ▼     ▼     ▼     ▼     ▼     ▼     ▼

CCR = 300（比较值）
      │
      ▼
输出: 高    高    高    低    低    低    低    低    低    低    高...

占空比 = CCR / ARR = 300 / 1000 = 30%
```

### 2.2 PWM 模式

STM32 支持两种 PWM 模式：

| 模式 | 名称 | 说明 |
|:----:|:----:|:-----|
| **PWM1** | PWM 模式 1 | CNT < CCR 时输出有效电平（高电平） |
| **PWM2** | PWM 模式 2 | CNT < CCR 时输出无效电平（低电平） |

```
PWM1 模式（高电平有效）：
CNT:     0      CCR      ARR
         │       │        │
         ▼       ▼        ▼
输出:   高───────┐       ┌──
                 └───────┘

PWM2 模式（低电平有效）：
CNT:     0      CCR      ARR
         │       │        │
         ▼       ▼        ▼
输出:   低───────┐       ┌──
                 └───────┘
                 （与PWM1反相）
```

### 2.3 PWM 输出通道

STM32 定时器有多个输出通道：

| 定时器 | 通道数 | 通道名称 |
|:------:|:------:|:---------|
| 高级定时器(TIM1/TIM8) | 4 | CH1, CH2, CH3, CH4 |
| 通用定时器(TIM2~TIM5) | 4 | CH1, CH2, CH3, CH4 |
| 基本定时器(TIM6/TIM7) | 0 | 无输出通道 |

**互补输出 + 死区控制**（仅高级定时器）：
```
TIM1_CH1    ┌──┐  ┌──┐     ┌──┐  ┌──
（正向）     │  │  │  │     │  │
────────────┘  └──┘  └─────┘  └──

TIM1_CH1N        ┌──┐     ┌──┐  ┌──┐
（反向）          │  │     │  │  │  │
─────────────────┘  └─────┘  └──┘  └──

死区时间：
←──→
CH1 下降沿和 CH1N 上升沿之间的保护间隔
防止上下管同时导通
```

---

## 三、PWM 关键配置参数

### 3.1 时基单元配置

```c
typedef struct {
    uint16_t TIM_Prescaler;         // 预分频器 PSC
    uint16_t TIM_CounterMode;       // 计数模式（向上/向下/中心对齐）
    uint16_t TIM_Period;            // 自动重装载值 ARR
    uint16_t TIM_ClockDivision;     // 时钟分频因子
    uint8_t  TIM_RepetitionCounter; // 重复计数器（仅高级定时器）
} TIM_TimeBaseInitTypeDef;
```

### 3.2 频率与占空比计算

```
PWM 频率计算公式：
              时钟频率
PWM频率 = ─────────────────
          (PSC + 1) × (ARR + 1)

例如：72MHz时钟，PSC=71，ARR=999
PWM频率 = 72MHz / (72 × 1000) = 1kHz

占空比计算公式：
              CCR
占空比 = ───────── × 100%
          ARR + 1

例如：ARR=999，CCR=500
占空比 = 500 / 1000 = 50%
```

### 3.3 常用频率配置表

| 目标频率 | 时钟 | PSC | ARR | 分辨率 |
|:--------:|:----:|:---:|:---:|:------:|
| 1 kHz | 72 MHz | 71 | 999 | 1000级 |
| 10 kHz | 72 MHz | 71 | 99 | 100级 |
| 20 kHz | 72 MHz | 35 | 99 | 100级 |
| 50 kHz | 72 MHz | 143 | 9 | 10级 |
| 100 kHz | 72 MHz | 71 | 9 | 10级 |

---

## 四、PWM 代码配置步骤

### 4.1 完整初始化流程

```c
/**
 * @brief TIM2 PWM 初始化（CH1 - PA0）
 * @param freq: PWM频率(Hz)
 * @param duty: 初始占空比(0-1000对应0-100%)
 * @note 假设系统时钟72MHz
 */
void PWM_Init_TIM2(uint16_t freq, uint16_t duty)
{
    // 计算分频参数
    // PWM频率 = 72MHz / (PSC+1) / (ARR+1)
    uint16_t arr = 1000 - 1;      // ARR固定1000，分辨率千分之一
    uint16_t psc = (72000000 / freq / 1000) - 1;
    
    // 1. 使能时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    
    // 2. 配置GPIO（PA0 - TIM2_CH1）
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;  // 复用推挽输出
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // 3. 配置时基单元
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStruct;
    TIM_TimeBaseStruct.TIM_Prescaler = psc;
    TIM_TimeBaseStruct.TIM_CounterMode = TIM_CounterMode_Up;  // 向上计数
    TIM_TimeBaseStruct.TIM_Period = arr;
    TIM_TimeBaseStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStruct);
    
    // 4. 配置输出比较
    TIM_OCInitTypeDef TIM_OCInitStruct;
    TIM_OCInitStruct.TIM_OCMode = TIM_OCMode_PWM1;        // PWM模式1
    TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;  // 使能输出
    TIM_OCInitStruct.TIM_Pulse = duty;                    // 比较值CCR
    TIM_OCInitStruct.TIM_OCPolarity = TIM_OCPolarity_High; // 高电平有效
    TIM_OC1Init(TIM2, &TIM_OCInitStruct);                // 配置CH1
    
    // 5. 使能自动重装载预装载
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    
    // 6. 启动定时器
    TIM_Cmd(TIM2, ENABLE);
}

/**
 * @brief 设置PWM占空比
 * @param duty: 占空比值(0-1000)
 */
void PWM_SetDuty_TIM2(uint16_t duty)
{
    // 直接修改CCR寄存器
    TIM2->CCR1 = duty;
    // 或使用库函数：TIM_SetCompare1(TIM2, duty);
}
```

### 4.2 多通道PWM配置

```c
/**
 * @brief TIM3 四通道PWM初始化
 * @note TIM3_CH1-PB4, CH5-PB5, CH3-PB0, CH4-PB1（重映射）
 */
void PWM_Init_TIM3_Multi(void)
{
    // 1. 使能时钟和AFIO
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    
    // 2. 部分重映射：TIM3_CH1->PB4, CH2->PB5
    GPIO_PinRemapConfig(GPIO_PartialRemap_TIM3, ENABLE);
    
    // 3. 配置GPIO
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // 4. 时基单元配置（1kHz）
    TIM_TimeBaseInitTypeDef TIM_BaseStruct;
    TIM_BaseStruct.TIM_Prescaler = 71;
    TIM_BaseStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_BaseStruct.TIM_Period = 999;
    TIM_BaseStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM3, &TIM_BaseStruct);
    
    // 5. 配置4个通道相同的PWM参数
    TIM_OCInitTypeDef TIM_OCInitStruct;
    TIM_OCInitStruct.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStruct.TIM_Pulse = 0;        // 初始占空比0
    TIM_OCInitStruct.TIM_OCPolarity = TIM_OCPolarity_High;
    
    TIM_OC1Init(TIM3, &TIM_OCInitStruct);  // CH1
    TIM_OC2Init(TIM3, &TIM_OCInitStruct);  // CH2
    TIM_OC3Init(TIM3, &TIM_OCInitStruct);  // CH3
    TIM_OC4Init(TIM3, &TIM_OCInitStruct);  // CH4
    
    // 6. 使能预装载
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    
    // 7. 启动
    TIM_Cmd(TIM3, ENABLE);
}

/**
 * @brief 设置TIM3各通道占空比
 */
void PWM_SetDuty_TIM3_CH1(uint16_t duty) { TIM_SetCompare1(TIM3, duty); }
void PWM_SetDuty_TIM3_CH2(uint16_t duty) { TIM_SetCompare2(TIM3, duty); }
void PWM_SetDuty_TIM3_CH3(uint16_t duty) { TIM_SetCompare3(TIM3, duty); }
void PWM_SetDuty_TIM3_CH4(uint16_t duty) { TIM_SetCompare4(TIM3, duty); }
```

---

## 五、高级定时器 PWM（带死区）

### 5.1 互补输出 + 死区配置

```c
/**
 * @brief TIM1 高级定时器PWM初始化（带死区和刹车）
 * @note TIM1_CH1/CH1N - PA8/PB13
 */
void PWM_Init_TIM1_Adv(void)
{
    // 1. 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 | RCC_APB2Periph_GPIOA | 
                          RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    
    // 2. 配置GPIO
    GPIO_InitTypeDef GPIO_InitStruct;
    // CH1 - PA8
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_8;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // CH1N - PB13
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // 3. 时基单元（20kHz，适合电机控制）
    TIM_TimeBaseInitTypeDef TIM_BaseStruct;
    TIM_BaseStruct.TIM_Prescaler = 0;       // 不分频
    TIM_BaseStruct.TIM_CounterMode = TIM_CounterMode_CenterAligned1; // 中心对齐
    TIM_BaseStruct.TIM_Period = 1799;       // 72MHz/1800 = 40kHz（中心对齐后为20kHz）
    TIM_BaseStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_BaseStruct.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &TIM_BaseStruct);
    
    // 4. 配置输出比较（互补输出）
    TIM_OCInitTypeDef TIM_OCInitStruct;
    TIM_OCInitStruct.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;    // 主输出使能
    TIM_OCInitStruct.TIM_OutputNState = TIM_OutputNState_Enable;    // 互补输出使能
    TIM_OCInitStruct.TIM_Pulse = 900;       // 50%占空比
    TIM_OCInitStruct.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStruct.TIM_OCNPolarity = TIM_OCNPolarity_High;
    TIM_OCInitStruct.TIM_OCIdleState = TIM_OCIdleState_Reset;
    TIM_OCInitStruct.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OC1Init(TIM1, &TIM_OCInitStruct);
    
    // 5. 死区时间配置（1μs死区）
    TIM_BDTRInitTypeDef TIM_BDTRInitStruct;
    TIM_BDTRInitStruct.TIM_OSSRState = TIM_OSSRState_Enable;
    TIM_BDTRInitStruct.TIM_OSSIState = TIM_OSSIState_Enable;
    TIM_BDTRInitStruct.TIM_LOCKLevel = TIM_LOCKLevel_OFF;
    TIM_BDTRInitStruct.TIM_DeadTime = 72;   // 72个时钟周期 = 1μs（72MHz）
    TIM_BDTRInitStruct.TIM_Break = TIM_Break_Enable;
    TIM_BDTRInitStruct.TIM_BreakPolarity = TIM_BreakPolarity_Low;
    TIM_BDTRInitStruct.TIM_AutomaticOutput = TIM_AutomaticOutput_Enable;
    TIM_BDTRConfig(TIM1, &TIM_BDTRInitStruct);
    
    // 6. 使能预装载和主输出
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);     // 高级定时器必须调用！
    
    // 7. 启动
    TIM_Cmd(TIM1, ENABLE);
}
```

### 5.2 死区时间计算

```
死区时间设置（以72MHz为例）：

DTG[7:5]  |  时间计算公式          | 范围
----------|------------------------|------------
0xx       |  DT = DTG[7:0] × T     | 0~1.77μs
10x       |  DT = (64+DTG[5:0]) × 2T | 1.77~3.55μs
110       |  DT = (32+DTG[4:0]) × 8T | 3.55~7.11μs
111       |  DT = (32+DTG[4:0]) × 16T| 7.11~14.2μs

其中 T = 1/72MHz = 13.9ns

常用死区时间：
- 电机控制：0.5~5μs
- 开关电源：100ns~1μs
```

---

## 六、PWM 应用实例

### 6.1 LED 呼吸灯（渐变亮度）

```c
/**
 * @brief LED呼吸灯效果
 * @note 使用TIM2_CH1 - PA0
 */
void PWM_BreathLED(void)
{
    // 初始化：1kHz，初始占空比0
    PWM_Init_TIM2(1000, 0);
    
    uint16_t brightness = 0;
    int8_t dir = 1;  // 1=渐亮，-1=渐暗
    
    while(1)
    {
        // 修改占空比
        PWM_SetDuty_TIM2(brightness);
        
        // 渐变步进
        brightness += dir * 5;
        
        // 边界判断，改变方向
        if(brightness >= 1000) {
            brightness = 1000;
            dir = -1;  // 开始渐暗
        }
        else if(brightness == 0) {
            dir = 1;   // 开始渐亮
        }
        
        Delay_ms(10);  // 延时控制呼吸速度
    }
}
```

### 6.2 舵机控制（SG90）

```c
/**
 * @brief 舵机控制（SG90）
 * @note 50Hz PWM（20ms周期）
 * @note 0.5ms(0°)~2.5ms(180°)脉宽
 * 
 * ARR = 19999, PSC = 71 (72MHz)
 * PWM频率 = 72M / 72 / 20000 = 50Hz
 * 
 * 角度对应CCR值：
 * 0°   → 0.5ms → CCR = 500
 * 90°  → 1.5ms → CCR = 1500
 * 180° → 2.5ms → CCR = 2500
 */
void PWM_Servo_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    TIM_TimeBaseInitTypeDef TIM_BaseStruct;
    TIM_BaseStruct.TIM_Prescaler = 71;
    TIM_BaseStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_BaseStruct.TIM_Period = 19999;  // 20ms周期
    TIM_BaseStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &TIM_BaseStruct);
    
    TIM_OCInitTypeDef TIM_OCStruct;
    TIM_OCStruct.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCStruct.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCStruct.TIM_Pulse = 1500;      // 初始90°
    TIM_OCStruct.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM2, &TIM_OCStruct);
    
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
}

/**
 * @brief 设置舵机角度
 * @param angle: 0~180度
 */
void Servo_SetAngle(uint8_t angle)
{
    // 角度映射到CCR
    // 0°=500, 180°=2500
    uint16_t ccr = 500 + (angle * 2000 / 180);
    TIM_SetCompare1(TIM2, ccr);
}
```

### 6.3 直流电机调速

```c
/**
 * @brief 直流电机PWM调速
 * @note 使用L298N或TB6612驱动
 * @note TIM3_CH1-CH4控制4路电机
 */
#define MOTOR_MAX_SPEED 1000

void Motor_Init(void)
{
    PWM_Init_TIM3_Multi();
}

/**
 * @brief 设置电机速度
 * @param motor: 0~3 对应4个电机
 * @param speed: -1000~+1000（正负表示方向）
 */
void Motor_SetSpeed(uint8_t motor, int16_t speed)
{
    uint16_t pwm_val;
    uint8_t dir;
    
    // 方向判断
    if(speed >= 0) {
        dir = 1;
        pwm_val = speed;
    } else {
        dir = 0;
        pwm_val = -speed;
    }
    
    // 限幅
    if(pwm_val > MOTOR_MAX_SPEED) pwm_val = MOTOR_MAX_SPEED;
    
    // 根据电机号设置PWM和方向GPIO
    switch(motor)
    {
        case 0:
            TIM_SetCompare1(TIM3, pwm_val);
            GPIO_WriteBit(MOTOR_PORT, MOTOR1_DIR_PIN, (BitAction)dir);
            break;
        case 1:
            TIM_SetCompare2(TIM3, pwm_val);
            GPIO_WriteBit(MOTOR_PORT, MOTOR2_DIR_PIN, (BitAction)dir);
            break;
        // ...
    }
}
```

---

## 七、关键注意事项

### 7.1 常见问题

| 问题 | 可能原因 | 解决方法 |
|:-----|:---------|:---------|
| **无PWM输出** | GPIO未配置为复用模式 | 使用 `GPIO_Mode_AF_PP` |
| **频率不对** | PSC或ARR计算错误 | 检查公式：f = 72M/((PSC+1)*(ARR+1)) |
| **占空比跳变** | 未使能预装载 | 调用 `TIM_OCxPreloadConfig()` |
| **高级定时器无输出** | 未使能主输出 | 调用 `TIM_CtrlPWMOutputs()` |
| **输出有毛刺** | 死区时间设置不当 | 调整 `TIM_DeadTime` |

### 7.2 预装载的重要性

```c
// 必须使能CCR和ARR的预装载，否则占空比会跳变
TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);  // CCR预装载
TIM_ARRPreloadConfig(TIM2, ENABLE);               // ARR预装载

原理：
- 预装载使能：新值先写入影子寄存器，下一个周期才生效（平滑过渡）
- 预装载禁用：新值立即生效（可能导致PWM glitch）
```

### 7.3 GPIO引脚复用表

| 定时器 | 通道 | 默认引脚 | 重映射引脚 |
|:------:|:----:|:--------:|:----------:|
| TIM1 | CH1 | PA8 | PE9 |
| TIM1 | CH2 | PA9 | PE11 |
| TIM2 | CH1 | PA0 | PA15 |
| TIM2 | CH2 | PA1 | PB3 |
| TIM3 | CH1 | PA6 | PB4 |
| TIM3 | CH2 | PA7 | PB5 |
| TIM4 | CH1 | PB6 | PD12 |
| TIM4 | CH2 | PB7 | PD13 |

---

## 八、总结

```
PWM 配置要点：

1. 【时钟】使能定时器时钟和GPIO时钟
2. 【GPIO】配置为复用推挽输出（AF_PP）
3. 【时基】设置PSC和ARR确定PWM频率
4. 【输出比较】选择PWM模式，设置初始CCR
5. 【预装载】使能CCR和ARR预装载
6. 【高级定时器】额外使能主输出（MOE）
7. 【启动】调用TIM_Cmd使能定时器

占空比调节：修改CCR寄存器（TIM_SetComparex）
频率调节：修改ARR（影响分辨率）或PSC
```

**记忆口诀**：
- **时钟** → **GPIO** → **时基** → **OC** → **预装载** → **启动**
