# I2C.c / I2C.h 简明创建编写流程

> 基于蓝桥杯 CT107D 开发板（STC15F2K60S2）
> 遵循工程分层：System / Hardware / User

---

## 一、文件创建位置

```
项目根目录/
├── Hardware/           ← 在此文件夹创建
│   ├── I2C.c          # I2C驱动实现
│   ├── I2C.h          # I2C头文件声明
│   ├── PCF8591.c/h    # PCF8591专用驱动（可选）
│   ├── AT24C02.c/h    # AT24C02专用驱动（可选）
│   └── ...
│
├── System/
│   ├── Bus.c/h
│   ├── Init.c/h
│   └── Public.h       # 包含u8/u16等类型定义
│
└── User/
    └── main.c         # 主程序（调用I2C函数）
```

---

## 二、I2C.h 头文件编写

### 步骤1：创建文件
**文件路径**: `Hardware/I2C.h`

### 步骤2：写入内容

```c
/**
 * @file I2C.h
 * @brief 软件I2C总线驱动模块头文件
 * @details 提供软件模拟I2C总线通信接口
 *          - 底层I2C时序：启动、停止、发送、接收、应答
 *          - PCF8591 AD/DA转换器接口
 *          - AT24C02 EEPROM存储器接口
 */

#ifndef __I2C_H__
#define __I2C_H__

#include "Public.h"

// ========== I2C设备地址定义 ==========
/**
 * @brief PCF8591 AD/DA转换器I2C地址
 * @details 写地址：0x90，读地址：0x91
 */
#define PCF8591_ADDR 0x90

/**
 * @brief AT24C02 EEPROM存储器I2C地址
 * @details 写地址：0xA0，读地址：0xA1
 */
#define AT24C02_ADDR 0xA0

// ========== I2C基础时序函数 ==========
/**
 * @brief I2C启动信号
 * @details 产生起始条件：SCL高电平时SDA下降沿
 */
void I2C_Start(void);

/**
 * @brief I2C停止信号
 * @details 产生停止条件：SCL高电平时SDA上升沿
 */
void I2C_Stop(void);

/**
 * @brief I2C发送一个字节
 * @param dat 要发送的8位数据，高位先发(MSB)
 */
void I2C_SendByte(u8 dat);

/**
 * @brief I2C接收一个字节
 * @retval 接收到的8位数据，高位先收(MSB)
 */
u8 I2C_RecvByte(void);

/**
 * @brief I2C等待从机应答
 * @retval 0: 收到应答(ACK)，1: 无应答(NACK)
 */
bit I2C_WaitAck(void);

/**
 * @brief I2C发送应答信号
 * @param ack 应答类型：0=ACK，1=NACK
 */
void I2C_SendAck(bit ack);

// ========== PCF8591 AD/DA转换器接口 ==========
/**
 * @brief PCF8591读取指定通道的AD值
 * @param channel 模拟输入通道号，范围0-3
 *              - 0: 通道0（扩展接口）
 *              - 1: 通道1（光敏电阻）
 *              - 2: 通道2（LM324）
 *              - 3: 通道3（电位器Rb2）
 * @retval 8位AD转换结果，范围0-255
 */
u8 PCF8591_Read(u8 channel);

/**
 * @brief PCF8591输出DA值
 * @param dat 8位DA输出值，范围0-255
 */
void PCF8591_Write(u8 dat);

// ========== AT24C02 EEPROM接口 ==========
/**
 * @brief EEPROM写入一个字节
 * @param addr 写入地址，范围0-255
 * @param dat 要写入的数据
 */
void EEPROM_WriteByte(u8 addr, u8 dat);

/**
 * @brief EEPROM读取一个字节
 * @param addr 读取地址，范围0-255
 * @retval 读取到的数据
 */
u8 EEPROM_ReadByte(u8 addr);

/**
 * @brief EEPROM连续写入多个字节（页写入）
 * @param addr 起始地址（建议8字节对齐）
 * @param buf 数据缓冲区指针
 * @param len 写入长度（不超过8字节）
 */
void EEPROM_WriteBuf(u8 addr, u8 *buf, u8 len);

/**
 * @brief EEPROM连续读取多个字节
 * @param addr 起始地址，范围0-255
 * @param buf 数据缓冲区指针
 * @param len 读取长度（不超过256字节）
 */
void EEPROM_ReadBuf(u8 addr, u8 *buf, u8 len);

#endif /* __I2C_H__ */
```

---

## 三、I2C.c 驱动文件编写

### 步骤1：创建文件
**文件路径**: `Hardware/I2C.c`

### 步骤2：写入内容

