# 软件IIC（模拟IIC）详解

## 一、什么是软件IIC

### 1.1 软件IIC vs 硬件IIC

| 对比项 | 硬件IIC | 软件IIC |
|--------|---------|---------|
| **实现方式** | 使用MCU内置I2C外设 | 用GPIO模拟时序 |
| **引脚选择** | 固定引脚（如STM32的PB6/7或PB8/9） | **任意GPIO** |
| **波特率控制** | 硬件分频器配置 | 软件延时控制 |
| **CPU占用** | 低（可DMA传输） | 高（全程占用CPU） |
| **灵活性** | 受硬件限制 | **极高，任意引脚、任意速率** |
| **稳定性** | 高（硬件处理时序） | 依赖延时精度 |
| **适用场景** | 通用通信 | 引脚受限、移植到无I2C外设的MCU |

### 1.2 为什么要用软件IIC

1. **引脚被占用**：硬件I2C引脚被其他功能占用
2. **引脚随意选**：想使用任意GPIO，不局限于特定引脚
3. **调试方便**：可以直接观察波形，方便理解协议
4. **移植性强**：任何MCU都能实现，只要有GPIO
5. **双I2C需求**：需要同时使用多组I2C，但硬件只有1-2组

---

## 二、软件IIC的核心原理

### 2.1 GPIO配置要求

**必须使用开漏输出（Open-Drain）**：

```c
GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;  // 开漏输出
```

**为什么用开漏？**

```
         VCC
          │
         ┌┴┐
         │ │ 上拉电阻
         └┬┘
          ├──────── 总线
          │
    ┌─────┴─────┐
    │   MOSFET   │ ← 开漏输出：只能拉低或释放
    │  (控制端)  │
    └─────┬─────┘
          │
         GND
```

- **输出0**：MOSFET导通，总线被拉低
- **输出1**：MOSFET截止，总线由上拉电阻拉高
- **输入**：MOSFET截止时，可读取外部电平

### 2.2 时序控制方法

软件IIC用**延时**控制时序：

```c
#define I2C_DELAY_US    5   // 延时5微秒，对应约100kHz

void Delay_us(uint32_t us);  // 微秒延时函数
```

**速率计算**：
- 标准模式100kHz：每位约10μs，半周期约5μs
- 快速模式400kHz：每位约2.5μs，半周期约1.25μs

---

## 三、完整代码实现

### 3.1 引脚定义

```c
#include "stm32f10x.h"
#include "Delay.h"

/* ========== 引脚定义（可修改为任意GPIO）========== */
#define I2C_SCL_PORT    GPIOB
#define I2C_SDA_PORT    GPIOB
#define I2C_SCL_PIN     GPIO_Pin_10
#define I2C_SDA_PIN     GPIO_Pin_11

/* ========== 引脚操作宏 ========== */
#define SCL_H()         GPIO_SetBits(I2C_SCL_PORT, I2C_SCL_PIN)
#define SCL_L()         GPIO_ResetBits(I2C_SCL_PORT, I2C_SCL_PIN)
#define SDA_H()         GPIO_SetBits(I2C_SDA_PORT, I2C_SDA_PIN)
#define SDA_L()         GPIO_ResetBits(I2C_SDA_PORT, I2C_SDA_PIN)
#define SCL_READ()      GPIO_ReadInputDataBit(I2C_SCL_PORT, I2C_SCL_PIN)
#define SDA_READ()      GPIO_ReadInputDataBit(I2C_SDA_PORT, I2C_SDA_PIN)
```

### 3.2 初始化函数

```c
/**
 * @brief  软件I2C初始化
 * @note   配置SCL和SDA为开漏输出模式
 */
void Soft_I2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    
    // 开启GPIO时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    // 配置SCL - 开漏输出
    GPIO_InitStruct.GPIO_Pin = I2C_SCL_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;  // 开漏输出（关键！）
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C_SCL_PORT, &GPIO_InitStruct);
    
    // 配置SDA - 开漏输出
    GPIO_InitStruct.GPIO_Pin = I2C_SDA_PIN;
    GPIO_Init(I2C_SDA_PORT, &GPIO_InitStruct);
    
    // 初始状态：都置高（空闲状态）
    SCL_H();
    SDA_H();
}
```

### 3.3 起始信号

```c
/**
 * @brief  起始信号
 * @note   时序：SCL=1时，SDA从高跳变到低
 *         ___
 *  SDA       \____
 *         ___
 *  SCL       \____
 */
void Soft_I2C_Start(void)
{
    // 确保初始状态
    SDA_H();
    Delay_us(I2C_DELAY_US);
    SCL_H();
    Delay_us(I2C_DELAY_US);
    
    // SCL=1时，SDA从高变低
    SDA_L();
    Delay_us(I2C_DELAY_US);
    
    // 拉低SCL，准备发送数据
    SCL_L();
}
```

