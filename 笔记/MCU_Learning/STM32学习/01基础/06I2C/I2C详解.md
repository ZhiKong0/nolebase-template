# I2C 详解（内部集成电路总线）

> I2C（Inter-Integrated Circuit）是 STM32 中最常用的同步串行通信协议之一，采用两根线（SDA数据线 + SCL时钟线）实现多主多从通信，广泛应用于传感器、EEPROM、OLED 等外设连接。

---

![Pasted image 20260308100000](https://raw.githubusercontent.com/ZhiKong0/Image_Auto/main/Obsidian/Pasted%20image%2020260308100000.png)

## 一、I2C 基本概念

### 1.1 什么是 I2C？

**I2C** 全称 **Inter-Integrated Circuit**（内部集成电路总线），是由 Philips（现 NXP）开发的**同步半双工串行通信协议**。

**核心特点**：

- **两根信号线**：SDA（数据线）+ SCL（时钟线）
- **多主多从**：支持一主多从或多主模式
- **地址寻址**：通过 7 位或 10 位地址识别从设备
- **半双工**：同一时间只能单向传输
- **同步通信**：时钟由主设备产生，从设备同步
- **开漏输出**：支持线与功能，实现多主仲裁

### 1.2 I2C 的核心特性

| 特性 | 说明 |
|------|------|
| **信号线** | 2 根：SDA（数据线）、SCL（时钟线） |
| **通信模式** | 同步、半双工 |
| **寻址方式** | 7 位地址（128 个设备）或 10 位地址（1024 个设备） |
| **传输速率** | 标准模式 100Kbps、快速模式 400Kbps、高速模式 3.4Mbps |
| **电平标准** | TTL/CMOS 电平，通常 3.3V 或 5V |
| **上拉电阻** | SDA 和 SCL 必须外接上拉电阻（通常 4.7KΩ~10KΩ） |
| **位置** | **芯片外设**，STM32F1 有 2 个 I2C 接口（I2C1、I2C2） |

> **注意**：I2C 是芯片外设，与 USART、SPI 并列属于串行通信接口。STM32F103 支持 2 个 I2C 接口。

---

## 二、I2C 物理层详解

### 2.1 I2C 总线结构

```
┌─────────────────────────────────────────────────────────────┐
│                      I2C 总线拓扑结构                          │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│         VCC (3.3V/5V)                                       │
│           │                                                  │
│           ├───[4.7KΩ]───┬──────────┬──────────┬─── SDA      │
│           │             │          │          │              │
│           ├───[4.7KΩ]───┼──────────┼──────────┼─── SCL      │
│           │             │          │          │              │
│           │          ┌────┐    ┌────┐    ┌────┐            │
│           │          │Master│    │Slave1│    │Slave2│        │
│           │          │ 主机  │    │ 从机1│    │ 从机2│        │
│           │          └────┘    └────┘    └────┘            │
│           │            │          │          │              │
│          GND          GND        GND        GND             │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 开漏输出与线与功能

| 特性 | 说明 |
|------|------|
| **开漏输出** | I2C 引脚只能输出低电平或高阻态，不能主动输出高电平 |
| **上拉电阻** | 高电平由上拉电阻提供，实现电平转换兼容 |
| **线与功能** | 多个设备可同时拉低总线，实现仲裁和时钟同步 |

### 2.3 上拉电阻选择

| 总线电容 | 推荐上拉电阻 | 适用场景 |
|:--------:|:------------:|:---------|
| < 100pF | 10KΩ | 短距离、低速 |
| 100~200pF | 4.7KΩ | 常规应用 |
| 200~400pF | 2.2KΩ | 长距离、高速 |

---

## 三、I2C 协议层详解

### 3.1 数据帧格式

```
┌─────────────────────────────────────────────────────────────────┐
│                    I2C 完整数据帧格式                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Start  │  地址(7bit)  │ R/W │ ACK │  数据(8bit)  │ ACK │ ... │ Stop │
│   (S)   │   (A6~A0)    │ (1) │ (1) │   (D7~D0)   │ (1) │     │  (P) │
│                                                                  │
│  ┌─┐    ┌──┬──┬──┬──┬──┬──┬──┐ ┌─┐ ┌─┐ ┌──┬──┬──┬──┬──┬──┬──┐ ┌─┐ ┌─┐
│  │S│    │A6│A5│A4│A3│A2│A1│A0│ │R│ │A│ │D7│D6│D5│D4│D3│D2│D1│ │A│ │P│
│  └─┘    └──┴──┴──┴──┴──┴──┴──┘ └─┘ └─┘ └──┴──┴──┴──┴──┴──┴──┴──┘ └─┘ └─┘
│                                                                  │
│  S: 起始信号（SDA下降沿，SCL高电平）                              │
│  P: 停止信号（SDA上升沿，SCL高电平）                              │
│  ACK: 应答位（低电平有效）                                        │
│  NACK: 非应答（高电平）                                           │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 通信时序详解

| 时序 | 描述 | 图示 |
|------|------|------|
| **起始信号 (S)** | SCL=1 时，SDA 从高→低跳变 | `SCL: ─┐  ┌──┐  ┌─` |
| | | `SDA: ─┘  └──┘  │` |
| **停止信号 (P)** | SCL=1 时，SDA 从低→高跳变 | `SCL: ─┐  ┌──┐  ┌─` |
| | | `SDA: ─┘  └──┘  ┘` |
| **数据有效** | SCL=1 时，SDA 必须稳定 | `SCL: ─┐  ┌──┐  ┌─` |
| | | `SDA: ─┘──────┘` |
| **数据变化** | SCL=0 时，SDA 可以变化 | `SCL: ─┐  ┌──┐  ┌─` |
| | | `SDA: ─┘──┐──┘` |

### 3.3 应答机制（ACK/NACK）

| 应答类型 | 电平 | 含义 | 场景 |
|----------|:----:|------|------|
| **ACK** | 低电平 | 接收方正确接收 | 从机收到数据/地址 |
| **NACK** | 高电平 | 接收方未接收 | 从机不存在/数据错误/接收完毕 |

### 3.4 完整通信流程

```
主机写入数据到从机：

┌────────┐  Start   ┌─────────┐  Addr+W   ┌─────────┐  ACK   ┌─────────┐
│ 主机   │ ────────► │ 从机    │ ◄─────────│ 主机    │ ──────►│ 从机    │
│        │           │ (监听)  │           │发送地址  │        │ 应答    │
└────────┘           └─────────┘           └─────────┘        └─────────┘
     │                                          │                    │
     │                   ┌─────────┐  Data       │                    │
     │                   │ 主机   │ ◄───────────┘                    │
     │                   │        │ 发送数据                          │
     │                   └─────────┘                                    │
     │                        │                    ┌─────────┐  ACK   │
     │                        └───────────────────│ 从机    │ ───────┘
     │                                            │ 应答    │
     │                                            └─────────┘
     │  (重复发送数据+应答，直到发送完毕)
     │
     │  Stop
     ▼
┌─────────┐
│ 从机    │
│ (结束)  │
└─────────┘
```

### 3.5 7 位地址格式

```
┌─────────────────────────────────────────────────┐
│              7 位地址 + 读写位格式                  │
├─────────────────────────────────────────────────┤
│                                                  │
│  Bit7  Bit6  Bit5  Bit4  Bit3  Bit2  Bit1  Bit0  │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┐              │
│  │A6 │A5 │A4 │A3 │A2 │A1 │A0 │R/W│              │
│  └───┴───┴───┴───┴───┴───┴───┴───┘              │
│                                                  │
│  地址位 (7bit)        读写位 (1bit)              │
│  A6~A0: 从机地址      0=写 (主机→从机)          │
│  共128个地址          1=读 (从机→主机)            │
│                                                  │
│  示例：                                            │
│  0x78 = 0b01111000 → 地址0x3C, 写操作             │
│  0x79 = 0b01111001 → 地址0x3C, 读操作             │
│                                                  │
└─────────────────────────────────────────────────┘
```

---

## 四、I2C 读写操作详解

### 4.1 主机发送（写操作）

```c
/**
 * @brief  I2C 主机发送数据流程
 * 
 *  流程：
 *  1. 发送起始信号 (Start)
 *  2. 发送从机地址 + 写位 (Addr + W)
 *  3. 等待从机应答 (ACK)
 *  4. 发送数据字节 (Data[0])
 *  5. 等待从机应答 (ACK)
 *  6. 重复 4~5 直到数据发送完毕
 *  7. 发送停止信号 (Stop)
 */

// 时序图示：
// S: __/‾‾‾‾\____/‾‾‾‾\____/‾‾‾‾\____/‾‾‾‾\____
//    S   Addr+W  ACK   Data0   ACK   Data1   ACK   ...   Stop
//
// D: ‾‾‾\\_____////_____\\_____////_____\\_____////___
//       0111...0  A  1111...1  A  0000...0  A  ...  P
```

### 4.2 主机接收（读操作）

```c
/**
 * @brief  I2C 主机接收数据流程
 * 
 *  流程：
 *  1. 发送起始信号 (Start)
 *  2. 发送从机地址 + 读位 (Addr + R)
 *  3. 等待从机应答 (ACK)
 *  4. 接收数据字节 (Data[0])
 *  5. 主机发送应答 (ACK) 继续接收，或 (NACK) 结束接收
 *  6. 重复 4~5 直到接收完毕
 *  7. 发送停止信号 (Stop)
 */

// 时序图示：
// S: __/‾‾‾‾\____/‾‾‾‾\____/‾‾‾‾\____/‾‾‾‾\____
//    S   Addr+R  ACK   Data0   ACK   Data1   NACK  Stop
//
// D: ‾‾‾\\_____////_____\\_____////_________//___
//       0111...1  A  1111...1  A  0000...0  N  P
```

### 4.3 组合格式（先写后读）

```c
/**
 * @brief  I2C 组合格式（常用于读取寄存器）
 * 
 *  应用场景：读取 EEPROM、传感器寄存器
 *  流程：
 *  1. 发送起始信号
 *  2. 发送从机地址 + 写位
 *  3. 发送寄存器地址（要读取的寄存器）
 *  4. 发送重复起始信号 (Repeated Start)
 *  5. 发送从机地址 + 读位
 *  6. 接收数据
 *  7. 发送停止信号
 */

// 时序图示：
// S: __/‾‾‾‾\____/‾‾‾‾\____/‾‾‾‾\__/‾‾‾‾\____/‾‾‾‾\____
//    S  Addr+W  ACK  RegAddr  ACK  Sr Addr+R  ACK  Data  NACK  Stop
//
// Sr = Repeated Start (重复起始)
```

---

## 五、STM32 I2C 外设详解

### 5.1 STM32F1 I2C 特性

| 特性 | 说明 |
|------|------|
| **I2C 数量** | 2 个独立接口：I2C1、I2C2 |
| **工作模式** | 主机发送、主机接收、从机发送、从机接收 |
| **地址模式** | 7 位地址、10 位地址 |
| **传输速度** | 标准模式 100KHz、快速模式 400KHz |
| **时钟源** | APB1 总线时钟（36MHz），经分频得到 I2C 时钟 |
| **中断支持** | 事件中断、错误中断、缓冲区中断 |
| **DMA 支持** | 支持 DMA 传输 |

### 5.2 I2C 时钟配置

```
┌─────────────────────────────────────────────────────────────┐
│                  I2C 时钟分频计算                             │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  APB1 时钟 = 36MHz                                           │
│         │                                                    │
│         ▼                                                    │
│  ┌─────────────┐                                             │
│  │  分频系数    │  (2~50, 或 4~62 取决于模式)                  │
│  │   (CCR)     │                                             │
│  └──────┬──────┘                                             │
│         │                                                    │
│         ▼                                                    │
│  I2C 时钟 = APB1 / (CCR * 2)  或  APB1 / CCR                 │
│                                                              │
│  示例：100KHz 标准模式                                        │
│  CCR = 36MHz / (100KHz * 2) = 180 = 0xB4                     │
│                                                              │
│  示例：400KHz 快速模式                                        │
│  CCR = 36MHz / (400KHz * 3) = 30 = 0x1E  (Duty=2:1)          │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 5.3 I2C 初始化结构体

```c
typedef struct
{
    uint32_t I2C_ClockSpeed;          // I2C 时钟频率 (Hz)，如 100000
    uint16_t I2C_Mode;                // 模式：I2C_Mode_I2C（I2C模式）
    uint16_t I2C_DutyCycle;           // 快速模式占空比（2:1 或 16:9）
    uint16_t I2C_OwnAddress1;         // 本机地址（从机模式使用）
    uint16_t I2C_Ack;                 // 应答使能：I2C_Ack_Enable
    uint16_t I2C_AcknowledgedAddress; // 地址长度：7位或10位
} I2C_InitTypeDef;
```

---

## 六、I2C 配置和使用

### 6.1 完整配置流程

```c
#include "stm32f10x.h"

/**
 * @brief  I2C1 初始化配置示例（主机模式，100KHz）
 * @note   配置 PB6(SCL) 和 PB7(SDA) 为 I2C1
 */
void I2C1_Configuration(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    I2C_InitTypeDef I2C_InitStruct;
    
    // ==================== 第1步：使能外设时钟 ====================
    // 使能 GPIOB 时钟（I2C1 使用 PB6、PB7）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    // 使能 I2C1 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    
    // ==================== 第2步：配置 GPIO ====================
    // PB6: I2C1_SCL, PB7: I2C1_SDA
    // 必须配置为开漏输出 + 复用功能
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_OD;  // 复用开漏输出
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // ==================== 第3步：配置 I2C 参数 ====================
    I2C_InitStruct.I2C_ClockSpeed = 100000;           // 100KHz 标准模式
    I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;          // I2C 模式
    I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;   // 快速模式占空比 2:1
    I2C_InitStruct.I2C_OwnAddress1 = 0x00;            // 主机模式，从机地址无效
    I2C_InitStruct.I2C_Ack = I2C_Ack_Enable;          // 使能应答
    I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit; // 7位地址
    
    // 初始化 I2C1
    I2C_Init(I2C1, &I2C_InitStruct);
    
    // ==================== 第4步：使能 I2C ====================
    I2C_Cmd(I2C1, ENABLE);
}
```

### 6.2 常用 I2C 函数

| 函数 | 功能 |
|------|------|
| `I2C_Init(I2Cx, &initStruct)` | 初始化 I2C |
| `I2C_Cmd(I2Cx, ENABLE)` | 使能 I2C |
| `I2C_GenerateSTART(I2Cx, ENABLE)` | 产生起始信号 |
| `I2C_GenerateSTOP(I2Cx, ENABLE)` | 产生停止信号 |
| `I2C_Send7bitAddress(I2Cx, addr, direction)` | 发送 7 位地址 |
| `I2C_SendData(I2Cx, data)` | 发送数据 |
| `I2C_ReceiveData(I2Cx)` | 接收数据 |
| `I2C_GetFlagStatus(I2Cx, flag)` | 获取标志位状态 |
| `I2C_CheckEvent(I2Cx, event)` | 检查事件状态 |

### 6.3 等待事件宏

```c
// 常用事件检查宏
#define EV5    ((uint32_t)0x00030001)  // 起始信号已发送 (SB=1, MSL=1, BUSY=1)
#define EV6    ((uint32_t)0x00030082)  // 地址已发送且应答 (ADDR=1, MSL=1, BUSY=1, TXE=1)
#define EV7    ((uint32_t)0x00030040)  // 数据接收就绪 (RXNE=1, BUSY=1, MSL=1)
#define EV8    ((uint32_t)0x00030084)  // 数据发送就绪 (TXE=1, BTF=0, MSL=1, BUSY=1)
#define EV8_2  ((uint32_t)0x00030084)  // 数据传输完成 (BTF=1, MSL=1, BUSY=1)
```

---

## 七、I2C 读写函数实现

### 7.1 主机发送函数（字节写）

```c
/**
 * @brief  I2C 向从机发送数据（阻塞式）
 * @param  I2Cx: I2C 接口（I2C1 或 I2C2）
 * @param  slaveAddr: 从机地址（7位，左对齐，如 0xD0 表示 0x68<<1）
 * @param  pData: 要发送的数据缓冲区
 * @param  len: 数据长度
 * @retval 0: 成功，1: 失败
 */
uint8_t I2C_MasterSend(I2C_TypeDef* I2Cx, uint8_t slaveAddr, uint8_t* pData, uint16_t len)
{
    // 1. 等待总线空闲
    while (I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSY));
    
    // 2. 发送起始信号
    I2C_GenerateSTART(I2Cx, ENABLE);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_MODE_SELECT));  // 等待 EV5
    
    // 3. 发送从机地址 + 写位
    I2C_Send7bitAddress(I2Cx, slaveAddr, I2C_Direction_Transmitter);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));  // 等待 EV6
    
    // 4. 发送数据
    for (uint16_t i = 0; i < len; i++)
    {
        I2C_SendData(I2Cx, pData[i]);
        while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_TRANSMITTED));  // 等待 EV8
    }
    
    // 5. 发送停止信号
    I2C_GenerateSTOP(I2Cx, ENABLE);
    
    return 0;
}
```

### 7.2 主机接收函数（字节读）

```c
/**
 * @brief  I2C 从从机接收数据（阻塞式）
 * @param  I2Cx: I2C 接口
 * @param  slaveAddr: 从机地址（7位，左对齐）
 * @param  pData: 接收数据缓冲区
 * @param  len: 要接收的数据长度
 * @retval 0: 成功，1: 失败
 */