```c
/**
 * @file I2C.c
 * @brief 软件I2C总线驱动模块
 * @details 提供软件模拟I2C总线通信功能
 */

#include "I2C.h"
#include <INTRINS.H>

// ========== 引脚定义 ==========
sbit SCL = P2^0;        // I2C时钟线
sbit SDA = P2^1;        // I2C数据线

// ========== 延时系数 ==========
#define DELAY_TIME 5

/**
 * @brief I2C时序延时函数
 */
static void I2C_Delay(void)
{
    u8 i;
    for(i = 0; i < DELAY_TIME; i++)
    {
        _nop_();
    }
}

// ========== 以下是赛题自带基础函数 ==========

void I2C_Start(void)
{
    SDA = 1;
    SCL = 1;
    I2C_Delay();
    SDA = 0;
    I2C_Delay();
    SCL = 0;
}

void I2C_Stop(void)
{
    SDA = 0;
    SCL = 1;
    I2C_Delay();
    SDA = 1;
    I2C_Delay();
}

void I2C_SendByte(u8 dat)
{
    u8 i;
    for(i = 0; i < 8; i++)
    {
        SCL = 0;
        if(dat & 0x80)
            SDA = 1;
        else
            SDA = 0;
        I2C_Delay();
        SCL = 1;
        I2C_Delay();
        dat <<= 1;
    }
    SCL = 0;
}

u8 I2C_RecvByte(void)
{
    u8 i, dat = 0;
    SDA = 1;
    for(i = 0; i < 8; i++)
    {
        SCL = 1;
        I2C_Delay();
        dat <<= 1;
        if(SDA)
            dat |= 0x01;
        SCL = 0;
        I2C_Delay();
    }
    return dat;
}

bit I2C_WaitAck(void)
{
    bit ack;
    SCL = 1;
    I2C_Delay();
    ack = SDA;
    SCL = 0;
    I2C_Delay();
    return ack;
}

void I2C_SendAck(bit ack)
{
    SCL = 0;
    SDA = ack;
    I2C_Delay();
    SCL = 1;
    I2C_Delay();
    SCL = 0;
    SDA = 1;
    I2C_Delay();
}

// ========== 以上是赛题自带基础函数 ==========

// ========== PCF8591 AD/DA 驱动函数 ==========

u8 PCF8591_Read(u8 channel)
{
    u8 dat;
    
    // 第一次通信：发送控制字
    I2C_Start();
    I2C_SendByte(PCF8591_ADDR);
    I2C_WaitAck();
    I2C_SendByte(0x00 | (channel & 0x03));
    I2C_WaitAck();
    
    // 第二次通信：读取数据
    I2C_Start();
    I2C_SendByte(PCF8591_ADDR | 0x01);
    I2C_WaitAck();
    (void)I2C_RecvByte();           // Dummy Read，丢弃
    I2C_SendAck(0);
    dat = I2C_RecvByte();            // 有效数据
    I2C_SendAck(1);
    I2C_Stop();
    
    return dat;
}

void PCF8591_Write(u8 dat)
{
    I2C_Start();
    I2C_SendByte(PCF8591_ADDR);
    I2C_WaitAck();
    I2C_SendByte(0x40);             // 使能DA输出
    I2C_WaitAck();
    I2C_SendByte(dat);
    I2C_WaitAck();
    I2C_Stop();
}

// ========== AT24C02 EEPROM 驱动函数 ==========

void EEPROM_WriteByte(u8 addr, u8 dat)
{
    I2C_Start();
    I2C_SendByte(AT24C02_ADDR);
    I2C_WaitAck();
    I2C_SendByte(addr);
    I2C_WaitAck();
    I2C_SendByte(dat);
    I2C_WaitAck();
    I2C_Stop();
    
    // 写周期延时（>5ms）
    _nop_(); _nop_(); _nop_(); _nop_();
}

u8 EEPROM_ReadByte(u8 addr)
{
    u8 dat;
    
    // 发送存储地址
    I2C_Start();
    I2C_SendByte(AT24C02_ADDR);
    I2C_WaitAck();
    I2C_SendByte(addr);
    I2C_WaitAck();
    
    // 读取数据
    I2C_Start();
    I2C_SendByte(AT24C02_ADDR | 0x01);
    I2C_WaitAck();
    dat = I2C_RecvByte();
    I2C_SendAck(1);
    I2C_Stop();
    
    return dat;
}

void EEPROM_WriteBuf(u8 addr, u8 *buf, u8 len)
{
    I2C_Start();
    I2C_SendByte(AT24C02_ADDR);
    I2C_WaitAck();
    I2C_SendByte(addr);
    I2C_WaitAck();
    while(len--)
    {
        I2C_SendByte(*buf++);
        I2C_WaitAck();
    }
    I2C_Stop();
    
    // 写周期延时
    _nop_(); _nop_(); _nop_(); _nop_();
}

void EEPROM_ReadBuf(u8 addr, u8 *buf, u8 len)
{
    // 发送存储地址
    I2C_Start();
    I2C_SendByte(AT24C02_ADDR);
    I2C_WaitAck();
    I2C_SendByte(addr);
    I2C_WaitAck();
    
    // 连续读取
    I2C_Start();
    I2C_SendByte(AT24C02_ADDR | 0x01);
    I2C_WaitAck();
    while(len--)
    {
        *buf++ = I2C_RecvByte();
        I2C_SendAck(len == 0);      // 最后一个字节发NACK
    }
    I2C_Stop();
}
```

---

## 四、User/main.c 中调用示例

### 4.1 PCF8591读取光敏电阻并显示

