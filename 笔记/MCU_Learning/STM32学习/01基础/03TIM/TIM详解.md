# TIM 详解（定时器）

> TIM（Timer）是 STM32 中最强大、最灵活的外设之一，支持定时、计数、PWM输出、输入捕获、编码器接口等多种功能。掌握 TIM 是嵌入式开发的核心技能。

---

![[Pasted image 20260303221233.png]]

## 一、TIM 基本概念

### 1.1 什么是 TIM？

**TIM** 全称 **Timer**（定时器），是 STM32 中用于**计时和计数**的外设模块。

**核心功能**：

- **定时中断**：产生精确的时间基准（如1ms系统节拍）
- **PWM输出**：生成可调占空比的方波（电机控制、LED调光）
- **输入捕获**：测量外部信号的频率和脉宽
- **输出比较**：在指定时间产生事件
- **编码器接口**：读取正交编码器（电机转速/位置）
- **单脉冲模式**：产生单个脉冲信号

### 1.2 STM32F1 定时器分类

| 类型                 | 定时器     | 位数 | 计数方向   | 特殊功能              | 总线 |
| :------------------- | :--------- | :--: | :--------- | :-------------------- | :--- |
| **高级定时器** | TIM1、TIM8 | 16位 | 上/下/中心 | 死区、刹车、互补输出  | APB2 |
| **通用定时器** | TIM2~5     | 16位 | 上/下/中心 | PWM、输入捕获、编码器 | APB1 |
| **基本定时器** | TIM6、TIM7 | 16位 | 向上       | 仅定时/DAC触发        | APB1 |
| **看门狗**     | IWDG、WWDG |  -  | -          | 系统监控复位          | 独立 |
| **系统滴答**   | SysTick    | 24位 | 向下       | 系统节拍（内核）      | 内核 |

### 1.3 定时器核心特性对比

| 特性                 | 高级 (TIM1/8) | 通用 (TIM2~5) | 基本 (TIM6/7) |
| :------------------- | :------------ | :------------ | :------------ |
| **互补输出**   | ✅ 支持       | ❌ 不支持     | ❌ 不支持     |
| **死区插入**   | ✅ 支持       | ❌ 不支持     | ❌ 不支持     |
| **刹车输入**   | ✅ 支持       | ❌ 不支持     | ❌ 不支持     |
| **输入捕获**   | ✅ 4通道      | ✅ 4通道      | ❌ 无         |
| **输出比较**   | ✅ 4通道      | ✅ 4通道      | ❌ 无         |
| **编码器接口** | ✅ 支持       | ✅ 支持       | ❌ 不支持     |
| **触发DAC**    | ✅ 支持       | ✅ 支持       | ✅ 支持       |
| **DMA**        | ✅ 支持       | ✅ 支持       | ✅ 支持       |

---

## 二、定时器时钟源与时基

### 2.1 定时器时钟来源