uint8_t I2C_MasterReceive(I2C_TypeDef* I2Cx, uint8_t slaveAddr, uint8_t* pData, uint16_t len)
{
    // 1. 等待总线空闲
    while (I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSY));
    
    // 2. 发送起始信号
    I2C_GenerateSTART(I2Cx, ENABLE);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_MODE_SELECT));  // 等待 EV5
    
    // 3. 发送从机地址 + 读位
    I2C_Send7bitAddress(I2Cx, slaveAddr, I2C_Direction_Receiver);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED));  // 等待 EV6
    
    // 4. 接收数据
    for (uint16_t i = 0; i < len; i++)
    {
        // 如果是最后一个字节，发送 NACK
        if (i == len - 1)
        {
            I2C_AcknowledgeConfig(I2Cx, DISABLE);  // 禁用应答（发送NACK）
            I2C_GenerateSTOP(I2Cx, ENABLE);         // 提前产生停止信号
        }
        
        while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_RECEIVED));  // 等待 EV7
        pData[i] = I2C_ReceiveData(I2Cx);
    }
    
    // 5. 重新使能应答（为下次通信做准备）
    I2C_AcknowledgeConfig(I2Cx, ENABLE);
    
    return 0;
}
```

### 7.3 寄存器读写函数（组合格式）

```c
/**
 * @brief  I2C 向从机寄存器写入数据
 * @param  I2Cx: I2C 接口
 * @param  slaveAddr: 从机地址
 * @param  regAddr: 寄存器地址
 * @param  pData: 数据缓冲区
 * @param  len: 数据长度
 */