```c
#include "System/Init.h"
#include "Hardware/I2C.h"
#include "Hardware/Nixie.h"

u8 adc_value;        // 存储AD值

void main(void)
{
    System_Init();
    
    while(1)
    {
        // 读取通道1（光敏电阻）
        adc_value = PCF8591_Read(1);
        
        // 在数码管上显示0-255
        Nixie_DisplayNumber(adc_value);
        
        Delay_ms(100);  // 延时100ms
    }
}
```

### 4.2 PCF8591 DA输出（可调电压）

```c
#include "System/Init.h"
#include "Hardware/I2C.h"

u8 da_value = 128;   // DA输出值，128对应约2.5V

void main(void)
{
    System_Init();
    
    while(1)
    {
        // 输出DA值（可用万用表测量AOUT引脚）
        PCF8591_Write(da_value);
        
        // 调节输出值
        if(++da_value > 255)
            da_value = 0;
            
        Delay_ms(500);
    }
}
```

### 4.3 EEPROM存储按键次数

```c
#include "System/Init.h"
#include "Hardware/I2C.h"
#include "Hardware/Key.h"
#include "Hardware/Nixie.h"

u8 key_count;        // 按键次数

void main(void)
{
    System_Init();
    
    // 上电时从EEPROM读取之前的次数
    key_count = EEPROM_ReadByte(0x00);
    
    while(1)
    {
        if(Key_Scan() == KEY_PRESS)
        {
            key_count++;                    // 次数加1
            EEPROM_WriteByte(0x00, key_count);  // 保存到EEPROM
        }
        
        Nixie_DisplayNumber(key_count);     // 显示次数
    }
}
```

### 4.4 电位器控制数码管亮度（综合应用）

```c
#include "System/Init.h"
#include "Hardware/I2C.h"
#include "Hardware/Nixie.h"

u8 adc_value;        // 电位器读数
u8 brightness;       // 亮度值

void main(void)
{
    System_Init();
    
    while(1)
    {
        // 读取通道3（电位器Rb2）
        adc_value = PCF8591_Read(3);
        
        // 将0-255映射到亮度级别
        brightness = adc_value / 25;  // 0-10级亮度
        
        // 设置数码管亮度
        Nixie_SetBrightness(brightness);
        
        // 显示当前AD值
        Nixie_DisplayNumber(adc_value);
    }
}
```

---

## 五、关键要点速查

| 要点 | 说明 |
|------|------|
| **I2C引脚** | SCL=P20, SDA=P21 |
| **PCF8591写地址** | 0x90 |
| **PCF8591读地址** | 0x91 |
| **AT24C02写地址** | 0xA0 |
| **AT24C02读地址** | 0xA1 |
| **PCF8591通道** | 0-3，1=光敏，3=电位器 |
| **Dummy Read** | PCF8591需要两次读取，第一次丢弃 |
| **EEPROM写周期** | 写入后必须延时>5ms |
| **页写入** | 一次最多8字节，不能跨页 |
| **ACK/NACK** | 0=ACK（继续），1=NACK（结束） |

---

## 六、常见问题

**Q1: PCF8591读取值不稳定？**
- 确认使用了Dummy Read机制
- 检查通道号是否正确（0-3）
- 增加采样延时或多次采样取平均

**Q2: EEPROM写入后读取不正确？**
- 检查是否延时5ms以上
- 确认写入地址和读取地址一致
- 检查是否跨页（地址0x07后不能直接写0x08）

**Q3: 编译报错找不到I2C函数？**
- 确认I2C.c已添加到Keil工程
- 确认main.c中`#include "Hardware/I2C.h"`
- 检查Public.h是否包含u8/u16等类型定义

**Q4: 设备无应答？**
- 检查设备地址是否正确
- 检查SCL/SDA引脚是否接错
- 检查设备是否损坏
- 增加I2C_Delay延时时间

---

## 七、函数调用关系速查

```
【读取PCF8591】
PCF8591_Read(channel)
  ├─ I2C_Start()
  ├─ I2C_SendByte(0x90)
  ├─ I2C_WaitAck()
  ├─ I2C_SendByte(控制字)
  ├─ I2C_WaitAck()
  ├─ I2C_Start()
  ├─ I2C_SendByte(0x91)
  ├─ I2C_WaitAck()
  ├─ I2C_RecvByte() ← Dummy
  ├─ I2C_SendAck(0)
  ├─ I2C_RecvByte() ← 有效数据
  ├─ I2C_SendAck(1)
  └─ I2C_Stop()

【写入EEPROM】
EEPROM_WriteByte(addr, dat)
  ├─ I2C_Start()
  ├─ I2C_SendByte(0xA0)
  ├─ I2C_WaitAck()
  ├─ I2C_SendByte(addr)
  ├─ I2C_WaitAck()
  ├─ I2C_SendByte(dat)
  ├─ I2C_WaitAck()
  ├─ I2C_Stop()
  └─ Delay_ms(5)
```

---

**文档版本**: V1.0  
**适用板型**: CT107D (STC15F2K60S2)  
**更新日期**: 2026年
