# NVIC 详解（嵌套向量中断控制器）

> NVIC（Nested Vectored Interrupt Controller）是 Cortex-M 内核的核心组件，负责管理所有中断的优先级、使能/禁用和响应。

---
![Pasted image 20260303203827](https://raw.githubusercontent.com/ZhiKong0/Image_Auto/main/Obsidian/Pasted%20image%2020260303203827.png)

## 一、NVIC 基本概念

### 1.1 什么是 NVIC？

**NVIC** 是 ARM Cortex-M 系列处理器内置的**中断控制器**，全称 **Nested Vectored Interrupt Controller**（嵌套向量中断控制器）。

**核心功能**：
- 接收并管理所有外设和内核的中断请求
- 根据优先级裁决哪个中断优先执行
- 支持中断嵌套（高优先级可打断低优先级）
- 快速响应中断（硬件自动保存/恢复现场）

### 1.2 NVIC 的核心特性

| 特性        | 说明                           |
| --------- | ---------------------------- |
| **中断数量**  | 最多支持 240 个外部中断 + 16 个内部异常    |
| **优先级位数** | 8 位（实际使用高 4 位，STM32F1 用 4 位） |
| **优先级等级** | 16 级可编程优先级（0~15，数字越小优先级越高）   |
| **优先级分组** | 支持抢占优先级和子优先级分组               |
| **尾链机制**  | 两个中断背靠背时，省去现场恢复/保存时间         |
| **位置**    | **内核外设**，位于 Cortex-M 处理器内部   |

> **注意**：NVIC 是 ARM 设计的内核外设，所有 Cortex-M 芯片都有，与厂商无关。

---

## 二、中断优先级详解

### 2.1 两种优先级类型

NVIC 使用**两组优先级**来管理中断：

| 类型 | 英文 | 功能 | 类比 |
|------|------|------|------|
| **抢占优先级** | Preemption Priority | 可中断嵌套，高优先级可**打断**正在执行的低优先级中断 | 急诊病人直接冲进诊室 |
| **响应优先级** | Sub Priority / Response Priority | 仅影响排队顺序，**不能打断**正在执行的中断 | 普通病人插队提前看病 |

**规则总结**：
1. **抢占优先级高的**可以**打断**（抢占）正在执行的低优先级中断
2. **响应优先级高的**只能**优先排队**，不能打断正在执行的中断
3. 抢占优先级和响应优先级都相同时，按**中断号**（向量表序号）排队

### 2.2 优先级数值规则

- 数值越小，优先级越高
- `0` 是最高优先级
- `15` 是最低优先级（当使用 4 位时）

```
优先级数值: 0 > 1 > 2 > ... > 14 > 15
            ↑                  ↑
         最高优先级         最低优先级
```

---

## 三、优先级分组（Priority Grouping）

### 3.1 分组原理

NVIC 使用 **4 位** 来表示优先级（STM32F1），这 4 位可以**切分**为两部分：
- **抢占优先级位**（Preemption Priority Bits）
- **响应优先级位**（Sub Priority Bits）

| 分组 | 抢占优先级位数 | 响应优先级位数 | 抢占优先级范围 | 响应优先级范围 |
|:----:|:-------------:|:-------------:|:-------------:|:-------------:|
| 组0 | 0位 | 4位 | 0（唯一） | 0~15 |
| 组1 | 1位 | 3位 | 0~1 | 0~7 |
| 组2 | 2位 | 2位 | 0~3 | 0~3 |
| 组3 | 3位 | 1位 | 0~7 | 0~1 |
| 组4 | 4位 | 0位 | 0~15 | 0（唯一） |

### 3.2 分组选择策略

| 场景 | 推荐分组 | 原因 |
|------|---------|------|
| 需要大量嵌套中断 | 组4 | 抢占优先级最多（16级），无响应优先级 |
| 需要精细抢占控制 | 组2 | 抢占和响应各4级，比较均衡 |
| 简单项目 | 组2 | 最常见选择，兼容性好 |

### 3.3 分组代码配置

```c
// 标准库配置
#include "misc.h"

// 选择优先级分组2：2位抢占优先级 + 2位响应优先级
NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
```

**分组宏定义**（在 `misc.h` 中）：
```c
#define NVIC_PriorityGroup_0    ((uint32_t)0x700)  // 0位抢占，4位响应
#define NVIC_PriorityGroup_1    ((uint32_t)0x600)  // 1位抢占，3位响应
#define NVIC_PriorityGroup_2    ((uint32_t)0x500)  // 2位抢占，2位响应
#define NVIC_PriorityGroup_3    ((uint32_t)0x400)  // 3位抢占，1位响应
#define NVIC_PriorityGroup_4    ((uint32_t)0x300)  // 4位抢占，0位响应
```

---

## 四、NVIC 中断向量表

### 4.1 向量表结构

STM32F1 的向量表包含 **68 个可屏蔽中断通道**（不同型号可能有差异）：

```
向量号     中断名称           说明
─────────────────────────────────────────
0          WWDG              窗口看门狗中断
1          PVD               PVD通过EXTI线检测中断
2          TAMPER            入侵检测中断
3          RTC               RTC全局中断
4          FLASH             FLASH全局中断
5          RCC               RCC全局中断
6          EXTI0             EXTI线0中断
7          EXTI1             EXTI线1中断
8          EXTI2             EXTI线2中断
9          EXTI3             EXTI线3中断
10         EXTI4             EXTI线4中断
11         DMA1_Channel1     DMA1通道1全局中断
...        ...               ...
37         TIM2              TIM2全局中断
38         TIM3              TIM3全局中断
39         TIM4              TIM4全局中断
...        ...               ...
59         TIM5              TIM5全局中断（大容量）
...        ...               ...
67         DMA2_Channel4_5   DMA2通道4和5全局中断（大容量）
```

### 4.2 向量表特点

| 特点 | 说明 |
|------|------|
| **固定地址** | 从 `0x0800_0000`（Flash）或 `0x2000_0000`（RAM）开始 |
| **入口大小** | 每个中断向量占 4 字节（存储中断服务函数地址） |
| **自动跳转** | 硬件根据中断号自动计算跳转地址 |
| **C语言处理** | 编译器自动生成跳转表，无需手动配置 |

---

## 五、NVIC 配置和使用

### 5.1 NVIC 初始化结构体

```c
typedef struct
{
    uint8_t NVIC_IRQChannel;                    // 中断通道（如 EXTI0_IRQn）
    uint8_t NVIC_IRQChannelPreemptionPriority;  // 抢占优先级（0~15）
    uint8_t NVIC_IRQChannelSubPriority;         // 响应优先级（0~15）
    FunctionalState NVIC_IRQChannelCmd;         // 使能/禁用（ENABLE/DISABLE）
} NVIC_InitTypeDef;
```

### 5.2 常用中断通道（IRQn）

| 中断通道名 | 说明 | 向量号 |
|-----------|------|--------|
| `WWDG_IRQn` | 窗口看门狗中断 | 0 |
| `PVD_IRQn` | PVD中断 | 1 |
| `RTC_IRQn` | RTC中断 | 3 |
| `EXTI0_IRQn` | EXTI线0中断 | 6 |
| `EXTI1_IRQn` | EXTI线1中断 | 7 |
| `EXTI2_IRQn` | EXTI线2中断 | 8 |
| `EXTI3_IRQn` | EXTI线3中断 | 9 |
| `EXTI4_IRQn` | EXTI线4中断 | 10 |
| `DMA1_Channel1_IRQn` | DMA1通道1中断 | 11 |
| `ADC1_2_IRQn` | ADC1和ADC2中断 | 18 |
| `TIM1_UP_IRQn` | TIM1更新中断 | 25 |
| `TIM2_IRQn` | TIM2全局中断 | 28 |
| `TIM3_IRQn` | TIM3全局中断 | 29 |
| `TIM4_IRQn` | TIM4全局中断 | 30 |
| `I2C1_EV_IRQn` | I2C1事件中断 | 31 |
| `I2C1_ER_IRQn` | I2C1错误中断 | 32 |
| `SPI1_IRQn` | SPI1全局中断 | 35 |
| `USART1_IRQn` | USART1全局中断 | 37 |
| `USART2_IRQn` | USART2全局中断 | 38 |
| `EXTI15_10_IRQn` | EXTI线[15:10]中断 | 40 |
| `TIM5_IRQn` | TIM5全局中断 | 50 |

### 5.3 完整配置示例

```c
#include "stm32f10x.h"

void NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStruct;
    
    // 1. 配置优先级分组（2位抢占 + 2位响应）
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    
    // 2. 配置 TIM2 中断
    NVIC_InitStruct.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;  // 抢占优先级1
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;         // 响应优先级0
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
    
    // 3. 配置 TIM3 中断（优先级比TIM2低）
    NVIC_InitStruct.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 2;  // 抢占优先级2（比TIM2低）
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
    
    // 4. 配置 USART1 中断（最高优先级）
    NVIC_InitStruct.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0;  // 抢占优先级0（最高）
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;         // 响应优先级1
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
}
```

---

## 六、中断嵌套示例

### 6.1 嵌套场景分析

假设优先级分组为 **Group 2**（2位抢占 + 2位响应）：

| 中断 | 抢占优先级 | 响应优先级 | 实际优先级 |
|------|:---------:|:---------:|:---------:|
| USART1 中断 | 0 | 1 | **最高** |
| TIM2 中断 | 1 | 0 | 中等 |
| TIM3 中断 | 2 | 0 | 较低 |
| EXTI0 中断 | 1 | 1 | 与TIM2同级 |

**场景分析**：

```
时间轴 →

主程序运行 ─────────────────────────────────────────────
              ↓ TIM2中断请求
TIM2 ISR     ┌─────────────┐
（优先级1）   │ 正在执行...  │ ←────────┐
             └──────┬──────┘          │ USART1抢占
                    ↓ USART1中断请求   │
USART1 ISR  ┌───────┴────────────────┐│
（优先级0）  │ 高优先级抢占，打断TIM2  ││
            └─────────────────────────┘│
                                       │ TIM2恢复
TIM2 ISR 续 ───────────────────────────┘
             ↓ TIM3中断请求（不抢占）
TIM3 ISR    ┌────────────────────────┐
（优先级2）  │ 等待TIM2执行完毕后执行  │
            └────────────────────────┘
                    ↓ EXTI0中断请求（不抢占）
EXTI0 ISR   ┌────────────────────────┐
（优先级1）  │ 与TIM2同级，按中断号排队 │
            └────────────────────────┘
```

### 6.2 抢占 vs 响应的区别

**抢占场景**（高抢占优先级打断低抢占优先级）：
```c
// USART1抢占优先级=0，TIM2抢占优先级=1
// USART1可以打断TIM2
```

**响应场景**（相同抢占优先级时，响应优先级决定排队顺序）：
```c
// TIM2: 抢占=1, 响应=0
// EXTI0: 抢占=1, 响应=1
// 两者同时发生时，TIM2先执行（响应优先级更高）
// 但TIM2执行时EXTI0不能打断它（抢占优先级相同）
```

---

## 七、NVIC 底层寄存器（了解）

### 7.1 关键寄存器

| 寄存器 | 地址偏移 | 功能 |
|--------|---------|------|
| `NVIC_ISER[0~7]` | 0x000~0x01C | 中断使能寄存器（写1使能） |
| `NVIC_ICER[0~7]` | 0x080~0x09C | 中断禁用寄存器（写1禁用） |
| `NVIC_ISPR[0~7]` | 0x100~0x11C | 中断挂起设置寄存器 |
| `NVIC_ICPR[0~7]` | 0x180~0x19C | 中断挂起清除寄存器 |
| `NVIC_IABR[0~7]` | 0x200~0x21C | 中断活动状态寄存器（只读） |
| `NVIC_IP[0~59]` | 0x300~0x3EC | 中断优先级寄存器（每个中断8位） |

### 7.2 寄存器操作示例（直接寄存器版）

```c
// 直接操作寄存器使能TIM2中断（非标准库方式）
// 1. 使能TIM2中断（NVIC_ISER[0]的第28位）
NVIC->ISER[0] |= (1 << 28);  // TIM2_IRQn = 28

// 2. 设置优先级（NVIC_IP[28]，使用高4位）
NVIC->IP[28] = 0x10;  // 抢占优先级=1, 响应优先级=0 (Group2)
```

> **注意**：实际开发中建议使用标准库或HAL库，不要直接操作寄存器。

---

## 八、常见问题与注意事项

### 8.1 优先级配置陷阱

| 问题 | 原因 | 解决 |
|------|------|------|
| 中断不触发 | 优先级分组未配置或 NVIC 未使能 | 检查 `NVIC_PriorityGroupConfig` 和 `NVIC_Init` |
| 高优先级被低优先级打断 | 抢占优先级设置反了 | 数值越小优先级越高 |
| 中断偶尔丢失 | 相同抢占优先级的中断嵌套导致 | 确保关键中断有更高的抢占优先级 |

### 8.2 最佳实践

1. **尽早配置优先级分组**：在 `main()` 开始时就配置
2. **统一优先级策略**：项目中使用相同的分组方式
3. **预留优先级空间**：不要把0~15全部用完，留一些给后期扩展
4. **关键外设优先**：串口、紧急停止等设高抢占优先级
5. **避免同级抢占**：相同抢占优先级的中断不能嵌套

---

## 十、完整工程示例

### 10.1 工程概述

本示例演示一个多中断源项目，包含：
- **TIM2**: 1ms 定时中断（低优先级，用于系统节拍）
- **TIM3**: 10ms 定时中断（中优先级，用于数据处理）
- **USART1**: 串口接收中断（最高优先级，用于紧急命令接收）
- **EXTI0**: 外部按键中断（中优先级，用于用户输入）

**优先级分组**: Group 2（2位抢占优先级 + 2位响应优先级）

| 中断源 | 抢占优先级 | 响应优先级 | 说明 |
|--------|:---------:|:---------:|:----:|
| USART1 | 0 | 1 | 最高抢占优先级，可打断其他所有中断 |
| TIM3 | 1 | 0 | 中等抢占优先级 |
| EXTI0 | 1 | 1 | 与TIM3同级，但响应优先级低 |
| TIM2 | 2 | 0 | 最低抢占优先级，可被其他中断打断 |

### 10.2 完整 main.c 代码

```c
/**
 * @file    main.c
 * @brief   NVIC 多中断源工程示例
 * @author  Your Name
 * @version V1.0
 * @date    2026-03-02
 * 
 * 本示例演示 NVIC 的优先级分组、多中断源配置和中断嵌套
 */

#include "stm32f10x.h"

// 全局变量定义
volatile uint32_t g_SystemTick = 0;         // 系统节拍计数
volatile uint32_t g_Tim3Count = 0;          // TIM3 中断计数
volatile uint32_t g_Exti0Count = 0;         // EXTI0 中断计数
volatile uint8_t  g_RxBuffer[64];           // 串口接收缓冲区
volatile uint8_t  g_RxIndex = 0;            // 接收索引
volatile uint8_t  g_RxComplete = 0;         // 接收完成标志

// 函数声明
void RCC_Configuration(void);
void GPIO_Configuration(void);
void NVIC_Configuration(void);
void TIM2_Configuration(void);
void TIM3_Configuration(void);
void USART1_Configuration(void);
void EXTI0_Configuration(void);

/**
 * @brief  主函数
 * @param  无
 * @retval 无
 */
int main(void)
{
    // 1. 系统时钟配置（72MHz）
    RCC_Configuration();
    
    // 2. 配置 NVIC（必须在所有中断初始化之前）
    NVIC_Configuration();
    
    // 3. GPIO 初始化
    GPIO_Configuration();
    
    // 4. 外设初始化
    TIM2_Configuration();      // 1ms 系统节拍
    TIM3_Configuration();      // 10ms 数据处理
    USART1_Configuration();    // 串口通信
    EXTI0_Configuration();     // 外部按键
    
    // 5. 启动 TIM2
    TIM_Cmd(TIM2, ENABLE);
    
    printf("System Started!\r\n");
    printf("Priority Group: 2 (2-bit Preempt + 2-bit Sub)\r\n");
    printf("USART1: Preempt=0, Sub=1 (Highest)\r\n");
    printf("TIM3:   Preempt=1, Sub=0 (Medium)\r\n");
    printf("EXTI0:  Preempt=1, Sub=1 (Medium)\r\n");
    printf("TIM2:   Preempt=2, Sub=0 (Lowest)\r\n");
    printf("-------------------------------------------\r\n");
    
    // 6. 主循环
    while (1)
    {
        // 每 1000ms 打印一次统计信息
        if (g_SystemTick >= 1000)
        {
            g_SystemTick = 0;
            
            printf("[Main Loop] Tick=%lu, Tim3Count=%lu, Exti0Count=%lu\r\n",
                   g_SystemTick, g_Tim3Count, g_Exti0Count);
        }
        
        // 处理串口接收到的数据
        if (g_RxComplete)
        {
            g_RxComplete = 0;
            printf("[USART1 RX] Received: %s\r\n", g_RxBuffer);
            g_RxIndex = 0;
            memset((void*)g_RxBuffer, 0, sizeof(g_RxBuffer));
        }
    }
}

/**
 * @brief  RCC 时钟配置
 * @note   系统时钟 72MHz，APB1=36MHz，APB2=72MHz
 */
void RCC_Configuration(void)
{
    // 启用 HSE（外部高速晶振 8MHz）
    RCC_HSEConfig(RCC_HSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET);
    
    // 配置 PLL：8MHz * 9 = 72MHz
    RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
    
    // 切换系统时钟到 PLL
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08);
    
    // 配置 AHB=72MHz，APB1=36MHz，APB2=72MHz
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div2);
    RCC_PCLK2Config(RCC_HCLK_Div1);
    
    // 启用外设时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | 
                           RCC_APB2Periph_USART1 | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);
}

/**
 * @brief  NVIC 配置（核心部分）
 * @note   配置优先级分组和各中断优先级
 */
void NVIC_Configuration(void)
{
    NVIC_InitTypeDef NVIC_InitStruct;
    
    // ==================== 关键：配置优先级分组 ====================
    // 选择 Group 2：2位抢占优先级 + 2位响应优先级
    // 抢占优先级范围：0~3，响应优先级范围：0~3
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    
    // -------------------- USART1 中断（最高抢占优先级）--------------------
    // 抢占优先级=0（最高），响应优先级=1
    // 说明：可以打断所有其他中断（TIM2、TIM3、EXTI0）
    NVIC_InitStruct.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0;  // 抢占优先级最高
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;         // 响应优先级
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
    
    // -------------------- TIM3 中断（中等抢占优先级）--------------------
    // 抢占优先级=1，响应优先级=0
    // 说明：可以打断 TIM2，但不能打断 USART1
    // 与 EXTI0 同级抢占优先级，但响应优先级更高
    NVIC_InitStruct.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;         // 响应优先级高于 EXTI0
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
    
    // -------------------- EXTI0 中断（中等抢占优先级）--------------------
    // 抢占优先级=1，响应优先级=1
    // 说明：与 TIM3 同级抢占优先级，但不能打断 TIM3（响应优先级低）
    NVIC_InitStruct.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 1;         // 响应优先级低于 TIM3
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
    
    // -------------------- TIM2 中断（最低抢占优先级）--------------------
    // 抢占优先级=2（最低），响应优先级=0
    // 说明：可被 USART1、TIM3、EXTI0 打断
    NVIC_InitStruct.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 2;  // 抢占优先级最低
    NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
}

/**
 * @brief  GPIO 配置
 */
void GPIO_Configuration(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    
    // PA0: 按键输入（外部中断）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;  // 上拉输入
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PA9: USART1 TX（复用推挽输出）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PA10: USART1 RX（浮空输入）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PB0: LED 输出（指示 TIM2 运行）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // PB1: LED 输出（指示 TIM3 运行）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_1;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/**
 * @brief  TIM2 配置（1ms 定时中断）
 * @note   系统节拍定时器，最低优先级
 */
void TIM2_Configuration(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
    
    // TIM2 时钟 = APB1 * 2 = 36MHz * 2 = 72MHz
    // 预分频：72MHz / 72 = 1MHz
    // 计数周期：1MHz / 1000 = 1kHz (1ms)
    TIM_TimeBaseInitStruct.TIM_Period = 1000 - 1;
    TIM_TimeBaseInitStruct.TIM_Prescaler = 72 - 1;
    TIM_TimeBaseInitStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStruct);
    
    // 使能更新中断
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
}

/**
 * @brief  TIM3 配置（10ms 定时中断）
 * @note   数据处理定时器，中等优先级
 */
void TIM3_Configuration(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
    
    // 10ms 定时
    TIM_TimeBaseInitStruct.TIM_Period = 10000 - 1;
    TIM_TimeBaseInitStruct.TIM_Prescaler = 72 - 1;
    TIM_TimeBaseInitStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStruct);
    
    // 使能更新中断
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM3, ENABLE);  // 启动 TIM3
}

/**
 * @brief  USART1 配置（115200 baud）
 * @note   串口接收中断，最高优先级
 */
void USART1_Configuration(void)
{
    USART_InitTypeDef USART_InitStruct;
    
    USART_InitStruct.USART_BaudRate = 115200;
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits = USART_StopBits_1;
    USART_InitStruct.USART_Parity = USART_Parity_No;
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &USART_InitStruct);
    
    // 使能接收中断
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART1, ENABLE);
}

/**
 * @brief  EXTI0 配置（按键外部中断）
 * @note   下降沿触发，中等优先级
 */
void EXTI0_Configuration(void)
{
    // 配置 AFIO：PA0 映射到 EXTI0
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    
    EXTI_InitTypeDef EXTI_InitStruct;
    EXTI_InitStruct.EXTI_Line = EXTI_Line0;
    EXTI_InitStruct.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Falling;  // 下降沿触发
    EXTI_InitStruct.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStruct);
}

// ============================================================================
// 中断服务函数（ISR）
// ============================================================================

/**
 * @brief  TIM2 中断服务函数（1ms节拍，最低优先级）
 * @note   可被 USART1、TIM3、EXTI0 打断
 */
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        // 清除中断标志（必须在最开始清除，防止重复进入）
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        
        // 系统节拍计数
        g_SystemTick++;
        
        // LED 闪烁（指示 TIM2 运行）
        if (g_SystemTick % 500 == 0)
        {
            GPIO_WriteBit(GPIOB, GPIO_Pin_0, 
                (BitAction)(1 - GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_0)));
        }
        
        // 模拟耗时操作（用于演示中断嵌套）
        // 当这个耗时操作执行时，如果有 USART1 中断，会立即抢占
        for (volatile int i = 0; i < 100; i++);
    }
}

/**
 * @brief  TIM3 中断服务函数（10ms数据处理，中等优先级）
 * @note   可打断 TIM2，但不能打断 USART1
 */
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        
        g_Tim3Count++;
        
        // LED 闪烁（指示 TIM3 运行）
        GPIO_WriteBit(GPIOB, GPIO_Pin_1, 
            (BitAction)(1 - GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_1)));
        
        // 模拟数据处理
        // 如果此时 TIM2 在执行，会暂停等待此 ISR 完成
        // 如果此时 USART1 中断发生，此 ISR 会被抢占
    }
}

/**
 * @brief  USART1 中断服务函数（最高优先级）
 * @note   可以抢占所有其他中断
 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        // 读取接收数据（自动清除 RXNE 标志）
        uint8_t data = USART_ReceiveData(USART1);
        
        // 存储到缓冲区
        if (g_RxIndex < sizeof(g_RxBuffer) - 1)
        {
            g_RxBuffer[g_RxIndex++] = data;
            
            // 接收到回车或换行，标记接收完成
            if (data == '\r' || data == '\n')
            {
                g_RxBuffer[g_RxIndex - 1] = '\0';
                g_RxComplete = 1;
            }
        }
        
        // 回显
        USART_SendData(USART1, data);
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    }
}

/**
 * @brief  EXTI0 中断服务函数（按键，中等优先级）
 * @note   与 TIM3 同级抢占优先级，响应优先级低于 TIM3
 */
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        // 清除中断标志
        EXTI_ClearITPendingBit(EXTI_Line0);
        
        // 按键消抖（简单延时）
        for (volatile int i = 0; i < 10000; i++);
        
        // 确认按键仍然按下
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_RESET)
        {
            g_Exti0Count++;
            printf("[EXTI0] Button pressed! Count=%lu\r\n", g_Exti0Count);
        }
    }
}

// ============================================================================
// 重定向 printf 到 USART1（用于调试输出）
// ============================================================================

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

### 10.3 代码要点说明

#### A. NVIC 配置顺序
```c
// 正确顺序：先配置 NVIC，再初始化外设
NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);  // 第1步：分组
NVIC_Init(...);                                     // 第2步：各中断优先级
TIM2_Configuration();                              // 第3步：外设配置
TIM_Cmd(TIM2, ENABLE);                             // 第4步：启动外设
```

#### B. 中断标志清除
```c
void TIM2_IRQHandler(void)
{
    // 必须在 ISR 最开始清除标志！
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);  // 立即清除
        // 用户代码...
    }
}
```

#### C. 中断嵌套演示
当运行以下代码时，可以观察到嵌套效果：

```c
// 在 TIM2 ISR 中（优先级2，最低）
for (volatile int i = 0; i < 100; i++);  // 模拟耗时操作
// 执行期间如果：
// - USART1 中断发生 → 立即抢占，执行 USART1 ISR，完成后返回这里继续
// - TIM3 中断发生 → 立即抢占，执行 TIM3 ISR，完成后返回这里继续
// - EXTI0 中断发生 → 等待当前循环完成（同抢占优先级不嵌套）
```

### 10.4 编译与调试

**编译配置**:
1. 在 Keil/VS Code 中创建 STM32F103 工程
2. 添加标准库文件（StdPeriph）
3. 配置编译选项：
   - 芯片型号：STM32F103C8
   - 使用微库（Use MicroLIB）
   - 包含路径添加标准库头文件

**调试观察**:
1. 在 NVIC_Configuration() 设置断点，观察优先级配置
2. 在各个 ISR 中设置断点，验证中断触发
3. 使用串口助手发送数据，观察 USART1 抢占效果
4. 按按键观察 EXTI0 与 TIM3 的优先级关系

---

## 十一、总结

### 核心要点回顾

```
┌─────────────────────────────────────────────────────┐
│                    NVIC 核心概念                      │
├─────────────────────────────────────────────────────┤
│                                                      │
│  1. NVIC 是 ARM Cortex-M 内核的中断控制器            │
│                                                      │
│  2. 优先级 = 抢占优先级 + 响应优先级                  │
│     • 抢占优先级 → 决定是否可以打断（嵌套）           │
│     • 响应优先级 → 决定排队顺序                      │
│                                                      │
│  3. 优先级分组（5种）                                │
│     Group0: 0位抢占 + 4位响应                        │
│     Group1: 1位抢占 + 3位响应                        │
│     Group2: 2位抢占 + 2位响应 ← 最常用               │
│     Group3: 3位抢占 + 1位响应                        │
│     Group4: 4位抢占 + 0位响应                        │
│                                                      │
│  4. 数值越小，优先级越高（0是最高）                   │
│                                                      │
│  5. 配置流程：                                        │
│     NVIC_PriorityGroupConfig() → NVIC_Init()         │
│                                                      │
└─────────────────────────────────────────────────────┘
```

### 记忆口诀

> **"抢占嵌套，响应排队，数值越小，优先级越美"**

---

**参考文档**:
- 《STM32F10xxx 参考手册》
- 《Cortex-M3 权威指南》
- 《ARM Cortex-M3 技术参考手册》

**文档版本**: v1.0  
**更新日期**: 2026年3月