uint8_t I2C_WriteReg(I2C_TypeDef* I2Cx, uint8_t slaveAddr, uint8_t regAddr, uint8_t* pData, uint16_t len)
{
    while (I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSY));
    
    // 起始信号
    I2C_GenerateSTART(I2Cx, ENABLE);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_MODE_SELECT));
    
    // 发送地址 + 写
    I2C_Send7bitAddress(I2Cx, slaveAddr, I2C_Direction_Transmitter);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));
    
    // 发送寄存器地址
    I2C_SendData(I2Cx, regAddr);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_TRANSMITTED));
    
    // 发送数据
    for (uint16_t i = 0; i < len; i++)
    {
        I2C_SendData(I2Cx, pData[i]);
        while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_TRANSMITTED));
    }
    
    // 停止信号
    I2C_GenerateSTOP(I2Cx, ENABLE);
    
    return 0;
}

/**
 * @brief  I2C 从从机寄存器读取数据（组合格式）
 */
uint8_t I2C_ReadReg(I2C_TypeDef* I2Cx, uint8_t slaveAddr, uint8_t regAddr, uint8_t* pData, uint16_t len)
{
    // ===== 第一阶段：发送寄存器地址 =====
    while (I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSY));
    
    I2C_GenerateSTART(I2Cx, ENABLE);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_MODE_SELECT));
    
    I2C_Send7bitAddress(I2Cx, slaveAddr, I2C_Direction_Transmitter);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));
    
    I2C_SendData(I2Cx, regAddr);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_TRANSMITTED));
    
    // ===== 第二阶段：重复起始 + 读取数据 =====
    I2C_GenerateSTART(I2Cx, ENABLE);  // 重复起始
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_MODE_SELECT));
    
    I2C_Send7bitAddress(I2Cx, slaveAddr, I2C_Direction_Receiver);
    while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED));
    
    // 接收数据
    for (uint16_t i = 0; i < len; i++)
    {
        if (i == len - 1)
        {
            I2C_AcknowledgeConfig(I2Cx, DISABLE);
            I2C_GenerateSTOP(I2Cx, ENABLE);
        }
        
        while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_RECEIVED));
        pData[i] = I2C_ReceiveData(I2Cx);
    }
    
    I2C_AcknowledgeConfig(I2Cx, ENABLE);
    
    return 0;
}
```

---

## 八、常见错误与注意事项

### 8.1 典型错误

| 问题 | 原因 | 解决 |
|------|------|------|
| **通信失败，总线一直忙** | 上拉电阻未接或 SDA/SCL 短路 | 检查硬件连接，确保 4.7KΩ 上拉电阻 |
| **从机无应答** | 从机地址错误、从机未初始化、线路断开 | 确认 7 位地址左对齐，检查从机供电 |
| **数据接收错误** | 时钟速度过快、线路干扰 | 降低时钟频率到 100KHz，缩短线路 |
| **卡死在等待标志** | 未清除标志、中断未处理 | 确保正确检查事件标志 |
| **只能发送第一个字节** | 未等待 BTF 标志 | 使用 `I2C_EVENT_MASTER_BYTE_TRANSMITTED` |
| **接收数据少一个字节** | NACK 和 STOP 顺序错误 | 最后一个字节前发送 NACK 并提前产生 STOP |

### 8.2 最佳实践

1. **必须接上拉电阻**：SDA 和 SCL 必须外接 4.7KΩ~10KΩ 上拉电阻
2. **地址左对齐**：7 位地址需要左移 1 位（如 0x68 → 0xD0）
3. **检查 BUSY 标志**：通信前确保总线空闲
4. **正确等待事件**：不要只检查标志位，使用 `I2C_CheckEvent()` 检查完整事件
5. **最后字节特殊处理**：接收最后一个字节前禁用 ACK 并提前产生 STOP
6. **恢复 ACK 设置**：接收完成后重新使能 ACK
7. **超时处理**：实际项目中添加超时机制，防止死等

---

## 九、完整工程示例

### 9.1 工程概述

本示例演示：
- **I2C1**: 配置为 100KHz 主机模式（PB6=SCL, PB7=SDA）
- **设备**: 连接 AT24C02 EEPROM（地址 0xA0）
- **功能**: 向 EEPROM 写入数据，然后读取并验证

### 9.2 完整 main.c 代码

```c
/**
 * @file    main.c
 * @brief   I2C 读写 EEPROM 完整示例
 * @author  Your Name
 * @version V1.0
 * @date    2026-03-08
 */