```
┌─────────────────────────────────────────────────────────────┐
│                      定时器时钟源选择                          │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  内部时钟 (CK_INT)                                          │
│      │                                                       │
│      ├── TIM1/TIM8 (APB2) ← 72MHz                           │
│      │      └── 经倍频后 → 72MHz (如果APB2预分频≠1)          │
│      │                                                       │
│      ├── TIM2~7 (APB1) ← 36MHz                              │
│      │      └── 经倍频后 → 72MHz (APB1预分频≠1，实际×2)     │
│      │                                                       │
│  外部时钟模式1：ETR引脚                                      │
│  外部时钟模式2：TIx引脚（输入捕获）                           │
│  内部触发输入 (ITRx)：其他定时器触发                          │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 定时器时钟频率计算

| 定时器    | 所在总线 | 总线频率 | 实际时钟频率    | 说明                  |
| :-------- | :------- | :------: | :-------------- | :-------------------- |
| TIM1/TIM8 | APB2     |  72MHz  | 72MHz           | APB2预分频=1，不倍频  |
| TIM2~5    | APB1     |  36MHz  | **72MHz** | APB1预分频=2，实际×2 |
| TIM6/TIM7 | APB1     |  36MHz  | **72MHz** | APB1预分频=2，实际×2 |

> **重要**：APB1 上的定时器时钟是总线频率的 **2倍**！这是 STM32 的设计特性。

### 2.3 时基单元（Time Base Unit）

```
┌─────────────────────────────────────────────────────────┐
│                     时基单元结构                          │
├─────────────────────────────────────────────────────────┤
│                                                          │
│   ┌─────────────┐    ┌─────────────┐    ┌───────────┐ │
│   │  预分频器    │ →  │   计数器     │ →  │  自动重载  │ │
│   │  (PSC)      │    │   (CNT)      │    │  (ARR)    │ │
│   └─────────────┘    └─────────────┘    └───────────┘ │
│          │                   │                  │      │
│          ▼                   ▼                  ▼      │
│   CK_CNT = CK_PSC/(PSC+1)   计数             重装值   │
│                                                          │
│   时序：                                                 │
│   CK_PSC → [PSC+1分频] → CK_CNT → CNT计数 → 达到ARR   │
│                                           ↓            │
│                                    产生更新事件(UEV)    │
│                                    /更新中断(UI)       │
└─────────────────────────────────────────────────────────┘
```

### 2.4 定时时间计算公式

```
定时频率 = 定时器时钟 / (预分频值 + 1) / (自动重载值 + 1)

定时周期 = (预分频值 + 1) × (自动重载值 + 1) / 定时器时钟频率

示例（1ms定时中断）：
- 定时器时钟 = 72MHz
- 预分频值 (PSC) = 72-1 = 71
- 自动重载值 (ARR) = 1000-1 = 999
- 定时周期 = (71+1) × (999+1) / 72M = 72 × 1000 / 72M = 1ms
```

---

## 三、定时器工作模式

### 3.1 计数模式

| 模式               | 说明                         | 应用场景                |
| :----------------- | :--------------------------- | :---------------------- |
| **向上计数** | 从0计数到ARR，然后溢出重装   | 最常用的定时模式        |
| **向下计数** | 从ARR计数到0，然后溢出重装   | 需要递减计数的场景      |
| **中心对齐** | 先向上到ARR，再向下到0，循环 | PWM对称波形（电机控制） |

```c
// 向上计数模式（最常用）
TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;

// 向下计数模式
TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Down;

// 中心对齐模式（3种细分模式）
TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_CenterAligned1;
TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_CenterAligned2;
TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_CenterAligned3;
```

### 3.2 PWM 模式

```
PWM 原理示意图：

ARR = 999 (周期)
PSC = 71  (72分频)

CNT:    0    CCR    ARR
        │     │      │
        ▼     ▼      ▼
        ┌─────┐      ┌─────┐      ┌─────┐
        │     │      │     │      │     │
        │  高  │  低  │  高  │  低  │  高  │
        │     │      │     │      │     │
        └─────┘      └─────┘      └─────┘
      
        |←─高电平时间─→|
        |←─────周期─────→|
      
占空比 = CCR / (ARR + 1) = CCR / 1000
```

| PWM 模式           | 说明                                | 特点            |
| :----------------- | :---------------------------------- | :-------------- |
| **PWM模式1** | 向上计数：CNT < CCR 时输出有效电平  | 最常用的PWM模式 |
| **PWM模式2** | 向上计数：CNT ≥ CCR 时输出有效电平 | 与模式1互补     |

### 3.3 输入捕获模式

```
输入捕获原理：

外部信号 ──────┐
              │
              ▼
         ┌─────────┐
         │ TI1 引脚 │
         └────┬────┘
              │
              ▼
         ┌─────────┐     ┌─────────┐
         │ 边沿检测 │ →  │ 捕获CNT  │ → CCR1 = 当前CNT值
         │ (滤波)  │     │ 到 CCR1 │
         └─────────┘     └─────────┘

应用：
- 测量脉冲宽度（一个引脚捕获上升沿和下降沿）
- 测量频率（两个连续上升沿的时间差）
- 测量占空比（PWMI模式）
```

### 3.4 编码器接口模式

```
编码器信号 → 定时器计数

