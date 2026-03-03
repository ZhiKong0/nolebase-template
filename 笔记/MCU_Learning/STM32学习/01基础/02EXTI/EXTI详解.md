# EXTI 详解（外部中断/事件控制器）

> EXTI（External Interrupt/Event Controller）是 STM32 专门用于检测外部引脚电平变化的外设，支持上升沿、下降沿、双边沿触发，以及软件触发。

---

![EXTI框图](./附件/Pasted%20image%2020260303204145.png)

## 一、EXTI 基本概念

### 1.1 什么是 EXTI？

**EXTI** 全称 **External Interrupt/Event Controller**（外部中断/事件控制器），是 STM32 中负责检测**外部引脚电平变化**的外设。

**核心功能**：
- 检测 GPIO 引脚的上升沿/下降沿/双边沿变化
- 支持软件触发中断/事件
- 产生中断请求给 NVIC
- 产生事件唤醒 MCU（不进入中断服务函数）

### 1.2 EXTI 的核心特性

| 特性            | 说明                                      |
| ------------- | --------------------------------------- |
| **外部线数量**     | 16条外部线（EXTI0~EXTI15），对应 GPIO Pin0~Pin15 |
| **内部线**       | 4条内部线（PVD、RTC、USB、以太网唤醒）                |
| **触发方式**      | 上升沿、下降沿、双边沿、软件触发                        |
| **输出模式**      | 中断模式（执行ISR）/ 事件模式（唤醒MCU）                |
| **与 NVIC 关系** | EXTI 产生请求 → NVIC 响应执行                   |
| **位置**        | **芯片外设**，位于 GPIO 和 NVIC 之间              |

> **注意**：EXTI 是芯片外设，不是 NVIC 的一部分。EXTI 检测到变化后向 NVIC 发出中断请求。

---

## 二、EXTI 与 GPIO 的映射关系

### 2.1 16条外部线对应 GPIO

| EXTI 线 | 对应 GPIO                  | 说明                     |
| :----: | :----------------------- | :--------------------- |
| EXTI0  | PA0 / PB0 / ... / PG0    | 所有端口的 Pin0 映射到 EXTI0   |
| EXTI1  | PA1 / PB1 / ... / PG1    | 所有端口的 Pin1 映射到 EXTI1   |
|  ...   | ...                      | ...                    |
| EXTI15 | PA15 / PB15 / ... / PG15 | 所有端口的 Pin15 映射到 EXTI15 |


### 2.2 映射原理

```
                    GPIO 引脚
                       │
       ┌───────────────┼───────────────┐
       │               │               │
       ▼               ▼               ▼
    ┌─────┐         ┌─────┐         ┌─────┐
    │ PA0 │         │ PB0 │   ...   │ PG0 │
    └──┬──┘         └──┬──┘         └──┬──┘
       │               │               │
       └───────────────┼───────────────┘
                       │
                       ▼
                 ┌─────────┐
                 │  AFIO   │  ← 复用功能重映射控制器
                 │ (选择器) │    选择哪个端口映射到 EXTI
                 └────┬────┘
                      │
                      ▼
                 ┌─────────┐
                 │  EXTI0  │  ← EXTI 线 0
                 └─────────┘
                      │
                      ▼
                 ┌─────────┐
                 │  NVIC   │  ← 产生 EXTI0_IRQn 中断
                 └─────────┘
```

**关键**：同一时刻，**只能有一个端口**的 PinX 映射到 EXTIx。
- 例如：如果 PA0 映射到 EXTI0，则 PB0~PG0 不能再映射到 EXTI0

---

## 三、EXTI 触发方式详解

### 3.1 三种边沿触发方式

| 触发方式 | 说明 | 应用场景 |
|:---------|:-----|:---------|
| **上升沿触发** | 信号从低电平跳变到高电平时触发 | 检测按键抬起 |
| **下降沿触发** | 信号从高电平跳变到低电平时触发 | 检测按键按下 |
| **双边沿触发** | 上升沿和下降沿都触发 | 检测电平任何变化 |

### 3.2 软件触发

```c
// 软件触发 EXTI0（不依赖外部引脚变化）
EXTI_GenerateSWInterrupt(EXTI_Line0);

// 应用场景：
// 1. 模拟外部中断进行测试
// 2. 在代码中主动触发中断处理
// 3. 实现复杂的软件事件机制
```

### 3.3 触发方式选择策略