#include "stm32f10x.h"
#include <stdio.h>

// 宏定义
#define EEPROM_ADDR     0xA0    // AT24C02 地址 (0x50 << 1)
#define BUFFER_SIZE     8

// 全局变量
uint8_t g_TxBuffer[BUFFER_SIZE] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
uint8_t g_RxBuffer[BUFFER_SIZE];

// 函数声明
void RCC_Configuration(void);
void GPIO_Configuration(void);
void I2C1_Configuration(void);
uint8_t I2C_WriteBuffer(uint8_t slaveAddr, uint8_t memAddr, uint8_t* pData, uint16_t len);
uint8_t I2C_ReadBuffer(uint8_t slaveAddr, uint8_t memAddr, uint8_t* pData, uint16_t len);
void Delay_ms(uint32_t ms);

int main(void)
{
    // 初始化
    RCC_Configuration();
    GPIO_Configuration();
    I2C1_Configuration();
    
    printf("\r
========== I2C EEPROM Demo ==========\r\n");
    printf("EEPROM: AT24C02 (Addr: 0x%02X)\r\n", EEPROM_ADDR);
    printf("I2C1: PB6=SCL, PB7=SDA, 100KHz\r\n");
    printf("=====================================\r\n\r\n");
    
    // 写入数据
    printf("Writing data to EEPROM...\r\n");
    if (I2C_WriteBuffer(EEPROM_ADDR, 0x00, g_TxBuffer, BUFFER_SIZE) == 0)
    {
        printf("Write success!\r\n");
    }
    else
    {
        printf("Write failed!\r\n");
    }
    
    // EEPROM 写入需要延时等待内部写入完成
    Delay_ms(10);
    
    // 读取数据
    printf("Reading data from EEPROM...\r\n");
    if (I2C_ReadBuffer(EEPROM_ADDR, 0x00, g_RxBuffer, BUFFER_SIZE) == 0)
    {
        printf("Read success!\r\n");
    }
    else
    {
        printf("Read failed!\r\n");
    }
    
    // 验证数据
    printf("Verifying data...\r\n");
    uint8_t match = 1;
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        if (g_TxBuffer[i] != g_RxBuffer[i])
        {
            match = 0;
            printf("Mismatch at [%d]: TX=0x%02X, RX=0x%02X\r\n", 
                   i, g_TxBuffer[i], g_RxBuffer[i]);
        }
    }
    
    if (match)
    {
        printf("Data verification PASSED!\r\n");
    }
    else
    {
        printf("Data verification FAILED!\r\n");
    }
    
    // 打印数据
    printf("\r\nData dump:\r\n");
    printf("TX: ");
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        printf("0x%02X ", g_TxBuffer[i]);
    }
    printf("\r\nRX: ");
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        printf("0x%02X ", g_RxBuffer[i]);
    }
    printf("\r\n");
    
    while (1);
}