A相 ──┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐
      └─┘ └─┘ └─┘ └─┘ └─
B相 ───┐ ┌─┐ ┌─┐ ┌─┐ ┌─
       └─┘ └─┘ └─┘ └─┘ └
    
      ↑   ↑   ↑   ↑   ↑
    
定时器根据A/B相的相位关系自动增/减计数：
- A超前B（正转）：CNT++
- B超前A（反转）：CNT--

应用：电机转速和位置反馈
```

---

## 四、TIM 配置和使用

### 4.1 时基初始化结构体

```c
typedef struct
{
    uint16_t TIM_Prescaler;         // 预分频值 (0~65535)
    uint16_t TIM_CounterMode;       // 计数模式（向上/向下/中心对齐）
    uint16_t TIM_Period;            // 自动重载值 ARR (0~65535)
    uint16_t TIM_ClockDivision;     // 时钟分频（影响死区时间）
    uint8_t TIM_RepetitionCounter;  // 重复计数器（仅高级定时器）
} TIM_TimeBaseInitTypeDef;
```

### 4.2 常用 TIM 函数

| 函数                                          | 功能                    |
| --------------------------------------------- | ----------------------- |
| `TIM_TimeBaseInit(TIMx, &initStruct)`       | 初始化时基单元          |
| `TIM_Cmd(TIMx, ENABLE)`                     | 启用/禁用定时器         |
| `TIM_ITConfig(TIMx, TIM_IT_Update, ENABLE)` | 使能更新中断            |
| `TIM_SetCompare1(TIMx, value)`              | 设置输出比较值 CCR1     |
| `TIM_GetCapture1(TIMx)`                     | 读取输入捕获值          |
| `TIM_OCxInit()`                             | 初始化输出比较通道      |
| `TIM_ICxInit()`                             | 初始化输入捕获通道      |
| `TIM_ARRPreloadConfig(ENABLE)`              | 使能ARR预装载（重要！） |

### 4.3 基础定时器配置（1ms中断）

```c
/**
 * @brief  TIM2 基础定时配置（1ms中断）
 * @note   APB1时钟36MHz，实际定时器时钟72MHz
 */
void TIM2_Base_Config(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;
  
    // ==================== 第1步：使能时钟 ====================
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
  
    // ==================== 第2步：配置时基 ====================
    // 定时器时钟 = 72MHz (APB1=36MHz × 2)
    // 目标频率 = 1kHz (1ms周期)
    // 
    // 计算：72MHz / 1000Hz = 72000
    // 分解：72 × 1000 = 72000
    // PSC = 72-1 = 71, ARR = 1000-1 = 999
  
    TIM_TimeBaseInitStruct.TIM_Prescaler = 72 - 1;      // 72分频 → 1MHz
    TIM_TimeBaseInitStruct.TIM_Period = 1000 - 1;     // 计数1000次 → 1ms
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStruct.TIM_RepetitionCounter = 0;
  
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStruct);
  
    // 使能ARR预装载（重要：防止立即生效导致异常）
    TIM_ARRPreloadConfig(TIM2, ENABLE);
  
    // ==================== 第3步：配置中断 ====================
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);  // 使能更新中断
  
    NVIC_InitStruct.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
  
    // ==================== 第4步：启动定时器 ====================
    TIM_Cmd(TIM2, ENABLE);
}

// 中断服务函数
volatile uint32_t g_SystemTick = 0;

void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
      
        g_SystemTick++;  // 系统节拍计数
      
        // 每1000ms（1秒）执行一次
        if (g_SystemTick % 1000 == 0)
        {
            // 秒级任务
        }
    }
}
```

### 4.4 PWM 输出配置

```c
/**
 * @brief  TIM3 PWM输出配置（PA6、PA7）
 * @note   频率1kHz，可调占空比
 */