```
触发方式选择决策树：

检测按键？
├── 是 → 按键是否带硬件消抖？
│         ├── 是 → 下降沿触发（检测按下）
│         └── 否 → 双边沿触发 + 软件消抖
└── 否 → 检测传感器信号？
          ├── 是 → 根据传感器输出特性选择
          └── 否 → 双边沿触发（捕获所有变化）
```

---

## 四、EXTI 与 NVIC 的协作

### 4.1 中断请求映射表

| EXTI 线 | NVIC 中断通道 | 向量号 | 说明 |
|:-------:|:-------------:|:------:|:-----|
| EXTI0 | EXTI0_IRQn | 6 | 独立中断通道 |
| EXTI1 | EXTI1_IRQn | 7 | 独立中断通道 |
| EXTI2 | EXTI2_IRQn | 8 | 独立中断通道 |
| EXTI3 | EXTI3_IRQn | 9 | 独立中断通道 |
| EXTI4 | EXTI4_IRQn | 10 | 独立中断通道 |
| EXTI5~9 | EXTI9_5_IRQn | 23 | 共用中断通道 |
| EXTI10~15 | EXTI15_10_IRQn | 40 | 共用中断通道 |

### 4.2 共用中断通道的处理

```c
// EXTI9_5_IRQHandler：处理 EXTI5~9 的共用中断
void EXTI9_5_IRQHandler(void)
{
    // 必须判断是哪个 EXTI 线触发的中断
    if (EXTI_GetITStatus(EXTI_Line5) != RESET)
    {
        // EXTI5 的中断处理
        EXTI_ClearITPendingBit(EXTI_Line5);
    }
    else if (EXTI_GetITStatus(EXTI_Line6) != RESET)
    {
        // EXTI6 的中断处理
        EXTI_ClearITPendingBit(EXTI_Line6);
    }
    // ... 以此类推处理 EXTI7/8/9
}
```

---

## 五、EXTI 配置和使用

### 5.1 EXTI 初始化结构体

```c
typedef struct
{
    uint32_t EXTI_Line;               // EXTI 线选择（EXTI_Line0 ~ EXTI_Line15）
    EXTIMode_TypeDef EXTI_Mode;       // 模式：中断(Interrupt)或事件(Event)
    EXTITrigger_TypeDef EXTI_Trigger; // 触发方式：上升沿/下降沿/双边沿
    FunctionalState EXTI_LineCmd;     // 使能/禁用
} EXTI_InitTypeDef;
```

### 5.2 完整配置流程

```c
#include "stm32f10x.h"

/**
 * @brief  EXTI 初始化配置示例（PA0 按键中断）
 * @note   配置 PA0 为下降沿触发的外部中断
 */
void EXTI0_Configuration(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    EXTI_InitTypeDef EXTI_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;
    
    // ==================== 第1步：使能外设时钟 ====================
    // 使能 GPIOA 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    // ↑ AFIO 时钟必须使能！否则 EXTI 映射失败
    
    // ==================== 第2步：配置 GPIO ====================
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;  // 上拉输入（按键通常一端接地）
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // ==================== 第3步：映射 GPIO 到 EXTI ====================
    // 将 PA0 映射到 EXTI0
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    
    // ==================== 第4步：配置 EXTI ====================
    EXTI_InitStruct.EXTI_Line = EXTI_Line0;           // 选择 EXTI0
    EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;  // 中断模式
    EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Falling; // 下降沿触发（按键按下）
    EXTI_InitStruct.EXTI_LineCmd = ENABLE;             // 使能
    EXTI_Init(&EXTI_InitStruct);
    
    // ==================== 第5步：配置 NVIC ====================
    NVIC_InitStruct.NVIC_IRQChannel = EXTI0_IRQn;     // EXTI0 中断通道
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
}
```

### 5.3 常用 EXTI 函数

| 函数 | 功能 |
|------|------|
| `GPIO_EXTILineConfig(port, pin)` | 映射 GPIO 端口到 EXTI 线 |
| `EXTI_Init(&initStruct)` | 初始化 EXTI |
| `EXTI_GetITStatus(line)` | 检查 EXTI 线中断标志 |
| `EXTI_ClearITPendingBit(line)` | 清除 EXTI 线中断标志 |
| `EXTI_GenerateSWInterrupt(line)` | 软件触发 EXTI 中断 |

---

## 六、中断服务函数（ISR）编写

### 6.1 基本 ISR 模板