void RCC_Configuration(void)
{
    // 系统时钟 72MHz
    RCC_HSEConfig(RCC_HSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET);
    
    FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
    FLASH_SetLatency(FLASH_Latency_2);
    
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div2);  // APB1 = 36MHz
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
    
    // 使能 GPIOB 和 AFIO 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    
    // PB6: I2C1_SCL, PB7: I2C1_SDA（复用开漏）
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void I2C1_Configuration(void)
{
    I2C_InitTypeDef I2C_InitStruct;
    
    // 使能 I2C1 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    
    // I2C 配置
    I2C_InitStruct.I2C_ClockSpeed = 100000;           // 100KHz
    I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStruct.I2C_OwnAddress1 = 0x00;
    I2C_InitStruct.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    
    I2C_Init(I2C1, &I2C_InitStruct);
    I2C_Cmd(I2C1, ENABLE);
}

/**
 * @brief  向 EEPROM 写入数据（页写入）
 */
uint8_t I2C_WriteBuffer(uint8_t slaveAddr, uint8_t memAddr, uint8_t* pData, uint16_t len)
{
    // 检查总线空闲
    uint32_t timeout = 10000;
    while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY))
    {
        if (--timeout == 0) return 1;
    }
    
    // 起始信号
    I2C_GenerateSTART(I2C1, ENABLE);
    timeout = 10000;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if (--timeout == 0) return 1;
    }
    
    // 发送地址 + 写
    I2C_Send7bitAddress(I2C1, slaveAddr, I2C_Direction_Transmitter);
    timeout = 10000;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        if (--timeout == 0) return 1;
    }
    
    // 发送内存地址
    I2C_SendData(I2C1, memAddr);
    timeout = 10000;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        if (--timeout == 0) return 1;
    }
    
    // 发送数据
    for (uint16_t i = 0; i < len; i++)
    {
        I2C_SendData(I2C1, pData[i]);
        timeout = 10000;
        while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        {
            if (--timeout == 0) return 1;
        }
    }
    
    // 停止信号
    I2C_GenerateSTOP(I2C1, ENABLE);
    
    return 0;
}