void TIM3_PWM_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
    TIM_OCInitTypeDef TIM_OCInitStruct;
  
    // ==================== 第1步：使能时钟 ====================
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
  
    // ==================== 第2步：配置GPIO ====================
    // PA6: TIM3_CH1, PA7: TIM3_CH2
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;  // 复用推挽输出
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
  
    // ==================== 第3步：配置时基 ====================
    // PWM频率 = 1kHz
    // 72MHz / 1kHz = 72000 = 72 × 1000
    TIM_TimeBaseInitStruct.TIM_Prescaler = 72 - 1;      // 1MHz计数频率
    TIM_TimeBaseInitStruct.TIM_Period = 1000 - 1;     // 1kHz PWM频率
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStruct);
  
    // ==================== 第4步：配置PWM输出 ====================
    TIM_OCInitStruct.TIM_OCMode = TIM_OCMode_PWM1;        // PWM模式1
    TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStruct.TIM_Pulse = 500;                     // 初始占空比50%
    TIM_OCInitStruct.TIM_OCPolarity = TIM_OCPolarity_High; // 有效电平为高
  
    // 初始化通道1 (PA6)
    TIM_OC1Init(TIM3, &TIM_OCInitStruct);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);  // 使能预装载
  
    // 初始化通道2 (PA7)
    TIM_OCInitStruct.TIM_Pulse = 250;  // 通道2初始占空比25%
    TIM_OC2Init(TIM3, &TIM_OCInitStruct);
    TIM_OC2PreloadConfig(TIM3, TIM_OCPreload_Enable);
  
    // 使能ARR预装载
    TIM_ARRPreloadConfig(TIM3, ENABLE);
  
    // 启动定时器
    TIM_Cmd(TIM3, ENABLE);
}

// 动态修改占空比
void TIM3_SetDutyCycle(uint16_t channel, uint16_t duty)
{
    // duty: 0~1000 对应 0%~100%
    if (duty > 1000) duty = 1000;
  
    switch (channel)
    {
        case 1: TIM_SetCompare1(TIM3, duty); break;
        case 2: TIM_SetCompare2(TIM3, duty); break;
        // TIM3还有CH3、CH4
        case 3: TIM_SetCompare3(TIM3, duty); break;
        case 4: TIM_SetCompare4(TIM3, duty); break;
    }
}
```

---

## 五、输入捕获配置（测量脉冲宽度）

```c
/**
 * @brief  TIM2 输入捕获配置（PA0）
 * @note   测量外部信号的高电平时间
 */
void TIM2_IC_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
    TIM_ICInitTypeDef TIM_ICInitStruct;
  
    // 使能时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
  
    // PA0: TIM2_CH1
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
  
    // 时基配置（1MHz计数频率，最大65ms测量范围）
    TIM_TimeBaseInitStruct.TIM_Prescaler = 72 - 1;
    TIM_TimeBaseInitStruct.TIM_Period = 0xFFFF;
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStruct);
  
    // 输入捕获配置
    TIM_ICInitStruct.TIM_Channel = TIM_Channel_1;
    TIM_ICInitStruct.TIM_ICPolarity = TIM_ICPolarity_Rising;  // 上升沿捕获
    TIM_ICInitStruct.TIM_ICSelection = TIM_ICSelection_DirectTI;
    TIM_ICInitStruct.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    TIM_ICInitStruct.TIM_ICFilter = 0x0;
    TIM_ICInit(TIM2, &TIM_ICInitStruct);
  
    // 使能捕获中断
    TIM_ITConfig(TIM2, TIM_IT_CC1, ENABLE);
  
    NVIC_InitTypeDef NVIC_InitStruct;
    NVIC_InitStruct.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
  
    TIM_Cmd(TIM2, ENABLE);
}

// 测量脉冲宽度的状态机
volatile uint8_t g_IC_State = 0;  // 0:等待上升沿, 1:等待下降沿
volatile uint16_t g_IC_RisingValue = 0;
volatile uint16_t g_IC_FallingValue = 0;
volatile uint32_t g_IC_PulseWidth = 0;