### 3.4 停止信号

```c
/**
 * @brief  停止信号
 * @note   时序：SCL=1时，SDA从低跳变到高
 *              ___
 *  SDA  ____/
 *              ___
 *  SCL  ____/
 */
void Soft_I2C_Stop(void)
{
    SCL_L();
    Delay_us(I2C_DELAY_US);
    SDA_L();
    Delay_us(I2C_DELAY_US);
    
    SCL_H();
    Delay_us(I2C_DELAY_US);
    
    // SCL=1时，SDA从低变高
    SDA_H();
    Delay_us(I2C_DELAY_US);
}
```

### 3.5 发送一个字节

```c
/**
 * @brief  发送一个字节
 * @param  byte: 要发送的数据
 * @note   先发高位(bit7)，后发低位(bit0)
 *         SCL=0时改变SDA，SCL=1时从机采样
 */
void Soft_I2C_SendByte(uint8_t byte)
{
    uint8_t i;
    
    for(i = 0; i < 8; i++)
    {
        SCL_L();                            // 拉低时钟，准备改变数据
        Delay_us(I2C_DELAY_US);
        
        // 设置SDA为要发送的位（从高位开始）
        if(byte & 0x80)                     // bit7为1
            SDA_H();
        else
            SDA_L();
        
        Delay_us(I2C_DELAY_US);
        SCL_H();                            // 拉高时钟，从机在此采样
        Delay_us(I2C_DELAY_US);
        
        byte <<= 1;                         // 左移，准备发送下一位
    }
    
    SCL_L();                                // 拉低时钟，准备接收ACK
}
```

### 3.6 等待从机ACK

```c
/**
 * @brief  等待从机应答
 * @retval 0: 收到ACK，1: 收到NACK
 * @note   释放SDA，在第9个时钟周期读取SDA状态
 */
uint8_t Soft_I2C_WaitACK(void)
{
    uint8_t ack = 0;
    
    SDA_H();                                // 释放SDA（主机置高，让从机可以拉低）
    Delay_us(I2C_DELAY_US);
    
    SCL_H();                                // 拉高时钟，进入ACK周期
    Delay_us(I2C_DELAY_US);
    
    if(SDA_READ() == 1)                     // 读取SDA状态
        ack = 1;                            // SDA=1，无应答（NACK）
    
    SCL_L();                                // 拉低时钟，结束ACK周期
    Delay_us(I2C_DELAY_US);
    
    return ack;
}
```

### 3.7 接收一个字节

```c
/**
 * @brief  接收一个字节
 * @retval 接收到的数据
 * @note   先收高位(bit7)，后收低位(bit0)
 *         主机控制SCL，在SCL=1时读取SDA
 */
uint8_t Soft_I2C_ReceiveByte(void)
{
    uint8_t i, byte = 0;
    
    SDA_H();                                // 释放SDA，准备读取
    
    for(i = 0; i < 8; i++)
    {
        byte <<= 1;                         // 左移，为新位腾出位置
        
        SCL_L();                            // 拉低时钟，通知从机准备数据
        Delay_us(I2C_DELAY_US);
        SCL_H();                            // 拉高时钟，从机数据已稳定
        Delay_us(I2C_DELAY_US);
        
        if(SDA_READ() == 1)                 // 读取SDA状态
            byte |= 0x01;                   // 收到1
        // 否则bit保持0
    }
    
    SCL_L();                                // 拉低时钟，准备发送ACK/NACK
    return byte;
}
```

### 3.8 发送ACK/NACK

```c
/**
 * @brief  发送应答信号
 * @param  ack: 0=ACK，1=NACK
 * @note   第9个时钟周期控制SDA：
 *         SDA=0表示ACK，SDA=1表示NACK
 */
void Soft_I2C_SendACK(uint8_t ack)
{
    SCL_L();                                // 拉低时钟，准备设置SDA
    Delay_us(I2C_DELAY_US);
    
    if(ack == 0)
        SDA_L();                            // ACK：拉低SDA
    else
        SDA_H();                            // NACK：置高SDA
    
    Delay_us(I2C_DELAY_US);
    SCL_H();                                // 拉高时钟，从机读取ACK/NACK
    Delay_us(I2C_DELAY_US);
    SCL_L();                                // 拉低时钟，结束ACK周期
    Delay_us(I2C_DELAY_US);
}
```

---

## 四、高级封装函数

### 4.1 发送多字节