```c
/**
 * @brief  EXTI0 中断服务函数
 * @note   PA0 按键中断处理
 */
void EXTI0_IRQHandler(void)
{
    // 第1步：检查中断标志（重要！）
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        // 第2步：立即清除中断标志（防止重复进入）
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 第3步：用户中断处理代码
        // 按键消抖（简单延时方式，实际项目建议用定时器）
        Delay_ms(20);
        
        // 确认按键确实按下
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET)
        {
            // 按键处理逻辑
            Button_Pressed_Handler();
        }
    }
}
```

### 6.2 共用中断通道 ISR 模板

```c
/**
 * @brief  EXTI9_5 中断服务函数（处理 EXTI5~9）
 */
void EXTI9_5_IRQHandler(void)
{
    // 依次检查每个 EXTI 线的中断标志
    
    // EXTI5
    if (EXTI_GetITStatus(EXTI_Line5) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line5);
        EXTI5_Handler();
    }
    
    // EXTI6
    if (EXTI_GetITStatus(EXTI_Line6) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line6);
        EXTI6_Handler();
    }
    
    // EXTI7
    if (EXTI_GetITStatus(EXTI_Line7) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line7);
        EXTI7_Handler();
    }
    
    // EXTI8
    if (EXTI_GetITStatus(EXTI_Line8) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line8);
        EXTI8_Handler();
    }
    
    // EXTI9
    if (EXTI_GetITStatus(EXTI_Line9) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line9);
        EXTI9_Handler();
    }
}
```

---

## 七、按键消抖处理

### 7.1 硬件消抖 vs 软件消抖

| 方式 | 优点 | 缺点 | 适用场景 |
|:-----|:-----|:-----|:---------|
| **硬件消抖** | 不占用CPU | 需要额外RC电路 | 对响应要求不高的场景 |
| **软件消抖** | 无需额外硬件 | 占用CPU时间 | 大多数应用场景 |
| **定时器消抖** | 精确、不阻塞 | 需要占用定时器 | 多按键、高精度场景 |

### 7.2 软件消抖实现

```c
// 简单延时消抖（阻塞方式，不推荐在复杂项目中使用）
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 延时消抖
        Delay_ms(20);
        
        // 确认按键状态
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET)
        {
            // 真正的按键按下
            Key_Action();
        }
    }
}
```

### 7.3 定时器消抖实现（推荐）

```c
volatile uint8_t g_KeyPressed = 0;
volatile uint32_t g_KeyDebounceCnt = 0;

// EXTI 中断：只标记按键事件，不处理
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line0);
        g_KeyPressed = 1;  // 标记按键事件发生
        g_KeyDebounceCnt = 0;  // 重置消抖计数
    }
}

// TIM 定时器中断（每1ms执行一次）：执行消抖和按键处理
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        
        // 消抖处理
        if (g_KeyPressed)
        {
            g_KeyDebounceCnt++;
            
            if (g_KeyDebounceCnt >= 20)  // 20ms 消抖时间
            {
                g_KeyPressed = 0;
                
                // 确认按键确实按下
                if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET)
                {
                    Key_Action();  // 执行按键动作
                }
            }
        }
    }
}
```

---

## 八、事件模式 vs 中断模式

### 8.1 两种模式对比

| 特性 | 中断模式 | 事件模式 |
|:-----|:---------|:---------|
| **触发后行为** | 执行 ISR | 唤醒 MCU（不执行代码） |
| **CPU 参与** | 需要 | 不需要 |
| **延迟** | 12个时钟（中断响应） | 更短 |
| **应用场景** | 需要处理逻辑 | 仅从低功耗模式唤醒 |
| **功耗** | 相对较高 | 极低 |

### 8.2 事件模式配置

```c
/**
 * @brief  EXTI 事件模式配置（用于从 STOP 模式唤醒）
 */
void EXTI0_Event_Config(void)
{
    EXTI_InitTypeDef EXTI_InitStruct;
    
    // 配置为事件模式（不是中断模式！）
    EXTI_InitStruct.EXTI_Line = EXTI_Line0;
    EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Event;      // ← 事件模式
    EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStruct.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStruct);
    
    // 注意：事件模式不需要配置 NVIC！
}

// 进入低功耗模式
void Enter_Stop_Mode(void)
{
    // 配置 EXTI0 事件唤醒
    EXTI0_Event_Config();
    
    // 进入 STOP 模式
    PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);
    
    // 被 EXTI0 事件唤醒后从这里继续执行
    // 此时 CPU 被唤醒，但不执行任何 ISR
    
    // 恢复系统时钟（STOP模式会关闭PLL）
    SystemClock_Restore();
}
```