/**
 * @brief  从 EEPROM 读取数据（随机读取）
 */
uint8_t I2C_ReadBuffer(uint8_t slaveAddr, uint8_t memAddr, uint8_t* pData, uint16_t len)
{
    uint32_t timeout;
    
    // ===== 第一阶段：发送内存地址 =====
    while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY));
    
    I2C_GenerateSTART(I2C1, ENABLE);
    timeout = 10000;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if (--timeout == 0) return 1;
    }
    
    I2C_Send7bitAddress(I2C1, slaveAddr, I2C_Direction_Transmitter);
    timeout = 10000;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        if (--timeout == 0) return 1;
    }
    
    I2C_SendData(I2C1, memAddr);
    timeout = 10000;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        if (--timeout == 0) return 1;
    }
    
    // ===== 第二阶段：重复起始 + 读取 =====
    I2C_GenerateSTART(I2C1, ENABLE);
    timeout = 10000;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if (--timeout == 0) return 1;
    }
    
    I2C_Send7bitAddress(I2C1, slaveAddr, I2C_Direction_Receiver);
    timeout = 10000;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
    {
        if (--timeout == 0) return 1;
    }
    
    // 接收数据
    for (uint16_t i = 0; i < len; i++)
    {
        // 最后一个字节：发送 NACK 并产生 STOP
        if (i == len - 1)
        {
            I2C_AcknowledgeConfig(I2C1, DISABLE);
            I2C_GenerateSTOP(I2C1, ENABLE);
        }
        
        timeout = 10000;
        while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED))
        {
            if (--timeout == 0) return 1;
        }
        
        pData[i] = I2C_ReceiveData(I2C1);
    }
    
    // 重新使能 ACK
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    
    return 0;
}