```c
/**
 * @brief  软件I2C发送多字节
 * @param  Addr: 从机地址（8位，bit0=0表示写）
 * @param  pData: 数据缓冲区
 * @param  Size: 数据长度
 * @retval 0: 成功，-1: 寻址失败，-2: 数据拒收
 */
int Soft_I2C_SendBytes(uint8_t Addr, uint8_t *pData, uint16_t Size)
{
    uint16_t i;
    
    Soft_I2C_Start();                       // 起始信号
    
    Soft_I2C_SendByte(Addr & 0xFE);         // 发送地址（写操作）
    if(Soft_I2C_WaitACK() != 0)             // 等待应答
    {
        Soft_I2C_Stop();
        return -1;                          // 寻址失败
    }
    
    for(i = 0; i < Size; i++)
    {
        Soft_I2C_SendByte(pData[i]);        // 发送数据
        if(Soft_I2C_WaitACK() != 0)         // 等待应答
        {
            Soft_I2C_Stop();
            return -2;                      // 数据拒收
        }
    }
    
    Soft_I2C_Stop();                        // 停止信号
    return 0;
}
```

### 4.2 接收多字节

```c
/**
 * @brief  软件I2C接收多字节
 * @param  Addr: 从机地址（8位，bit0=1表示读）
 * @param  pBuffer: 接收缓冲区
 * @param  Size: 接收长度
 * @retval 0: 成功，-1: 寻址失败
 */
int Soft_I2C_ReceiveBytes(uint8_t Addr, uint8_t *pBuffer, uint16_t Size)
{
    uint16_t i;
    
    Soft_I2C_Start();                       // 起始信号
    
    Soft_I2C_SendByte(Addr | 0x01);         // 发送地址（读操作）
    if(Soft_I2C_WaitACK() != 0)             // 等待应答
    {
        Soft_I2C_Stop();
        return -1;                          // 寻址失败
    }
    
    for(i = 0; i < Size; i++)
    {
        pBuffer[i] = Soft_I2C_ReceiveByte(); // 接收数据
        
        if(i < Size - 1)
            Soft_I2C_SendACK(0);            // 不是最后字节，回ACK继续接收
        else
            Soft_I2C_SendACK(1);            // 最后字节，回NACK停止
    }
    
    Soft_I2C_Stop();                        // 停止信号
    return 0;
}
```

---

## 五、延时函数实现

### 5.1 基于SysTick的微秒延时

```c
/**
 * @brief  微秒延时（使用SysTick）
 * @param  us: 延时微秒数
 * @note   假设系统时钟72MHz，SysTick每12个周期计数一次
 */
void Delay_us(uint32_t us)
{
    // 简单实现：空循环延时
    // 72MHz时，1us约72个时钟周期
    volatile uint32_t count;
    while(us--)
    {
        for(count = 0; count < 72; count++);
    }
}
```

### 5.2 使用定时器实现精确延时

```c
/**
 * @brief  使用TIM2实现微秒延时
 */
void Delay_us(uint32_t us)
{
    TIM_SetCounter(TIM2, 0);
    TIM_Cmd(TIM2, ENABLE);
    while(TIM_GetCounter(TIM2) < us);
    TIM_Cmd(TIM2, DISABLE);
}
```

---

## 六、常见问题

### 6.1 通信速率太慢

**原因**：延时太长

**优化**：
```c
#define I2C_DELAY_US    2   // 改为2us，约250kHz
```

### 6.2 通信不稳定

**原因**：
- 延时不够精确
- 中断干扰

**解决**：
```c
// 关键时序关中断
__disable_irq();
Soft_I2C_SendByte(data);
__enable_irq();
```

### 6.3 读取数据错误

**原因**：SDA未正确释放

**检查**：
```c
// 确保读取前SDA已置高（释放）
SDA_H();
Delay_us(I2C_DELAY_US);
if(SDA_READ() == ...)  // 然后再读取
```

---

## 七、软件IIC vs 硬件IIC 总结

| 场景 | 推荐方案 |
|------|----------|
| 通用开发、学习 | **软件IIC**（更易理解，更灵活） |
| 生产环境、高速通信 | 硬件IIC（更稳定，支持DMA） |
| 引脚受限 | **软件IIC**（任意GPIO） |
| 低功耗要求 | 硬件IIC（DMA传输时CPU可休眠） |
| 调试协议 | **软件IIC**（可单步调试观察） |

---

## 八、完整使用示例

```c
int main(void)
{
    // 初始化
    Soft_I2C_Init();
    
    // 发送数据
    uint8_t txData[] = {0x00, 0x8D, 0x14, 0xAF};
    Soft_I2C_SendBytes(0x78, txData, 4);
    
    // 接收数据
    uint8_t rxData;
    Soft_I2C_ReceiveBytes(0x78, &rxData, 1);
    
    while(1);
}
```

软件IIC的核心优势是**简单直观**，完全按照协议时序编写，非常适合学习和调试。