---

## 九、常见错误与注意事项

### 9.1 典型错误

| 问题 | 原因 | 解决 |
|------|------|------|
| 外部中断不触发 | 忘记使能 AFIO 时钟 | `RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE)` |
| 中断连续触发 | 未清除中断标志 | ISR 中必须调用 `EXTI_ClearITPendingBit()` |
| 多个按键冲突 | EXTI5~9 共用中断，未判断具体线路 | ISR 中检查 `EXTI_GetITStatus()` |
| 按键抖动导致多次触发 | 未做消抖处理 | 添加 10~20ms 消抖延时 |
| 中断无法嵌套 | NVIC 优先级配置错误 | 检查抢占优先级设置 |

### 9.2 最佳实践

1. **必须使能 AFIO 时钟**：GPIO 映射到 EXTI 需要 AFIO
2. **立即清除中断标志**：在 ISR 最开始就清除，防止重复进入
3. **做好消抖处理**：机械按键必须消抖，建议用定时器方式
4. **共用中断要判断线路**：EXTI9_5 和 EXTI15_10 是多路共用
5. **中断代码要简短**：复杂处理放到主循环，ISR 只标记事件

---

## 十、完整工程示例

### 10.1 工程概述

本示例演示：
- **PA0**：按键输入（EXTI0，下降沿触发，最高优先级）
- **PA1**：按键输入（EXTI1，下降沿触发）
- **PB5**：外部信号（EXTI5，上升沿触发，共用中断）

### 10.2 完整 main.c 代码

```c
/**
 * @file    main.c
 * @brief   EXTI 外部中断完整示例
 * @author  Your Name
 * @version V1.0
 * @date    2026-03-03
 */

#include "stm32f10x.h"
#include <stdio.h>

// 全局变量
volatile uint32_t g_Key0_PressCnt = 0;
volatile uint32_t g_Key1_PressCnt = 0;
volatile uint32_t g_EXTI5_Cnt = 0;

// 函数声明
void RCC_Configuration(void);
void GPIO_Configuration(void);
void NVIC_Configuration(void);
void EXTI_Configuration(void);
void Delay_ms(uint32_t ms);

int main(void)
{
    // 初始化
    RCC_Configuration();
    GPIO_Configuration();
    NVIC_Configuration();
    EXTI_Configuration();
    
    printf("\r\n========== EXTI Demo Started ==========\r\n");
    printf("PA0: Key0 (EXTI0, Independent)\r\n");
    printf("PA1: Key1 (EXTI1, Independent)\r\n");
    printf("PB5: External Signal (EXTI5, Shared EXTI9_5)\r\n");
    printf("=======================================\r\n\r\n");
    
    while (1)
    {
        // 每1秒打印一次统计
        printf("Key0=%lu, Key1=%lu, EXTI5=%lu\r\n", 
               g_Key0_PressCnt, g_Key1_PressCnt, g_EXTI5_Cnt);
        Delay_ms(1000);
    }
}

void RCC_Configuration(void)
{
    // 系统时钟 72MHz
    RCC_HSEConfig(RCC_HSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET);
    
    FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
    FLASH_SetLatency(FLASH_Latency_2);
    
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div2);
    RCC_PCLK2Config(RCC_HCLK_Div1);
    
    RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
    
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08);
}

void GPIO_Configuration(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    
    // 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | 
                           RCC_APB2Periph_GPIOB | 
                           RCC_APB2Periph_AFIO | 
                           RCC_APB2Periph_USART1, ENABLE);
    
    // PA0: Key0 输入（上拉）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PA1: Key1 输入（上拉）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_1;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PB5: 外部信号输入（浮空）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // PA9: USART1 TX
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void NVIC_Configuration(void)
{
    NVIC_InitTypeDef NVIC_InitStruct;
    
    // 优先级分组：2位抢占 + 2位响应
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    
    // EXTI0 (Key0) - 最高优先级
    NVIC_InitStruct.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
    
    // EXTI1 (Key1)
    NVIC_InitStruct.NVIC_IRQChannel = EXTI1_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&NVIC_InitStruct);
    
    // EXTI9_5 (Shared - EXTI5)
    NVIC_InitStruct.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&NVIC_InitStruct);
}

void EXTI_Configuration(void)
{
    EXTI_InitTypeDef EXTI_InitStruct;
    
    // ==================== PA0 -> EXTI0 ====================
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    
    EXTI_InitStruct.EXTI_Line = EXTI_Line0;
    EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Falling;  // 下降沿（按键按下）
    EXTI_InitStruct.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStruct);
    
    // ==================== PA1 -> EXTI1 ====================
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);
    
    EXTI_InitStruct.EXTI_Line = EXTI_Line1;
    EXTI_Init(&EXTI_InitStruct);
    
    // ==================== PB5 -> EXTI5 ====================
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource5);
    
    EXTI_InitStruct.EXTI_Line = EXTI_Line5;
    EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Rising;   // 上升沿触发
    EXTI_Init(&EXTI_InitStruct);
}

// ============================================================================
// 中断服务函数
// ============================================================================

void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        // 立即清除标志
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 简单消抖（实际项目建议用定时器）
        Delay_ms(20);
        
        // 确认按键确实按下
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET)
        {
            g_Key0_PressCnt++;
            printf("[EXTI0] Key0 pressed! Count=%lu\r\n", g_Key0_PressCnt);
        }
    }
}

void EXTI1_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line1) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line1);
        
        Delay_ms(20);
        
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1) == Bit_RESET)
        {
            g_Key1_PressCnt++;
            printf("[EXTI1] Key1 pressed! Count=%lu\r\n", g_Key1_PressCnt);
        }
    }
}

void EXTI9_5_IRQHandler(void)
{
    // 检查 EXTI5
    if (EXTI_GetITStatus(EXTI_Line5) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line5);
        
        g_EXTI5_Cnt++;
        printf("[EXTI5] External signal! Count=%lu\r\n", g_EXTI5_Cnt);
    }
    
    // 如有需要，检查 EXTI6/7/8/9
    // if (EXTI_GetITStatus(EXTI_Line6) != RESET) { ... }
}

// ============================================================================
// 延时函数和 printf 重定向
// ============================================================================

void Delay_ms(uint32_t ms)
{
    volatile uint32_t nCount;
    RCC_ClocksTypeDef RCC_Clocks;
    
    RCC_GetClocksFreq(&RCC_Clocks);
    nCount = (RCC_Clocks.HCLK_Frequency / 10000) * ms;
    
    for (; nCount != 0; nCount--);
}

#ifdef __GNUC__
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

PUTCHAR_PROTOTYPE
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}
```

