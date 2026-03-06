# RCC 详解（复位与时钟控制器）

> RCC（Reset and Clock Control）是 STM32 的时钟管理核心，负责配置系统时钟源、分频器和外设时钟使能。正确理解 RCC 是掌握 STM32 的第一步。

---

![image.png](https://raw.githubusercontent.com/ZhiKong0/Image_Auto/main/Obsidian/20260305094034983.png)

## 一、RCC 基本概念

### 1.1 什么是 RCC？

**RCC** 全称 **Reset and Clock Control**（复位与时钟控制器），是 STM32 中负责**时钟管理**和**系统复位**的控制单元。

**核心功能**：
- 选择和配置系统时钟源（HSE/HSI/PLL）
- 配置各总线时钟频率（AHB/APB1/APB2）
- 控制各外设时钟的使能/禁用
- 管理系统复位（上电复位、看门狗复位等）

### 1.2 RCC 的核心特性

| 特性 | 说明 |
|------|------|
| **时钟源** | 4种独立时钟源：HSE、HSI、LSE、LSI |
| **PLL锁相环** | 支持时钟倍频（最高72MHz@STM32F1） |
| **总线架构** | AHB（高速）+ APB1（低速）+ APB2（高速） |
| **时钟安全** | CSS时钟安全系统，HSE失效自动切换HSI |
| **外设控制** | 每个外设独立时钟使能，降低功耗 |
| **位置** | **芯片外设**，通过寄存器配置 |

> **注意**：RCC 是 STM32 芯片厂商设计的外设，不同系列（F1/F4/H7）的 RCC 结构和寄存器有所差异。

---

## 二、时钟源详解

### 2.1 四种时钟源

STM32F1 有 **4个独立时钟源**：

| 时钟源 | 英文 | 频率 | 精度 | 用途 |
|--------|------|------|------|------|
| **HSE** | High Speed External | 4~16MHz（通常8MHz） | 高（晶振） | 系统主时钟源 |
| **HSI** | High Speed Internal | 8MHz | 中（RC振荡器） | 备用/启动时钟 |
| **LSE** | Low Speed External | 32.768kHz | 高 | RTC时钟 |
| **LSI** | Low Speed Internal | 约40kHz | 低 | 独立看门狗(IWDG) |

### 2.2 时钟源选择策略

```
时钟源选择决策树：

需要高精度时钟？
├── 是 → 有外部晶振？
│         ├── 是 → 使用 HSE（8MHz晶振）
│         └── 否 → 使用 HSI（内部8MHz）
└── 否 → 对功耗敏感？
          ├── 是 → 使用 HSI（无需外部元件）
          └── 否 → 使用 HSE（稳定性更好）
```

### 2.3 时钟源对比

| 场景 | 推荐时钟源 | 原因 |
|------|-----------|------|
| 需要串口/USB精确通信 | HSE | 精度高，波特率误差小 |
| 快速原型开发 | HSI | 省去晶振，电路简单 |
| 低功耗应用 | HSI + 时钟分频 | 无需外部元件 |
| RTC实时时钟 | LSE | 32.768kHz标准频率 |
| 看门狗 | LSI | 独立时钟，不受主时钟影响 |

---

## 三、时钟树结构（Clock Tree）

### 3.1 STM32F1 时钟树

```
                    ┌─────────────────────────────────────┐
                    │           时钟源选择                  │
                    └─────────────────────────────────────┘
                                     │
           ┌─────────────────────────┼─────────────────────────┐
           │                         │                         │
           ▼                         ▼                         ▼
    ┌─────────────┐          ┌─────────────┐          ┌─────────────┐
    │   HSE       │          │   HSI       │          │   PLL       │
    │ 8MHz晶振    │          │ 内部8MHz    │          │ 倍频输出    │
    └──────┬──────┘          └──────┬──────┘          └──────┬──────┘
           │                         │                         │
           │    ┌────────────────────┘                         │
           │    │    HSE / 2 预分频（可选）                       │
           │    ▼                                                 │
           └───►┌─────────────┐                                   │
                │  PLLSRC     │◄─────────────────────────────────┘
                │  选择器      │
                └──────┬──────┘
                       │
                       ▼
                ┌─────────────┐
                │  PLLMUL     │  ×2, ×3, ..., ×16
                │  倍频器      │
                └──────┬──────┘
                       │ 72MHz (8MHz × 9)
                       ▼
    ┌──────────────────────────────────────────────────────────────┐
    │                     SYSCLK 系统时钟                           │
    │                      (最高72MHz)                             │
    └──────────────────────────┬───────────────────────────────────┘
                               │
           ┌───────────────────┼───────────────────┐
           │                   │                   │
           ▼                   ▼                   ▼
    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
    │   AHB       │    │   APB1      │    │   APB2      │
    │ 预分频器     │    │ 预分频器     │    │ 预分频器     │
    │ /1, /2.../512│    │ /1, /2, /4   │    │ /1, /2, /4   │
    └──────┬──────┘    └──────┬──────┘    └──────┬──────┘
           │                   │                   │
           ▼                   ▼                   ▼
    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
    │   HCLK      │    │   PCLK1     │    │   PCLK2     │
    │   72MHz     │    │   36MHz     │    │   72MHz     │
    │ (AHB总线)   │    │ (APB1总线)  │    │ (APB2总线)  │
    └──────┬──────┘    └──────┬──────┘    └──────┬──────┘
           │                   │                   │
           │                   │                   │
           ▼                   ▼                   ▼
    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
    │ AHB外设时钟  │    │ APB1外设时钟 │    │ APB2外设时钟 │
    │ (DMA/FSMC)  │    │ (TIM2~7/    │    │ (TIM1/       │
    │             │    │  USART2~3/   │    │  USART1/     │
    │             │    │  I2C/SPI2/   │    │  SPI1/ADC)   │
    │             │    │  UART4~5)    │    │              │
    └─────────────┘    └─────────────┘    └─────────────┘
```

### 3.2 总线时钟频率限制

| 总线 | 最高频率 | 连接外设 | 备注 |
|------|:--------:|:---------|:-----|
| **AHB** | 72MHz | DMA、FSMC、FLASH | 系统主总线 |
| **APB1** | 36MHz | TIM2~7、USART2~3、SPI2、I2C1~2、UART4~5 | 低速外设 |
| **APB2** | 72MHz | TIM1、USART1、SPI1、ADC1~3、GPIOA~G | 高速外设 |

> **重要**：APB1 上的定时器（TIM2~7）时钟为 36MHz 时，由于倍频机制，定时器实际时钟为 72MHz。

---

## 四、PLL 配置详解

### 4.1 PLL 作用与原理

**PLL（Phase Locked Loop）锁相环**：将低频时钟倍频为高频时钟

```
PLL 计算公式：

SYSCLK = (HSE 或 HSI/2) × PLLMUL

示例（常用配置）：
- HSE = 8MHz
- PLLMUL = ×9
- SYSCLK = 8MHz × 9 = 72MHz
```

### 4.2 PLL 配置参数

| 参数 | 选项 | 说明 |
|------|------|------|
| **PLLSRC** | HSE 或 HSI/2 | PLL输入时钟源选择 |
| **PLLMUL** | ×2 ~ ×16 | 倍频系数 |
| **HSE分频** | /1 或 /2 | HSE输入PLL前的预分频 |

### 4.3 常用 PLL 配置

| 目标SYSCLK | 时钟源 | 源频率 | PLLMUL | 计算 |
|:----------:|:------:|:------:|:------:|:----:|
| 72MHz | HSE | 8MHz | ×9 | 8×9=72 |
| 72MHz | HSE | 12MHz | ×6 | 12×6=72 |
| 64MHz | HSE | 8MHz | ×8 | 8×8=64 |
| 48MHz | HSE | 8MHz | ×6 | 8×6=48 |
| 24MHz | HSI | 8MHz/2=4MHz | ×6 | 4×6=24 |

---

## 五、RCC 配置和使用

### 5.1 标准库时钟配置函数

```c
// RCC 初始化结构体（标准库）
typedef struct
{
    uint32_t SYSCLK_Frequency;   // 系统时钟频率
    uint32_t HCLK_Frequency;     // AHB总线时钟频率
    uint32_t PCLK1_Frequency;    // APB1总线时钟频率
    uint32_t PCLK2_Frequency;    // APB2总线时钟频率
    uint32_t ADCCLK_Frequency;   // ADC时钟频率
} RCC_ClocksTypeDef;

// 获取当前时钟频率
RCC_ClocksTypeDef RCC_Clocks;
RCC_GetClocksFreq(&RCC_Clocks);
```

### 5.2 常用 RCC 配置函数

| 函数 | 功能 |
|------|------|
| `RCC_HSEConfig(RCC_HSE_ON)` | 启用/禁用HSE |
| `RCC_HSICmd(ENABLE)` | 启用HSI |
| `RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9)` | 配置PLL |
| `RCC_PLLCmd(ENABLE)` | 启用PLL |
| `RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK)` | 切换系统时钟源 |
| `RCC_HCLKConfig(RCC_SYSCLK_Div1)` | 配置AHB预分频 |
| `RCC_PCLK1Config(RCC_HCLK_Div2)` | 配置APB1预分频 |
| `RCC_PCLK2Config(RCC_HCLK_Div1)` | 配置APB2预分频 |
| `RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE)` | 使能APB2外设时钟 |
| `RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE)` | 使能APB1外设时钟 |
| `RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE)` | 使能AHB外设时钟 |

### 5.3 标准时钟配置流程（72MHz）

```c
/**
 * @brief  RCC 时钟配置（标准库版）
 * @note   系统时钟 72MHz，APB1=36MHz，APB2=72MHz
 */
void RCC_Configuration(void)
{
    // ==================== 第1步：启用 HSE ====================
    // 启用外部高速晶振（8MHz）
    RCC_HSEConfig(RCC_HSE_ON);
    
    // 等待 HSE 就绪（必须有超时检测，避免死等）
    ErrorStatus HSEStartUpStatus;
    HSEStartUpStatus = RCC_WaitForHSEStartUp();
    if (HSEStartUpStatus == SUCCESS)
    {
        // ==================== 第2步：配置 Flash 等待周期 ====================
        // 当 SYSCLK > 48MHz 时，Flash 需要 2个等待周期
        FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
        FLASH_SetLatency(FLASH_Latency_2);
        
        // ==================== 第3步：配置 AHB/APB 分频器 ====================
        // AHB = SYSCLK / 1 = 72MHz
        RCC_HCLKConfig(RCC_SYSCLK_Div1);
        
        // APB1 = HCLK / 2 = 36MHz（不能超过36MHz！）
        RCC_PCLK1Config(RCC_HCLK_Div2);
        
        // APB2 = HCLK / 1 = 72MHz
        RCC_PCLK2Config(RCC_HCLK_Div1);
        
        // ==================== 第4步：配置 PLL ====================
        // PLLCLK = HSE × 9 = 8MHz × 9 = 72MHz
        RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
        
        // 启用 PLL
        RCC_PLLCmd(ENABLE);
        
        // 等待 PLL 就绪
        while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
        
        // ==================== 第5步：切换系统时钟到 PLL ====================
        RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
        
        // 等待系统时钟切换完成
        while (RCC_GetSYSCLKSource() != 0x08);  // 0x08 表示 PLL 作为系统时钟
    }
    else
    {
        // HSE 启动失败，使用 HSI 作为备用
        // HSI = 8MHz，不需要 PLL
        // 或者触发错误处理
    }
}
```

---

## 六、外设时钟使能

### 6.1 外设时钟总线分布

| 总线 | 外设 | 时钟使能函数 |
|------|------|-------------|
| **AHB** | DMA1/DMA2 | `RCC_AHBPeriphClockCmd()` |
| | FSMC | `RCC_AHBPeriphClockCmd()` |
| | CRC | `RCC_AHBPeriphClockCmd()` |
| **APB1** | TIM2~7 | `RCC_APB1PeriphClockCmd()` |
| | USART2~3, UART4~5 | `RCC_APB1PeriphClockCmd()` |
| | SPI2, I2C1~2 | `RCC_APB1PeriphClockCmd()` |
| | CAN1~2, USB | `RCC_APB1PeriphClockCmd()` |
| | PWR, BKP | `RCC_APB1PeriphClockCmd()` |
| **APB2** | TIM1, TIM8~11 | `RCC_APB2PeriphClockCmd()` |
| | USART1 | `RCC_APB2PeriphClockCmd()` |
| | SPI1 | `RCC_APB2PeriphClockCmd()` |
| | ADC1~3 | `RCC_APB2PeriphClockCmd()` |
| | GPIOA~G | `RCC_APB2PeriphClockCmd()` |
| | AFIO | `RCC_APB2PeriphClockCmd()` |

### 6.2 外设时钟使能示例

```c
void Enable_Peripheral_Clocks(void)
{
    // ==================== APB2 外设时钟 ====================
    // GPIO 时钟（使用任何 GPIO 前必须使能）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | 
                           RCC_APB2Periph_GPIOB | 
                           RCC_APB2Periph_GPIOC, ENABLE);
    
    // 复用功能时钟（使用外部中断、重映射等需要）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    
    // USART1 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    
    // TIM1 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
    
    // ADC1 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    
    // ==================== APB1 外设时钟 ====================
    // TIM2 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    
    // TIM3 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    
    // USART2 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    
    // SPI2 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
    
    // I2C1 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    
    // ==================== AHB 外设时钟 ====================
    // DMA1 时钟
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    
    // DMA2 时钟（大容量产品）
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
}
```

### 6.3 外设时钟使能注意事项

| 问题 | 原因 | 解决 |
|------|------|------|
| 外设不工作 | 忘记使能时钟 | 检查对应总线的时钟使能 |
| 低功耗异常 | 未禁用不需要的外设时钟 | 使用 `...ClockCmd(..., DISABLE)` |
| GPIO 不响应 | 忘记使能 AFIO 时钟 | 重映射和外部中断需要 AFIO |

---

## 七、系统复位详解

### 7.1 三种系统复位源

| 复位源 | 触发条件 | 复位范围 |
|--------|---------|---------|
| **电源复位** | 上电/掉电/POR/PDR | 全部寄存器 |
| **系统复位** | NRST引脚/看门狗/软件复位 | 除备份域外全部 |
| **备份域复位** | BKP相关 | 仅备份域寄存器 |

### 7.2 软件复位

```c
// 软件触发系统复位
NVIC_SystemReset();  // 或者
SCB->AIRCR = 0x05FA0004;  // 直接写寄存器
```

### 7.3 复位标志检查

```c
void Check_Reset_Source(void)
{
    if (RCC_GetFlagStatus(RCC_FLAG_PORRST) == SET)
    {
        printf("上电复位\r\n");
        RCC_ClearFlag();  // 清除标志
    }
    else if (RCC_GetFlagStatus(RCC_FLAG_SFTRST) == SET)
    {
        printf("软件复位\r\n");
        RCC_ClearFlag();
    }
    else if (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) == SET)
    {
        printf("独立看门狗复位\r\n");
        RCC_ClearFlag();
    }
    else if (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) == SET)
    {
        printf("窗口看门狗复位\r\n");
        RCC_ClearFlag();
    }
}
```

---

## 八、时钟安全系统（CSS）

### 8.1 CSS 作用

**Clock Security System（时钟安全系统）**：当 HSE 失效时，自动切换到 HSI，保证系统不挂死。

### 8.2 CSS 配置

```c
// 启用时钟安全系统
RCC_ClockSecuritySystemCmd(ENABLE);

// 在 NMI 中断处理 CSS 事件
void NMI_Handler(void)
{
    if (RCC_GetITStatus(RCC_IT_CSS) == SET)
    {
        // HSE 失效！系统已自动切换到 HSI
        // 进行应急处理：报警、记录日志、尝试恢复等
        
        // 清除 CSS 中断标志
        RCC_ClearITPendingBit(RCC_IT_CSS);
    }
}
```

---

## 九、常见错误与注意事项

### 9.1 时钟配置陷阱

| 问题 | 原因 | 解决 |
|------|------|------|
| 系统死机 | APB1 时钟超过36MHz | 检查 `RCC_PCLK1Config`，必须≤36MHz |
| Flash 错误 | SYSCLK>48MHz但Flash等待周期=0 | 设置 `FLASH_SetLatency(FLASH_Latency_2)` |
| HSE 启动失败 | 晶振未起振或连接错误 | 检查晶振电路，或改用 HSI |
| 外设不工作 | 未使能外设时钟 | 检查 `RCC_APBxPeriphClockCmd` |
| 串口乱码 | 波特率计算错误（时钟配置与实际不符） | 确认 PCLK1/PCLK2 频率正确 |

### 9.2 Flash 等待周期配置

| SYSCLK 频率 | Flash 等待周期 | 配置代码 |
|:----------:|:--------------:|:---------|
| 0 < SYSCLK ≤ 24MHz | 0 | `FLASH_SetLatency(FLASH_Latency_0)` |
| 24 < SYSCLK ≤ 48MHz | 1 | `FLASH_SetLatency(FLASH_Latency_1)` |
| 48 < SYSCLK ≤ 72MHz | 2 | `FLASH_SetLatency(FLASH_Latency_2)` |

### 9.3 最佳实践

1. **时钟配置顺序**：启用HSE → 配置Flash → 配置分频器 → 配置PLL → 切换系统时钟
2. **等待检查**：每次启用 HSE/PLL 后都要等待就绪标志
3. **外设时钟**：使用外设前**一定**要检查是否已使能时钟
4. **错误处理**：HSE 启动失败要有备用方案（使用 HSI）
5. **时钟频率**：确认外设时钟频率与预期一致（特别是定时器、串口波特率计算）

---

## 十、完整工程示例

### 10.1 工程概述

本示例演示完整的 RCC 配置流程：
- **系统时钟**：72MHz（HSE 8MHz × 9）
- **AHB 时钟**：72MHz
- **APB1 时钟**：36MHz
- **APB2 时钟**：72MHz
- **使能外设**：GPIOA、USART1、TIM2

### 10.2 完整 main.c 代码

```c
/**
 * @file    main.c
 * @brief   RCC 时钟配置完整示例
 * @author  Your Name
 * @version V1.0
 * @date    2026-03-03
 * 
 * 本示例演示 STM32F1 的完整 RCC 配置流程
 */

#include "stm32f10x.h"
#include <stdio.h>

// 全局变量：存储当前时钟频率
uint32_t g_SysClockFreq = 0;
uint32_t g_AHB_ClockFreq = 0;
uint32_t g_APB1_ClockFreq = 0;
uint32_t g_APB2_ClockFreq = 0;

// 函数声明
void RCC_Configuration(void);
void GPIO_Configuration(void);
void USART1_Configuration(void);
void Print_Clock_Info(void);

/**
 * @brief  主函数
 */
int main(void)
{
    // ==================== 第1步：配置系统时钟 ====================
    RCC_Configuration();
    
    // ==================== 第2步：获取并打印时钟信息 ====================
    Print_Clock_Info();
    
    // ==================== 第3步：配置外设 ====================
    GPIO_Configuration();
    USART1_Configuration();
    
    // ==================== 主循环 ====================
    printf("\r\n========== System Started ==========\r\n");
    printf("System Clock: %lu MHz\r\n", g_SysClockFreq / 1000000);
    printf("AHB Clock:    %lu MHz\r\n", g_AHB_ClockFreq / 1000000);
    printf("APB1 Clock:   %lu MHz\r\n", g_APB1_ClockFreq / 1000000);
    printf("APB2 Clock:   %lu MHz\r\n", g_APB2_ClockFreq / 1000000);
    printf("=====================================\r\n\r\n");
    
    while (1)
    {
        // LED 闪烁（使用 PA8）
        GPIO_WriteBit(GPIOA, GPIO_Pin_8, 
            (BitAction)(1 - GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_8)));
        
        // 延时（简单循环延时，实际项目建议用定时器）
        for (volatile uint32_t i = 0; i < 5000000; i++);
        
        printf("System Running... SYSCLK=%luMHz\r\n", g_SysClockFreq / 1000000);
    }
}

/**
 * @brief  RCC 时钟配置（完整版）
 * @note   系统时钟 72MHz
 */
void RCC_Configuration(void)
{
    ErrorStatus HSEStartUpStatus;
    
    // ==================== 第1步：复位 RCC 寄存器 ====================
    RCC_DeInit();
    
    // ==================== 第2步：启用 HSE ====================
    RCC_HSEConfig(RCC_HSE_ON);
    HSEStartUpStatus = RCC_WaitForHSEStartUp();
    
    if (HSEStartUpStatus == SUCCESS)
    {
        // ==================== 第3步：配置 Flash 等待周期 ====================
        // SYSCLK = 72MHz > 48MHz，需要 2 个等待周期
        FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
        FLASH_SetLatency(FLASH_Latency_2);
        
        // ==================== 第4步：配置预分频器 ====================
        // HCLK = SYSCLK / 1 = 72MHz
        RCC_HCLKConfig(RCC_SYSCLK_Div1);
        
        // PCLK1 = HCLK / 2 = 36MHz（APB1不能超过36MHz！）
        RCC_PCLK1Config(RCC_HCLK_Div2);
        
        // PCLK2 = HCLK / 1 = 72MHz
        RCC_PCLK2Config(RCC_HCLK_Div1);
        
        // ADC 时钟 = PCLK2 / 6 = 12MHz（不超过14MHz）
        RCC_ADCCLKConfig(RCC_PCLK2_Div6);
        
        // ==================== 第5步：配置 PLL ====================
        // PLLCLK = HSE × 9 = 8MHz × 9 = 72MHz
        RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
        
        // 启用 PLL
        RCC_PLLCmd(ENABLE);
        
        // 等待 PLL 就绪
        while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
        
        // ==================== 第6步：切换到 PLL 作为系统时钟 ====================
        RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
        
        // 等待切换完成
        while (RCC_GetSYSCLKSource() != 0x08);  // 0x08 = PLL
    }
    else
    {
        // HSE 启动失败，使用 HSI
        // HSI = 8MHz，不使用 PLL
        RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
        while (RCC_GetSYSCLKSource() != 0x00);  // 0x00 = HSI
        
        // 记录错误（实际项目中应进行错误处理）
        // HSE_Error_Handler();
    }
    
    // ==================== 第7步：使能外设时钟 ====================
    // 使能 GPIOA、GPIOB、AFIO 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | 
                           RCC_APB2Periph_GPIOB | 
                           RCC_APB2Periph_AFIO, ENABLE);
    
    // 使能 USART1 时钟（在 APB2 上）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    
    // 使能 TIM2 时钟（在 APB1 上）
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
}

/**
 * @brief  获取并保存时钟频率
 */
void Print_Clock_Info(void)
{
    RCC_ClocksTypeDef RCC_Clocks;
    
    // 获取当前各总线时钟频率
    RCC_GetClocksFreq(&RCC_Clocks);
    
    // 保存到全局变量
    g_SysClockFreq = RCC_Clocks.SYSCLK_Frequency;
    g_AHB_ClockFreq = RCC_Clocks.HCLK_Frequency;
    g_APB1_ClockFreq = RCC_Clocks.PCLK1_Frequency;
    g_APB2_ClockFreq = RCC_Clocks.PCLK2_Frequency;
}

/**
 * @brief  GPIO 配置
 */
void GPIO_Configuration(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    
    // PA8: LED 输出
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_8;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PA9: USART1 TX（复用推挽）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PA10: USART1 RX（浮空输入）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/**
 * @brief  USART1 配置（115200 baud）
 */
void USART1_Configuration(void)
{
    USART_InitTypeDef USART_InitStruct;
    
    // 计算波特率时使用 PCLK2 = 72MHz
    USART_InitStruct.USART_BaudRate = 115200;
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits = USART_StopBits_1;
    USART_InitStruct.USART_Parity = USART_Parity_No;
    USART_InitStruct.USART_Mode = USART_Mode_Tx;  // 仅发送
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    
    USART_Init(USART1, &USART_InitStruct);
    USART_Cmd(USART1, ENABLE);
}

// ============================================================================
// 重定向 printf 到 USART1
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

#### A. 时钟配置顺序
```c
// 正确顺序：
RCC_DeInit();           // 第1步：复位
RCC_HSEConfig(ON);      // 第2步：启用HSE
FLASH_SetLatency(2);    // 第3步：配置Flash
RCC_HCLKConfig(Div1);   // 第4步：配置分频器
RCC_PLLConfig(...);     // 第5步：配置PLL
RCC_PLLCmd(ENABLE);     // 第6步：启用PLL
RCC_SYSCLKConfig(PLL);  // 第7步：切换系统时钟
```

#### B. 频率验证
```c
// 配置完成后验证时钟频率
RCC_ClocksTypeDef clocks;
RCC_GetClocksFreq(&clocks);

// 检查是否达到预期
if (clocks.SYSCLK_Frequency != 72000000)
{
    // 时钟配置错误！
}
```

#### C. APB1 时钟限制
```c
// APB1 不能超过 36MHz！
// 错误示例：
RCC_PCLK1Config(RCC_HCLK_Div1);  // 如果HCLK=72，则PCLK1=72，超过限制！

// 正确做法：
RCC_PCLK1Config(RCC_HCLK_Div2);  // PCLK1 = 36MHz
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
1. 在 `RCC_Configuration()` 设置断点，逐步观察时钟配置过程
2. 在 `Print_Clock_Info()` 处检查各时钟频率是否正确
3. 使用示波器观察 HSE 晶振波形（OSC_IN/OSC_OUT）
4. 验证 USART 输出波特率是否准确（115200bps）

---

## 十一、总结

### 核心要点回顾

```
┌─────────────────────────────────────────────────────┐
│                    RCC 核心概念                     │
├─────────────────────────────────────────────────────┤
│                                                      │
│  1. RCC 是 STM32 的时钟管理中心                       │
│                                                      │
│  2. 4个时钟源：HSE、HSI、LSE、LSI                     │
│     • HSE（8MHz 晶振）→ 系统主时钟首选               │
│     • HSI（内部 8MHz）→ 备用时钟                    │
│                                                      │
│  3. 3条总线：AHB（72M）、APB1（36M）、APB2（72M）    │
│                                                      │
│  4. PLL 倍频：HSE × N = SYSCLK（最高72MHz）          │
│                                                      │
│  5. 配置流程：                                        │
│     启用HSE → 配置Flash → 配置分频器 → 配置PLL        │
│     → 启用PLL → 切换系统时钟 → 使能外设时钟           │
│                                                      │
│  6. 关键限制：                                        │
│     • APB1 ≤ 36MHz                                  │
│     • SYSCLK > 48MHz 时 Flash 等待周期 ≥ 2          │
│     • 使用外设前必须使能其时钟                      │
│                                                      │
└─────────────────────────────────────────────────────┘
```

### 记忆口诀

> **"HSE为源，PLL倍频，先分频后使能，APB1别超36"**

---

**参考文档**:
- 《STM32F10xxx 参考手册》第6章 RCC
- 《STM32F10xxx 闪存编程手册》
- 《STM32 时钟配置工具 AN2867》

**文档版本**: v1.0  
**更新日期**: 2026年3月