void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_CC1) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_CC1);
      
        if (g_IC_State == 0)
        {
            // 捕获到上升沿
            g_IC_RisingValue = TIM_GetCapture1(TIM2);
            g_IC_State = 1;
          
            // 切换到下降沿捕获
            TIM_OC1PolarityConfig(TIM2, TIM_ICPolarity_Falling);
        }
        else
        {
            // 捕获到下降沿
            g_IC_FallingValue = TIM_GetCapture1(TIM2);
          
            // 计算脉宽（微秒）
            if (g_IC_FallingValue >= g_IC_RisingValue)
            {
                g_IC_PulseWidth = (g_IC_FallingValue - g_IC_RisingValue);
            }
            else
            {
                // 计数器溢出情况
                g_IC_PulseWidth = (0xFFFF - g_IC_RisingValue + g_IC_FallingValue);
            }
          
            g_IC_State = 0;
            // 切换回上升沿捕获
            TIM_OC1PolarityConfig(TIM2, TIM_ICPolarity_Rising);
        }
    }
}
```

---

## 六、编码器接口配置

```c
/**
 * @brief  TIM2 编码器接口配置（PA0、PA1）
 * @note   正交编码器接口，自动增减计数
 */
void TIM2_Encoder_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
  
    // 使能时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
  
    // PA0: TIM2_CH1 (编码器A相)
    // PA1: TIM2_CH2 (编码器B相)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
  
    // 时基配置
    TIM_TimeBaseInitStruct.TIM_Prescaler = 0;  // 编码器模式不分频
    TIM_TimeBaseInitStruct.TIM_Period = 0xFFFF;
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStruct);
  
    // 编码器接口配置
    // 模式3：两相都计数（4倍频，精度最高）
    TIM_EncoderInterfaceConfig(TIM2, 
                               TIM_EncoderMode_TI12,  // 两相都计数
                               TIM_ICPolarity_Rising, // A相不反相
                               TIM_ICPolarity_Rising);// B相不反相
  
    // 清除计数器
    TIM_SetCounter(TIM2, 0);
  
    // 启动
    TIM_Cmd(TIM2, ENABLE);
}

// 读取编码器值
int16_t Encoder_GetPosition(void)
{
    return (int16_t)TIM_GetCounter(TIM2);
}

// 清零编码器计数
void Encoder_Reset(void)
{
    TIM_SetCounter(TIM2, 0);
}
```

---

## 七、高级定时器特殊功能（TIM1/TIM8）

### 7.1 互补输出与死区

```c
/**
 * @brief  TIM1 高级定时器配置（带死区插入）
 * @note   用于电机驱动，防止上下桥臂直通
 */