### 10.3 代码要点

1. **AFIO 时钟必须使能**：`RCC_APB2Periph_AFIO`
2. **GPIO 映射**：`GPIO_EXTILineConfig()` 在 EXTI 初始化之前调用
3. **共用中断判断**：`EXTI9_5_IRQHandler()` 中使用 `EXTI_GetITStatus()` 判断具体线路
4. **消抖处理**：简单延时消抖，实际项目建议用定时器方式

---

## 十一、总结

### 核心要点回顾

```
┌─────────────────────────────────────────────────────┐
│                    EXTI 核心概念                      │
├─────────────────────────────────────────────────────┤
│                                                      │
│  1. EXTI 是芯片外设，不是 NVIC 的一部分              │
│     EXTI 检测变化 → 产生请求 → NVIC 响应执行         │
│                                                      │
│  2. 16条外部线 EXTI0~15，对应 GPIO Pin0~15           │
│     同一时刻只能有一个端口的 PinX 映射到 EXTIx       │
│                                                      │
│  3. 触发方式：上升沿/下降沿/双边沿/软件触发          │
│                                                      │
│  4. 中断通道映射：                                   │
│     EXTI0~4：独立通道                                │
│     EXTI5~9：共用 EXTI9_5_IRQn                        │
│     EXTI10~15：共用 EXTI15_10_IRQn                    │
│                                                      │
│  5. 配置流程：                                       │
│     使能时钟(GPIO+AFIO) → 配置GPIO → 映射EXTI        │
│     → 配置EXTI → 配置NVIC                            │
│                                                      │
│  6. 关键注意：                                       │
│     • 必须使能 AFIO 时钟                            │
│     • ISR 中立即清除中断标志                        │
│     • 机械按键必须消抖                               │
│     • 共用中断要判断具体线路                        │
│                                                      │
└─────────────────────────────────────────────────────┘
```

### 记忆口诀

> **"EXTI 检测，NVIC 响应，边沿触发要消抖，共用中断要判断"**

---

**参考文档**:
- 《STM32F10xxx 参考手册》第9章 中断和事件
- 《STM32F10xxx 参考手册》第10章 EXTI

**文档版本**: v1.0  
**更新日期**: 2026年3月
