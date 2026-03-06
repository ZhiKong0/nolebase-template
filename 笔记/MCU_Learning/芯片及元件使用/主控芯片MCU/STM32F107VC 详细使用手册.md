# STM32F107VC Connectivity Line MCU 详细使用手册

> **文档版本**: 1.0  
> **基于**: STM32F105xx/107xx Reference Manual  
> **适用芯片**: STM32F107VC (LQFP100封装)

---

## 目录

1. [芯片概述](#1-芯片概述)
2. [系统架构与内存映射](#2-系统架构与内存映射)
3. [时钟系统](#3-时钟系统)
4. [电源管理](#4-电源管理)
5. [GPIO配置](#5-gpio配置)
6. [通信接口](#6-通信接口)
   - [Ethernet MAC](#61-ethernet-mac)
   - [USB OTG](#62-usb-otg)
   - [CAN接口](#63-can接口)
   - [SPI/I2S](#64-spii2s)
   - [I2C](#65-i2c)
   - [USART](#66-usart)
7. [定时器](#7-定时器)
8. [ADC与DAC](#8-adc与dac)
9. [DMA控制器](#9-dma控制器)
10. [中断系统](#10-中断系统)
11. [代码示例](#11-代码示例)

---

## 1. 芯片概述

### 1.1 主要特性

| 参数 | 规格 |
|------|------|
| **内核** | ARM Cortex-M3 @ 72MHz |
| **性能** | 1.25 DMIPS/MHz (Dhrystone 2.1) |
| **Flash** | 64-256 KB |
| **SRAM** | 64 KB |
| **工作电压** | 2.0V - 3.6V |
| **GPIO** | 最多80个，大部分5V tolerant |
| **封装** | LQFP100 / LFBGA100 |
| **温度范围** | -40°C ~ +85°C (工业级) |

### 1.2 外设汇总

| 类型 | 数量 | 规格 |
|------|------|------|
| **定时器** | 10个 | 4×通用16位 + 1×电机控制 + 2×看门狗 + 2×基本 + SysTick |
| **ADC** | 2× | 12-bit, 1μs转换, 16通道, 2MSPS交错模式 |
| **DAC** | 2× | 12-bit |
| **DMA** | 1× | 12通道 |
| **I2C** | 2× | SMBus/PMBus支持, 400kHz |
| **SPI/I2S** | 3× | 18Mbit/s, SPI1/2支持I2S |
| **USART** | 5× | ISO7816, LIN, IrDA, 智能卡 |
| **CAN** | 2× | CAN 2.0B Active, 512B SRAM |
| **USB** | 1× | USB 2.0 OTG FS, 1.25KB SRAM |
| **Ethernet** | 1× | 10/100Mbps, MII/RMII, IEEE1588 |

### 1.3 启动模式

| BOOT1 | BOOT0 | 启动模式 | 说明 |
|-------|-------|----------|------|
| X | 0 | 主Flash | 从用户Flash启动 (默认) |
| 0 | 1 | 系统存储器 | 从System Memory启动 (ISP) |
| 1 | 1 | SRAM | 从SRAM启动 (调试) |

**BOOT引脚**：
- BOOT0: 引脚94 (LQFP100)
- BOOT1: 引脚37 (与PB2共享)

---

## 2. 系统架构与内存映射

### 2.1 系统架构

```
                    +------------------+
                    |   Cortex-M3 CPU  | 72MHz
                    |  NVIC (81中断)  |
                    +--------+---------+
                             |
        +--------------------+--------------------+
        |                    |                    |
   +----v----+         +------v------+     +-----v-----+
   |  Flash  |         |    SRAM     |     | 外设总线  |
   | 256KB |         |    64KB     |     | (APB1/2) |
   +---------+         +-------------+     +-----------+
        |                    |                    |
        |              +-----v------+             |
        |              |    DMA     | 12通道      |
        |              +------------+             |
        +-----------------------------------------+
```

### 2.2 内存映射

| 地址范围 | 大小 | 用途 |
|----------|------|------|
| 0x0000_0000 - 0x0003_FFFF | 256KB | Flash (代码或数据) |
| 0x0800_0000 - 0x0803_FFFF | 256KB | Flash (主区域) |
| 0x1FFF_0000 - 0x1FFF_7FFF | 32KB | System Memory (启动代码) |
| 0x1FFF_F000 - 0x1FFF_F80F | 2KB | Option Bytes |
| 0x2000_0000 - 0x2000_FFFF | 64KB | SRAM |
| 0x4000_0000 - 0x4000_33FF | - | APB1外设 |
| 0x4001_0000 - 0x4001_3FFF | - | APB2外设 |
| 0x4002_0000 - 0x4002_03FF | - | AHB外设 |

---

## 3. 时钟系统

### 3.1 时钟源

| 时钟源 | 频率范围 | 用途 |
|--------|----------|------|
| **HSI** | 8MHz (RC) | 内部高速，出厂校准±1% |
| **HSE** | 3-25MHz (晶振) | 外部高速，主时钟 |
| **LSI** | 40kHz (RC) | 内部低速，看门狗 |
| **LSE** | 32.768kHz (晶振) | 外部低速，RTC |
| **PLL** | xN倍频 | 系统主时钟 |

### 3.2 时钟树配置

```
                    +--------+
    HSE (8MHz) ---->|  PLL   | x9 = 72MHz (SYSCLK)
    HSI (8MHz) ---->| (×1~16)|
                    +----+---+
                         |
         +---------------+---------------+
         |               |               |
    +----v----+     +----v----+     +-----v-----+
    | AHB预分频 |     | APB1预分频 |     | APB2预分频 |
    | /1 (72MHz)|     | /2 (36MHz)|     | /1 (72MHz)|
    +----+-----+     +----+-----+     +-----+-----+
         |               |               |
    到DMA/Flash      到APB1外设      到APB2外设
                     (Max 36MHz)    (Max 72MHz)
```

### 3.3 时钟配置代码 (HAL库)

```c
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // 配置外部高速晶振HSE = 8MHz
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;  // 8MHz * 9 = 72MHz
    
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    // 配置总线时钟
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | 
                                  RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | 
                                  RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;   // HCLK = 72MHz
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;    // PCLK1 = 36MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;    // PCLK2 = 72MHz
    
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}
```

---

## 4. 电源管理

### 4.1 电源域

| 电源引脚 | 电压范围 | 功能 |
|----------|----------|------|
| **VDD** | 2.0-3.6V | 数字/模拟电源 |
| **VDDA** | 2.0-3.6V | 模拟电源 (ADC/DAC/复位) |
| **VSS** | 0V | 数字地 |
| **VSSA** | 0V | 模拟地 |
| **VBAT** | 1.8-3.6V | 备份域供电 (RTC/备份寄存器) |
| **VREF+** | 2.0-VDDA | ADC/DAC参考电压 |

### 4.2 低功耗模式

| 模式 | CPU | 外设 | 唤醒时间 | 功耗 |
|------|-----|------|----------|------|
| **Sleep** | 停止 | 运行 | 立即 | 中等 |
| **Stop** | 停止 | 停止(保留) | <10μs | 低 |
| **Standby** | 停止 | 停止(复位) | ~50μs | 最低 |

### 4.3 代码示例

```c
// 进入Sleep模式
void EnterSleepMode(void) {
    HAL_SuspendTick();
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    HAL_ResumeTick();
}

// 进入Stop模式
void EnterStopMode(void) {
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    // 唤醒后需要重新配置时钟
    SystemClock_Config();
}

// 进入Standby模式
void EnterStandbyMode(void) {
    HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1);
    HAL_PWR_EnterSTANDBYMode();
}
```

---

## 5. GPIO配置

### 5.1 GPIO特性

- **最大80个GPIO**，分为5组：PA(16), PB(16), PC(16), PD(15), PE(2)
- **大部分5V tolerant**，支持5V输入
- **独立配置**：每个引脚可单独配置
- **复用功能**：支持多种外设映射
- **重映射功能**：某些外设可映射到不同引脚

### 5.2 GPIO模式

| 模式 | 说明 |
|------|------|
| **Input Floating** | 浮空输入 |
| **Input Pull-up** | 上拉输入 |
| **Input Pull-down** | 下拉输入 |
| **Analog** | 模拟输入 (ADC/DAC) |
| **Output Open-drain** | 开漏输出 |
| **Output Push-pull** | 推挽输出 |
| **Alternate Function** | 复用功能 |

### 5.3 GPIO配置代码

```c
// GPIO初始化示例
void GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // 使能GPIO时钟
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    // LED输出 (PC13)
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    
    // 按键输入 (PA0)
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // USART1 TX (PA9) - 复用推挽
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // USART1 RX (PA10) - 浮空输入
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}
```

---

## 6. 通信接口

### 6.1 Ethernet MAC

#### 6.1.1 特性

- **10/100 Mbps** 自适应
- **专用DMA** + 4KB SRAM
- **MII/RMII** 接口支持
- **IEEE1588** 硬件时间戳 (PTP)
- **全双工/半双工** 支持

#### 6.1.2 RMII接口引脚 (默认)

| STM32引脚 | RMII信号 | 说明 |
|-----------|----------|------|
| PA1 | REF_CLK | 50MHz参考时钟 |
| PA2 | MDIO | 管理数据 |
| PA7 | CRS_DV | 载波侦听/数据有效 |
| PC1 | MDC | 管理时钟 |
| PC4 | RXD0 | 接收数据0 |
| PC5 | RXD1 | 接收数据1 |
| PB11 | TX_EN | 发送使能 |
| PB12 | TXD0 | 发送数据0 |
| PB13 | TXD1 | 发送数据1 |

#### 6.1.3 初始化代码

```c
void ETH_Init(void) {
    // 使能GPIO和ETH时钟
    __HAL_RCC_ETH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    // 配置RMII引脚
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    
    // PA1: REF_CLK, PA2: MDIO
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PA7: CRS_DV
    GPIO_InitStruct.Pin = GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PC1: MDC, PC4: RXD0, PC5: RXD1
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    
    // PB11: TX_EN, PB12: TXD0, PB13: TXD1
    GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // ETH配置
    heth.Instance = ETH;
    heth.Init.AutoNegotiation = ETH_AUTONEGOTIATION_ENABLE;
    heth.Init.Speed = ETH_SPEED_100M;
    heth.Init.DuplexMode = ETH_MODE_FULLDUPLEX;
    heth.Init.MediaInterface = ETH_MEDIA_INTERFACE_RMII;
    
    HAL_ETH_Init(&heth);
}
```

### 6.2 USB OTG

#### 6.2.1 特性

- **USB 2.0 OTG Full Speed** (12Mbps)
- **Device/Host/OTG** 三种模式
- **1.25KB** 专用SRAM
- **8个端点** (Device模式)
- **HNP/SRP** OTG协议支持

#### 6.2.2 USB引脚

| 引脚 | 功能 | 说明 |
|------|------|------|
| PA11 | USB_DM | 数据负 |
| PA12 | USB_DP | 数据正 |
| PA10 | USB_ID | OTG ID检测 |
| PA9 | USB_VBUS | VBUS检测 |

#### 6.2.3 初始化代码

```c
void USB_OTG_Init(void) {
    // 使能时钟
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    
    // 配置USB引脚
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // PA11: DM, PA12: DP
    GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // 配置USB OTG
    hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
    hpcd_USB_OTG_FS.Init.dev_endpoints = 8;
    hpcd_USB_OTG_FS.Init.speed = USB_OTG_SPEED_FULL;
    hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;
    hpcd_USB_OTG_FS.Init.phy_itface = USB_OTG_EMBEDDED_PHY;
    hpcd_USB_OTG_FS.Init.Sof_enable = ENABLE;
    
    HAL_PCD_Init(&hpcd_USB_OTG_FS);
}
```

### 6.3 CAN接口

#### 6.3.1 特性

- **2个CAN接口** (bxCAN)
- **CAN 2.0B Active** 协议
- **512字节** 专用SRAM
- **3个发送邮箱**，2个FIFO接收
- **时间触发通信** (TTCAN)

#### 6.3.2 CAN引脚

| CAN | TX | RX | 备注 |
|-----|----|----|------|
| CAN1 | PA12 | PA11 | 可与USB共享(不同时) |
| CAN1 | PB9 | PB8 | 重映射1 |
| CAN1 | PD1 | PD0 | 重映射2 |
| CAN2 | PB6 | PB5 | 默认 |
| CAN2 | PB13 | PB12 | 重映射 |

#### 6.3.3 初始化代码

```c
void CAN1_Init(void) {
    // 使能时钟
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    
    // 配置引脚 PA11(RX), PA12(TX)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // CAN配置
    hcan.Instance = CAN1;
    hcan.Init.Prescaler = 9;  // 36MHz / 9 / (1+8+7) = 250kbps
    hcan.Init.Mode = CAN_MODE_NORMAL;
    hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan.Init.TimeSeg1 = CAN_BS1_8TQ;
    hcan.Init.TimeSeg2 = CAN_BS2_7TQ;
    hcan.Init.TimeTriggeredMode = DISABLE;
    hcan.Init.AutoBusOff = ENABLE;
    hcan.Init.AutoWakeUp = DISABLE;
    hcan.Init.AutoRetransmission = ENABLE;
    hcan.Init.ReceiveFifoLocked = DISABLE;
    hcan.Init.TransmitFifoPriority = DISABLE;
    
    HAL_CAN_Init(&hcan);
    
    // 配置过滤器 (接收所有)
    CAN_FilterTypeDef canfilterconfig = {0};
    canfilterconfig.FilterBank = 0;
    canfilterconfig.FilterMode = CAN_FILTERMODE_IDMASK;
    canfilterconfig.FilterScale = CAN_FILTERSCALE_32BIT;
    canfilterconfig.FilterIdHigh = 0x0000;
    canfilterconfig.FilterIdLow = 0x0000;
    canfilterconfig.FilterMaskIdHigh = 0x0000;
    canfilterconfig.FilterMaskIdLow = 0x0000;
    canfilterconfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    canfilterconfig.FilterActivation = ENABLE;
    HAL_CAN_ConfigFilter(&hcan, &canfilterconfig);
}
```

### 6.4 SPI/I2S

#### 6.4.1 SPI特性

- **3个SPI接口** (SPI1, SPI2, SPI3)
- **最大18Mbit/s**
- **全双工/半双工**
- **硬件CRC校验**
- **DMA支持**

**SPI1在APB2 (72MHz)，SPI2/3在APB1 (36MHz)**

#### 6.4.2 SPI引脚

| SPI | MOSI | MISO | SCK | NSS |
|-----|------|------|-----|-----|
| SPI1 | PA7 | PA6 | PA5 | PA4 |
| SPI1 | PB5 | PB4 | PB3 | PA15 (重映射) |
| SPI2 | PB15 | PB14 | PB13 | PB12 |
| SPI3 | PB5 | PB4 | PB3 | PA15 |

#### 6.4.3 SPI初始化代码

```c
void SPI1_Init(void) {
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    
    // 配置引脚
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    
    // SCK (PA5), MOSI (PA7)
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // MISO (PA6)
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // SPI配置
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;  // 72MHz/256=281.25kHz
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    
    HAL_SPI_Init(&hspi1);
}

// SPI收发数据
uint8_t SPI1_TransmitReceive(uint8_t data) {
    uint8_t rx_data;
    HAL_SPI_TransmitReceive(&hspi1, &data, &rx_data, 1, 100);
    return rx_data;
}
```

### 6.5 I2C

#### 6.5.1 特性

- **2个I2C接口** (I2C1, I2C2)
- **标准模式** 100kHz
- **快速模式** 400kHz
- **SMBus/PMBus** 支持
- **7位/10位地址**

#### 6.5.2 I2C引脚

| I2C | SCL | SDA | 备注 |
|-----|-----|-----|------|
| I2C1 | PB6 | PB7 | 默认 |
| I2C1 | PB8 | PB9 | 重映射 |
| I2C2 | PB10 | PB11 | 默认 |

#### 6.5.3 I2C初始化代码

```c
void I2C1_Init(void) {
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // 配置PB6(SCL), PB7(SDA)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;  // 开漏输出
    GPIO_InitStruct.Pull = GPIO_NOPULL;       // 外部上拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // I2C配置
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 400000;  // 400kHz
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    HAL_I2C_Init(&hi2c1);
}

// I2C写寄存器
void I2C_WriteReg(uint8_t dev_addr, uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    HAL_I2C_Master_Transmit(&hi2c1, dev_addr << 1, buf, 2, 100);
}

// I2C读寄存器
uint8_t I2C_ReadReg(uint8_t dev_addr, uint8_t reg) {
    uint8_t data;
    HAL_I2C_Master_Transmit(&hi2c1, dev_addr << 1, &reg, 1, 100);
    HAL_I2C_Master_Receive(&hi2c1, dev_addr << 1, &data, 1, 100);
    return data;
}
```

### 6.6 USART

#### 6.6.1 特性

- **5个USART接口**
- **最大4.5Mbps**
- **同步/异步** 模式
- **LIN, IrDA, 智能卡** 支持
- **硬件流控** (RTS/CTS)

#### 6.6.2 USART引脚

| USART | TX | RX | 备注 |
|-------|----|----|------|
| USART1 | PA9 | PA10 | APB2, 72MHz |
| USART2 | PA2 | PA3 | APB1, 36MHz |
| USART3 | PB10 | PB11 | APB1 |
| UART4 | PC10 | PC11 | APB1 |
| UART5 | PC12 | PD2 | APB1 |

#### 6.6.3 USART初始化代码

```c
void USART1_Init(uint32_t baudrate) {
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    
    // PA9: TX (复用推挽), PA10: RX (浮空输入)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // USART配置
    huart1.Instance = USART1;
    huart1.Init.BaudRate = baudrate;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    
    HAL_UART_Init(&huart1);
}

// 发送字符串
void USART1_SendString(char* str) {
    HAL_UART_Transmit(&huart1, (uint8_t*)str, strlen(str), 100);
}

// 中断接收
void USART1_StartReceive_IT(uint8_t* buf, uint16_t size) {
    HAL_UART_Receive_IT(&huart1, buf, size);
}
```

---

## 7. 定时器

### 7.1 定时器分类

| 定时器 | 类型 | 特性 | 时钟源 |
|--------|------|------|--------|
| TIM1 | 高级 | 电机控制，死区，刹车 | APB2 (72MHz) |
| TIM2 | 通用 | 32位 | APB1 (36MHz) |
| TIM3,4 | 通用 | 16位 | APB1 |
| TIM5 | 通用 | 16位 | APB1 |
| TIM6,7 | 基本 | DAC触发 | APB1 |
| TIM15-17 | 通用 | 16位 (部分芯片) | APB2 |

### 7.2 电机控制PWM (TIM1)

#### 7.2.1 特性

- **3相PWM** 输出 (6通道)
- **死区时间** 可编程
- **紧急刹车** 输入
- **互补输出** 带死区

#### 7.2.2 引脚

| 功能 | 正向输出 | 互补输出 |
|------|----------|----------|
| CH1 | PA8 | PB13 |
| CH2 | PA9 | PB14 |
| CH3 | PA10 | PB15 |
| CH4 | PA11 | - |
| 刹车输入 | PB12 (BKIN) | - |
| ETR | PA12 | - |

#### 7.2.3 三相PWM代码

```c
void TIM1_PWM_Init(void) {
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // 配置PWM引脚
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    
    // PA8: CH1, PA9: CH2, PA10: CH3
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PB13: CH1N, PB14: CH2N, PB15: CH3N (互补输出)
    GPIO_InitStruct.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // 刹车输入
    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // TIM1配置
    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;  // 不分频
    htim1.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
    htim1.Init.Period = 7199;  // 72MHz / (7199+1) = 10kHz PWM
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    HAL_TIM_PWM_Init(&htim1);
    
    // PWM配置
    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 3600;  // 50% 占空比
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3);
    
    // 死区配置
    TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
    sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
    sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
    sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
    sBreakDeadTimeConfig.DeadTime = 100;  // 约1.4μs死区 (100/72MHz)
    sBreakDeadTimeConfig.BreakState = TIM_BREAK_ENABLE;
    sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_LOW;
    sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
    HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig);
    
    // 启动PWM
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    
    // 启动互补输出
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
}
```

---

## 8. ADC与DAC

### 8.1 ADC特性

- **2个ADC** (ADC1, ADC2)
- **12位分辨率**
- **1μs转换时间**
- **16个外部通道** + 2个内部通道
- **双ADC模式** (同时/交错采样)
- **最高2MSPS** (交错模式)

### 8.2 ADC通道

| 通道 | 引脚 | 说明 |
|------|------|------|
| ADC12_IN0 | PA0 | 外部输入 |
| ADC12_IN1 | PA1 | 外部输入 |
| ADC12_IN2 | PA2 | 外部输入 |
| ADC12_IN3 | PA3 | 外部输入 |
| ADC12_IN4 | PA4 | 外部输入 |
| ADC12_IN5 | PA5 | 外部输入 |
| ADC12_IN6 | PA6 | 外部输入 |
| ADC12_IN7 | PA7 | 外部输入 |
| ADC12_IN8 | PB0 | 外部输入 |
| ADC12_IN9 | PB1 | 外部输入 |
| ADC12_IN10 | PC0 | 外部输入 |
| ADC12_IN11 | PC1 | 外部输入 |
| ADC12_IN12 | PC2 | 外部输入 |
| ADC12_IN13 | PC3 | 外部输入 |
| ADC12_IN14 | PC4 | 外部输入 |
| ADC12_IN15 | PC5 | 外部输入 |
| ADC12_IN16 | - | 温度传感器 |
| ADC12_IN17 | - | 内部参考电压 |

### 8.3 ADC初始化代码

```c
void ADC1_Init(void) {
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    // 配置ADC引脚 (PA0, PC0)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    
    // ADC配置
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 2;  // 2个通道
    HAL_ADC_Init(&hadc1);
    
    // 配置通道
    ADC_ChannelConfTypeDef sConfig = {0};
    
    // 通道0 (PA0)
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_13CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    // 通道10 (PC0)
    sConfig.Channel = ADC_CHANNEL_10;
    sConfig.Rank = ADC_REGULAR_RANK_2;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    // 校准
    HAL_ADCEx_Calibration_Start(&hadc1);
}

uint16_t ADC_ReadChannel(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_13CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 100);
    uint16_t value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    
    return value;
}

// 读取芯片温度
float ADC_ReadTemperature(void) {
    // 内部温度传感器在ADC1_IN16
    uint16_t raw = ADC_ReadChannel(ADC_CHANNEL_TEMPSENSOR);
    
    // 温度计算公式 (参考手册)
    // V25 = 1.43V, Avg_Slope = 4.3mV/°C
    float voltage = raw * 3.3f / 4096.0f;
    float temperature = (voltage - 1.43f) / 0.0043f + 25.0f;
    
    return temperature;
}
```

### 8.4 DAC特性

- **2个DAC通道** (DAC_OUT1, DAC_OUT2)
- **12位分辨率**
- **输出缓冲** 可配置
- **噪声波形** 生成
- **DMA支持**

### 8.5 DAC引脚

| DAC | 引脚 | 说明 |
|-----|------|------|
| DAC_OUT1 | PA4 | 通道1输出 |
| DAC_OUT2 | PA5 | 通道2输出 |

### 8.6 DAC初始化代码

```c
void DAC_Init(void) {
    __HAL_RCC_DAC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    
    // 配置PA4, PA5为模拟输入
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // DAC配置
    DAC_ChannelConfTypeDef sConfig = {0};
    
    sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_1);
    HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_2);
    
    // 启动DAC
    HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
    HAL_DAC_Start(&hdac, DAC_CHANNEL_2);
}

// 设置DAC输出电压 (0-3.3V)
void DAC_SetVoltage(uint32_t channel, float voltage) {
    uint32_t value = (uint32_t)((voltage / 3.3f) * 4095.0f);
    HAL_DAC_SetValue(&hdac, channel, DAC_ALIGN_12B_R, value);
}
```

---

## 9. DMA控制器

### 9.1 DMA特性

- **12个独立通道**
- **7个请求源** 每个通道
- **12位计数器**
- **循环模式** 支持
- **优先级管理** (4级)

### 9.2 DMA通道分配

| 通道 | ADC | USART | SPI | I2C | TIM |
|------|-----|-------|-----|-----|-----|
| DMA1_CH1 | - | - | - | - | TIM2_UP |
| DMA1_CH2 | - | USART3_TX | SPI1_RX | - | TIM3_UP |
| DMA1_CH3 | - | USART3_RX | SPI1_TX | - | TIM3_CH4 |
| DMA1_CH4 | - | USART1_TX | SPI2_RX | I2C2_TX | TIM4_CH1 |
| DMA1_CH5 | - | USART1_RX | SPI2_TX | I2C2_RX | TIM4_CH2 |
| DMA1_CH6 | - | USART2_RX | - | I2C1_TX | TIM4_CH3 |
| DMA1_CH7 | - | USART2_TX | - | I2C1_RX | TIM4_UP |

### 9.3 ADC + DMA代码

```c
#define ADC_BUFFER_SIZE 16
uint16_t adc_buffer[ADC_BUFFER_SIZE];

void ADC_DMA_Init(void) {
    __HAL_RCC_DMA1_CLK_ENABLE();
    
    // ADC配置
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 2;
    HAL_ADC_Init(&hadc1);
    
    // 配置2个通道
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_13CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = ADC_REGULAR_RANK_2;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    // DMA配置
    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_adc1);
    
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
    
    // 启动ADC DMA
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buffer, ADC_BUFFER_SIZE);
}

// DMA传输完成回调
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    // 处理采集完成的数据
    // adc_buffer中已填满数据
}
```

---

## 10. 中断系统

### 10.1 NVIC特性

- **81个可屏蔽中断**
- **16个优先级** (4位优先级)
- **抢占优先级** 和 **响应优先级**
- **尾链优化** 减少中断延迟

### 10.2 优先级分组

| 分组 | 抢占优先级位 | 响应优先级位 |
|------|-------------|--------------|
| NVIC_PRIORITYGROUP_0 | 0位 | 4位 |
| NVIC_PRIORITYGROUP_1 | 1位 | 3位 |
| NVIC_PRIORITYGROUP_2 | 2位 | 2位 |
| NVIC_PRIORITYGROUP_3 | 3位 | 1位 |
| NVIC_PRIORITYGROUP_4 | 4位 | 0位 |

### 10.3 中断配置代码

```c
void NVIC_Config(void) {
    // 设置优先级分组2 (2位抢占, 2位响应)
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_2);
    
    // 配置USART1中断
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);  // 抢占优先级1, 响应0
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    
    // 配置TIM2中断
    HAL_NVIC_SetPriority(TIM2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    
    // 配置EXTI0中断 (按键)
    HAL_NVIC_SetPriority(EXTI0_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

// USART1中断服务程序
void USART1_IRQHandler(void) {
    HAL_UART_IRQHandler(&huart1);
}

// TIM2中断服务程序
void TIM2_IRQHandler(void) {
    HAL_TIM_IRQHandler(&htim2);
}
```

---

## 11. 代码示例

### 11.1 完整系统初始化

```c
#include "stm32f1xx_hal.h"

void System_Init(void) {
    // HAL初始化
    HAL_Init();
    
    // 系统时钟配置 (72MHz)
    SystemClock_Config();
    
    // 外设初始化
    GPIO_Init();
    USART1_Init(115200);
    TIM2_Init();
    ADC1_Init();
    I2C1_Init();
    
    // NVIC配置
    NVIC_Config();
}

int main(void) {
    System_Init();
    
    printf("STM32F107VC Started\r\n");
    
    while (1) {
        // 主循环
        HAL_Delay(1000);
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);  // LED闪烁
        
        // 读取ADC
        uint16_t adc_val = ADC_ReadChannel(ADC_CHANNEL_0);
        printf("ADC: %d\r\n", adc_val);
        
        // 发送CAN消息 (如果有CAN)
        // CAN_Transmit(...);
    }
}
```

### 11.2 网络通信示例 (Ethernet + LwIP)

```c
// 基于LwIP的TCP服务器示例
void net_init(void) {
    // ETH初始化
    ETH_Init();
    
    // LwIP初始化
    lwip_init();
    
    // 配置IP地址
    IP4_ADDR(&ipaddr, 192, 168, 1, 10);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 1, 1);
    netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &tcpip_input);
    netif_set_default(&gnetif);
    netif_set_up(&gnetif);
}
```

---

## 附录

### A. 时钟计算参考

| 目标时钟 | HSE=8MHz配置 | 备注 |
|----------|--------------|------|
| 72MHz | PLL=HSE×9 | SYSCLK=72MHz |
| 48MHz | PLL=HSE×6 | USB需要48MHz |
| 36MHz | PLL=HSE×4.5 | 或SYSCLK/2 |

### B. 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 程序不运行 | Boot配置错误 | 检查BOOT0/BOOT1引脚 |
| ADC读数不准 | 未校准 | 调用HAL_ADCEx_Calibration_Start |
| USB不识别 | 时钟不对 | 确保USB时钟为48MHz |
| CAN通信失败 | 波特率不匹配 | 检查Prescaler和时间段配置 |
| ETH不通 | PHY未复位 | 检查复位引脚和初始化顺序 |

---

*本文档基于STM32F105xx/107xx Reference Manual整理*
*整理日期: 2026年3月*