void TIM1_Advanced_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
    TIM_OCInitTypeDef TIM_OCInitStruct;
    TIM_BDTRInitTypeDef TIM_BDTRInitStruct;
  
    // 使能时钟（注意TIM1在APB2）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 | RCC_APB2Periph_GPIOA | 
                           RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
  
    // GPIO配置
    // PA8: TIM1_CH1, PA9: TIM1_CH2, PA10: TIM1_CH3
    // PB13: TIM1_CH1N, PB14: TIM1_CH2N, PB15: TIM1_CH3N (互补输出)
    // PB12: TIM1_BKIN (刹车输入)
  
    // ... GPIO初始化代码 ...
  
    // 时基配置
    TIM_TimeBaseInitStruct.TIM_Prescaler = 0;
    TIM_TimeBaseInitStruct.TIM_Period = 999;  // 72MHz/1000 = 72kHz PWM
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStruct.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStruct);
  
    // PWM输出配置
    TIM_OCInitStruct.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStruct.TIM_OutputNState = TIM_OutputNState_Enable;  // 互补输出使能
    TIM_OCInitStruct.TIM_Pulse = 500;
    TIM_OCInitStruct.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStruct.TIM_OCNPolarity = TIM_OCNPolarity_High;
    TIM_OCInitStruct.TIM_OCIdleState = TIM_OCIdleState_Reset;
    TIM_OCInitStruct.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
  
    TIM_OC1Init(TIM1, &TIM_OCInitStruct);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
  
    // 死区和刹车配置（高级定时器特有）
    TIM_BDTRInitStruct.TIM_OSSRState = TIM_OSSRState_Disable;
    TIM_BDTRInitStruct.TIM_OSSIState = TIM_OSSIState_Disable;
    TIM_BDTRInitStruct.TIM_LOCKLevel = TIM_LOCKLevel_OFF;
    TIM_BDTRInitStruct.TIM_DeadTime = 128;  // 死区时间（约1us @ 72MHz）
    TIM_BDTRInitStruct.TIM_Break = TIM_Break_Enable;
    TIM_BDTRInitStruct.TIM_BreakPolarity = TIM_BreakPolarity_Low;
    TIM_BDTRInitStruct.TIM_AutomaticOutput = TIM_AutomaticOutput_Enable;
    TIM_BDTRConfig(TIM1, &TIM_BDTRInitStruct);
  
    // 使能主输出（MOE）- 高级定时器必须！
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
  
    TIM_Cmd(TIM1, ENABLE);
}
```

---

## 八、常见错误与注意事项

### 8.1 典型错误

| 问题             | 原因                   | 解决                       |
| ---------------- | ---------------------- | -------------------------- |
| 定时器不工作     | 忘记使能时钟           | `RCC_APBxPeriphClockCmd` |
| PWM无输出        | GPIO未配置为复用模式   | `GPIO_Mode_AF_PP`        |
| PWM频率不对      | APB1定时器时钟理解错误 | 记住是总线频率的2倍        |
| 占空比跳变异常   | 未使能预装载           | `TIM_ARRPreloadConfig`   |
| 高级定时器无输出 | 未使能MOE              | `TIM_CtrlPWMOutputs`     |
| 编码器计数方向反 | A/B相接反              | 交换接线或软件处理         |

### 8.2 预装载的重要性

```c
// 错误方式：直接修改ARR，可能产生异常波形
TIM2->ARR = new_value;  // 危险！

// 正确方式：使用预装载
TIM_SetAutoreload(TIM2, new_value);  // 写入预装载寄存器
// 下一个更新事件时自动加载到影子寄存器

// 必须先使能预装载功能
TIM_ARRPreloadConfig(TIM2, ENABLE);
```

### 8.3 最佳实践

1. **使能预装载**：`TIM_ARRPreloadConfig()` 和 `TIM_OCxPreloadConfig()`
2. **正确计算时钟**：APB1定时器是总线频率的2倍
3. **高级定时器MOE**：`TIM_CtrlPWMOutputs()` 必须调用
4. **中断标志清除**：ISR中立即清除，防止重复进入
5. **GPIO复用模式**：PWM输出必须用 `GPIO_Mode_AF_PP`

---

## 九、完整工程示例

### 9.1 工程概述

本示例演示多定时器协同工作：

- **TIM2**: 1ms系统节拍（基础定时）
- **TIM3**: 1kHz PWM输出（PA6可调占空比）
- **TIM4**: 输入捕获（测量外部信号频率）

### 9.2 完整 main.c 代码

```c
/**
 * @file    main.c
 * @brief   TIM 定时器综合示例
 * @author  Your Name
 * @version V1.0
 * @date    2026-03-03
 */

#include "stm32f10x.h"
#include <stdio.h>

// 全局变量
volatile uint32_t g_SystemTick = 0;
volatile uint32_t g_PWM_Duty = 500;  // 初始占空比50%

// 函数声明
void RCC_Configuration(void);
void TIM2_Base_Config(void);     // 1ms节拍
void TIM3_PWM_Config(void);      // 1kHz PWM
void TIM4_IC_Config(void);       // 输入捕获
void GPIO_Configuration(void);
void USART_Configuration(void);