void Delay_ms(uint32_t ms)
{
    volatile uint32_t nCount;
    RCC_ClocksTypeDef RCC_Clocks;
    
    RCC_GetClocksFreq(&RCC_Clocks);
    nCount = (RCC_Clocks.HCLK_Frequency / 10000) * ms;
    
    for (; nCount != 0; nCount--);
}

// printf 重定向
#ifdef __GNUC__
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

PUTCHAR_PROTOTYPE
{
    // 如果使用 USART 输出，需要配置 USART
    // 这里假设使用 ITM 调试输出
    ITM_SendChar(ch);
    return ch;
}
```

### 9.3 代码要点

1. **上拉电阻必须**：SDA(PB7) 和 SCL(PB6) 外接 4.7KΩ 上拉电阻到 3.3V
2. **GPIO 配置**：必须使用 `GPIO_Mode_AF_OD`（复用开漏输出）
3. **地址格式**：EEPROM 地址 0x50 左移 1 位 = 0xA0
4. **页写入限制**：AT24C02 每页 8 字节，不能跨页写入
5. **写延时**：EEPROM 内部写入需要 5~10ms，必须延时
6. **超时处理**：添加超时检测，防止死等导致程序卡死

---

## 十、总结

### 核心要点回顾

```
┌─────────────────────────────────────────────────────────────┐
│                      I2C 核心概念                             │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. I2C 是同步半双工串行通信协议，使用 SDA + SCL 两根线       │
│                                                              │
│  2. 物理层特性：                                              │
│     • 开漏输出，必须外接上拉电阻（4.7KΩ~10KΩ）               │
│     • 支持线与，实现多主仲裁                                  │
│                                                              │
│  3. 协议层特性：                                              │
│     • 起始信号(S)：SCL=1 时 SDA 下降沿                       │
│     • 停止信号(P)：SCL=1 时 SDA 上升沿                       │
│     • 7位地址 + 1位读写位（地址左对齐）                       │
│     • ACK 应答机制                                           │
│                                                              │
│  4. STM32 I2C 配置流程：                                      │
│     使能时钟(GPIO+AFIO+I2C) → 配置GPIO(复用开漏)             │
│     → 配置I2C参数 → 使能I2C                                  │
│                                                              │
│  5. 通信流程：                                                │
│     检查BUSY → 起始信号 → 发送地址 → 等待ACK                 │
│     → 收发数据 → 停止信号                                    │
│                                                              │
│  6. 关键注意：                                                │
│     • 必须接上拉电阻                                         │
│     • 地址左移1位（7位→8位）                                  │
│     • 使用 I2C_CheckEvent() 等待事件                         │
│     • 接收最后一个字节前禁用ACK并提前STOP                     │
│     • 完成后重新使能ACK                                       │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 记忆口诀

> **"两根线来把信传，开漏输出要接上拉；起始地址加应答，半双工里谁主谁从；左移一位别忘记，收发分清ACK和NACK"**

---

**参考文档**:
- 《STM32F10xxx 参考手册》第24章 I2C接口
- 《I2C总线规范》Philips Semiconductor
- AT24C02 数据手册

**文档版本**: v1.0  
**更新日期**: 2026年3月