int main(void)
{
    RCC_Configuration();
    GPIO_Configuration();
    USART_Configuration();
  
    TIM2_Base_Config();
    TIM3_PWM_Config();
    TIM4_IC_Config();
  
    printf("\r\n========== TIM Demo Started ==========\r\n");
    printf("TIM2: 1ms system tick\r\n");
    printf("TIM3: 1kHz PWM on PA6 (duty adjustable)\r\n");
    printf("TIM4: Input capture for frequency measurement\r\n");
    printf("=======================================\r\n\r\n");
  
    uint32_t last_second = 0;
  
    while (1)
    {
        // 每秒钟执行一次
        if (g_SystemTick / 1000 != last_second)
        {
            last_second = g_SystemTick / 1000;
          
            printf("[Time: %lus] PWM Duty: %lu%%\r\n", 
                   last_second, g_PWM_Duty / 10);
        }
      
        // 模拟动态调整占空比（正弦变化）
        static uint16_t angle = 0;
        angle += 10;
        // 使用简化正弦表或计算
        g_PWM_Duty = 500 + (uint16_t)(400 * sin(angle * 3.14159 / 1800));
        TIM_SetCompare1(TIM3, g_PWM_Duty);
    }
}

// TIM2: 1ms基础定时
void TIM2_Base_Config(void)
{
    TIM_TimeBaseInitTypeDef TIM_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;
  
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
  
    TIM_InitStruct.TIM_Prescaler = 72 - 1;
    TIM_InitStruct.TIM_Period = 1000 - 1;
    TIM_InitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_InitStruct);
    TIM_ARRPreloadConfig(TIM2, ENABLE);
  
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
  
    NVIC_InitStruct.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
  
    TIM_Cmd(TIM2, ENABLE);
}

// TIM3: 1kHz PWM
void TIM3_PWM_Config(void)
{
    TIM_TimeBaseInitTypeDef TIM_InitStruct;
    TIM_OCInitTypeDef TIM_OCInitStruct;
  
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
  
    // PA6: TIM3_CH1
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
  
    TIM_InitStruct.TIM_Prescaler = 72 - 1;
    TIM_InitStruct.TIM_Period = 1000 - 1;
    TIM_InitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_InitStruct);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
  
    TIM_OCInitStruct.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStruct.TIM_Pulse = g_PWM_Duty;
    TIM_OCInitStruct.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM3, &TIM_OCInitStruct);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
  
    TIM_Cmd(TIM3, ENABLE);
}

// 中断处理
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        g_SystemTick++;
    }
}

// RCC、GPIO、USART配置（略，同前例）
```

---

## 十、总结

### 核心要点回顾

```
┌─────────────────────────────────────────────────────┐
│                    TIM 核心概念                      │
├─────────────────────────────────────────────────────┤
│                                                      │
│  1. 定时器分类：                                       │
│     • 高级 (TIM1/8)：带互补输出和死区，用于电机控制    │
│     • 通用 (TIM2~5)：功能最全，最常用                 │
│     • 基本 (TIM6/7)：仅定时，用于DAC触发             │
│                                                      │
│  2. 时钟频率：                                         │
│     • APB1定时器 = 36MHz × 2 = 72MHz                │
│     • APB2定时器 = 72MHz                            │
│                                                      │
│  3. 时基公式：                                         │
│     定时周期 = (PSC+1) × (ARR+1) / 时钟频率           │
│                                                      │
│  4. 工作模式：                                         │
│     • 定时中断：时基+NVIC配置                         │
│     • PWM输出：OC配置+GPIO复用                        │
│     • 输入捕获：IC配置+边沿检测                       │
│     • 编码器：EncoderInterface配置                    │
│                                                      │
│  5. 关键配置：                                         │
│     • 使能预装载（ARRPreload、OCPreload）            │
│     • 高级定时器必须使能MOE                           │
│     • PWM输出GPIO用复用推挽模式                       │
│                                                      │
└─────────────────────────────────────────────────────┘
```

### 记忆口诀

> **"APB1乘二算时钟，PSC分频ARR计，预装载防跳变，高级定时器开MOE"**

---

**参考文档**:

- 《STM32F10xxx 参考手册》第14~17章 定时器
- 《STM32电机控制应用笔记》

**文档版本**: v1.0
**更新日期**: 2026年3月
