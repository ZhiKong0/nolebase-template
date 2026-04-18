# OLED显示器详解（SSD1306驱动）

## 一、OLED显示器概述

### 1.1 什么是OLED

**OLED**（Organic Light-Emitting Diode，有机发光二极管）是一种自发光的显示技术，相比传统LCD：

| 特性 | OLED | LCD |
|------|------|-----|
| **发光原理** | 自发光 | 背光透射 |
| **对比度** | 极高（纯黑） | 较低 |
| **视角** | 接近180° | 较窄 |
| **响应速度** | 微秒级 | 毫秒级 |
| **功耗** | 显示黑色时最低 | 恒定 |
| **厚度** | 极薄 | 较厚 |

### 1.2 常见OLED模块规格

| 尺寸 | 分辨率 | 驱动芯片 | 通信接口 | 常见用途 |
|------|--------|----------|----------|----------|
| 0.96寸 | 128×64 | SSD1306 | I2C/SPI | 小型仪表、手环 |
| 1.3寸 | 128×64 | SSD1306/SH1106 | I2C/SPI | 开发板、小设备 |
| 1.54寸 | 128×64 | SSD1309 | I2C/SPI | 手持设备 |

**本文以最常见的 0.96寸 SSD1306 I2C接口 为例**

---

## 二、SSD1306驱动芯片

### 2.1 SSD1306基本参数

| 参数 | 值 |
|------|-----|
| **分辨率** | 128×64 像素 |
| **颜色** | 单色（黑/白或黑/蓝） |
| **驱动电压** | 7.5V~15V（内部DC-DC升压） |
| **逻辑电压** | 3.3V/5V |
| **I2C地址** | 0x3C（7位）/ 0x78（8位写） |
| **帧率** | 100fps@128×64 |

### 2.2 显示RAM结构

SSD1306内置 **GDDRAM**（Graphic Display Data RAM）：

```
GDDRAM结构（128×64位 = 1024字节）:

页(Page) 0:  [0x00] [0x01] ... [0x7F]  ← COM0~COM7
页(Page) 1:  [0x00] [0x01] ... [0x7F]  ← COM8~COM15
页(Page) 2:  [0x00] [0x01] ... [0x7F]  ← COM16~COM23
...                                    （每页8行）
页(Page) 7:  [0x00] [0x01] ... [0x7F]  ← COM56~COM63

            ↓
         列地址 0~127
         
每个字节控制8个像素（垂直方向）：
D7 ──→ COM(n+7)  (最下面)
D6 ──→ COM(n+6)
...
D1 ──→ COM(n+1)
D0 ──→ COM(n)     (最上面)
```

**寻址方式**：
- **页地址模式**（默认）：先选页，再选列，自动递增列地址
- **水平地址模式**：自动递增列和页
- **垂直地址模式**：自动递增页和列

---

## 三、I2C通信协议

### 3.1 OLED的I2C地址

| 配置 | 7位地址 | 8位写地址 | 8位读地址 |
|------|---------|-----------|-----------|
| SA0=0（默认）| 0x3C | 0x78 | 0x79 |
| SA0=1 | 0x3D | 0x7A | 0x7B |

**SA0引脚**：通常接GND，所以默认地址是 0x3C（写操作发送0x78）

### 3.2 I2C数据格式

OLED要求每个数据包包含**控制字节+数据字节**：

```
Control Byte: | Co | D/C# | 0 | 0 | 0 | 0 | 0 | 0 |
               ─┬──┬──
                 │  │
                 │  └─ 0=命令，1=数据
                 └─ 0=单字节，1=连续字节
```

| Co | D/C# | 含义 |
|----|------|------|
| 0 | 0 | 单字节命令 |
| 0 | 1 | 单字节数据 |
| 1 | 0 | 连续命令（最后一个字节Co=0）|
| 1 | 1 | 连续数据（最后一个字节Co=0）|

**实际使用**：
- 发送命令：`0x00` + 命令字节
- 发送数据：`0x40` + 数据字节

### 3.3 完整通信示例

发送命令点亮屏幕：
```
Start │ 0x78 │ ACK │ 0x00 │ ACK │ 0xAF │ ACK │ Stop
       ─┬─         ↑       ↑       ↑      ↑      ─┬─
        │        地址    控制字节  命令(开显示)   │
      Start                                      Stop
```

---

## 四、常用命令详解

### 4.1 基础命令

```c
#define OLED_CMD    0x00   // 命令控制字节
#define OLED_DATA   0x40   // 数据控制字节

// 显示开关
#define OLED_DISPLAY_OFF    0xAE   // 关显示
#define OLED_DISPLAY_ON     0xAF   // 开显示

// 对比度设置（亮度）
#define OLED_SET_CONTRAST   0x81   // 设置对比度命令
// 后跟1字节：0x00~0xFF，越大越亮

// 显示方向
#define OLED_NORMAL_DISPLAY  0xA6   // 正常显示（1=亮，0=暗）
#define OLED_INVERSE_DISPLAY 0xA7   // 反显（0=亮，1=暗）
```

### 4.2 地址设置命令

```c
// 设置列地址（仅页地址模式）
#define OLED_SET_COLUMN_LOW   0x00   // 列地址低4位 (0x00~0x0F)
#define OLED_SET_COLUMN_HIGH  0x10   // 列地址高4位 (0x10~0x1F)
// 实际列地址 = (高位字节 & 0x0F) << 4 | (低位字节 & 0x0F)

// 设置页地址
#define OLED_SET_PAGE           0xB0   // 页0~7：0xB0~0xB7

// 设置地址模式
#define OLED_SET_MEMORY_ADDR_MODE  0x20   // 后跟1字节：
#define OLED_HORIZONTAL_MODE       0x00   // 水平地址模式
#define OLED_VERTICAL_MODE         0x01   // 垂直地址模式
#define OLED_PAGE_MODE             0x02   // 页地址模式（默认）
```

### 4.3 硬件配置命令

```c
// 设置显示起始行
#define OLED_SET_START_LINE  0x40   // 0x40~0x7F对应行0~63

// 设置段重映射（水平翻转）
#define OLED_SEG_REMAP_OFF   0xA0   // 正常 (列0在左)
#define OLED_SEG_REMAP_ON    0xA1   // 翻转 (列127在左)

// 设置COM扫描方向（垂直翻转）
#define OLED_COM_SCAN_NORMAL 0xC0   // 正常 (从COM0到COM63)
#define OLED_COM_SCAN_REMAPPED 0xC8   // 翻转 (从COM63到COM0)

// 设置COM引脚硬件配置
#define OLED_SET_COM_PINS    0xDA   // 后跟1字节：
#define OLED_COM_PINS_SEQ    0x02   // 顺序
#define OLED_COM_PINS_ALT    0x12   // 交替（默认）
#define OLED_COM_PINS_DISABLE_REMAP 0x02   // 禁用左右重映射
#define OLED_COM_PINS_ENABLE_REMAP  0x22   // 启用左右重映射
```

### 4.4 时序和电荷泵命令

```c
// 设置显示时钟分频/振荡频率
#define OLED_SET_DISPLAY_CLOCK  0xD5   // 后跟1字节：
// 高4位：振荡频率，低4位：分频比
#define OLED_CLOCK_DIV_1_FREQ_8  0x80   // 默认

// 设置预充电周期
#define OLED_SET_PRECHARGE   0xD9   // 后跟1字节：
#define OLED_PRECHARGE_DEFAULT 0xF1   // 默认

// 设置VCOMH取消选择电平
#define OLED_SET_VCOMH       0xDB   // 后跟1字节：
#define OLED_VCOMH_DEFAULT   0x30   // 默认

// 电荷泵设置（关键！）
#define OLED_CHARGE_PUMP     0x8D   // 后跟1字节：
#define OLED_CHARGE_PUMP_ENABLE  0x14   // 使能电荷泵（点亮屏幕必须！）
#define OLED_CHARGE_PUMP_DISABLE 0x10   // 禁用
```

---

## 五、完整初始化流程

### 5.1 标准初始化代码

```c
/**
 * @brief  OLED初始化
 * @note   标准SSD1306初始化序列
 */
void OLED_Init(void)
{
    Delay_ms(100);  // 上电延时，等待OLED稳定
    
    // 初始化命令序列
    uint8_t init_cmds[] = {
        0xAE,           // 关显示
        0xD5, 0x80,     // 设置时钟分频/振荡频率
        0xA8, 0x3F,     // 设置多路复用率 (64行)
        0xD3, 0x00,     // 设置显示偏移
        0x40,           // 设置显示起始行
        0x8D, 0x14,     // 使能电荷泵（关键！）
        0x20, 0x02,     // 设置内存地址模式（页模式）
        0xA1,           // 段重映射（左右翻转，可选）
        0xC8,           // COM扫描方向（上下翻转，可选）
        0xDA, 0x12,     // 设置COM引脚硬件配置
        0x81, 0xCF,     // 设置对比度
        0xD9, 0xF1,     // 设置预充电周期
        0xDB, 0x30,     // 设置VCOMH
        0xA4,           // 全局显示开（使用GDDRAM内容）
        0xA6,           // 正常显示（非反显）
        0xAF            // 开显示
    };
    
    // 发送初始化命令
    for(int i = 0; i < sizeof(init_cmds); i++)
    {
        OLED_WriteCommand(init_cmds[i]);
    }
    
    OLED_Clear();   // 清屏
}
```

### 5.2 关键命令说明

| 命令 | 作用 | 注意 |
|------|------|------|
| `0x8D, 0x14` | 使能电荷泵 | **必须！** 否则屏幕不亮 |
| `0xA4` | 显示GDDRAM内容 | 用 `0xA5` 可全屏点亮测试 |
| `0xAF` | 开显示 | 最后发送 |

---

## 六、显示操作函数

### 6.1 基础写函数

```c
/**
 * @brief  向OLED写入命令
 */
void OLED_WriteCommand(uint8_t cmd)
{
    uint8_t data[2] = {0x00, cmd};  // 0x00=命令控制字节
    I2C_SendBytes(OLED_ADDR, data, 2);
}

/**
 * @brief  向OLED写入数据
 */
void OLED_WriteData(uint8_t dat)
{
    uint8_t data[2] = {0x40, dat};  // 0x40=数据控制字节
    I2C_SendBytes(OLED_ADDR, data, 2);
}
```

### 6.2 清屏函数

```c
/**
 * @brief  清屏
 */
void OLED_Clear(void)
{
    for(uint8_t page = 0; page < 8; page++)  // 8页
    {
        OLED_WriteCommand(0xB0 + page);     // 设置页地址
        OLED_WriteCommand(0x00);              // 设置列低地址
        OLED_WriteCommand(0x10);              // 设置列高地址
        
        for(uint8_t col = 0; col < 128; col++)  // 128列
        {
            OLED_WriteData(0x00);           // 写入0（不显示）
        }
    }
}
```

### 6.3 点亮/熄灭屏幕

```c
/**
 * @brief  开显示
 */
void OLED_DisplayOn(void)
{
    OLED_WriteCommand(0x8D);  // 电荷泵
    OLED_WriteCommand(0x14);  // 使能
    OLED_WriteCommand(0xAF);  // 开显示
}

/**
 * @brief  关显示
 */
void OLED_DisplayOff(void)
{
    OLED_WriteCommand(0x8D);  // 电荷泵
    OLED_WriteCommand(0x10);  // 禁用
    OLED_WriteCommand(0xAE);  // 关显示
}
```

### 6.4 设置光标位置

```c
/**
 * @brief  设置光标位置
 * @param  x: 列地址 0~127
 * @param  y: 页地址 0~7（对应行0~63，每页8行）
 */
void OLED_SetCursor(uint8_t x, uint8_t y)
{
    OLED_WriteCommand(0xB0 + y);            // 设置页地址
    OLED_WriteCommand(0x00 + (x & 0x0F));   // 设置列低地址
    OLED_WriteCommand(0x10 + ((x >> 4) & 0x0F));  // 设置列高地址
}
```

---

## 七、显示内容函数

### 7.1 显示一个字符

```c
// ASCII字符点阵（6×8字体，简化版）
const uint8_t ASCII_6x8[][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00},  // 空格
    {0x00,0x00,0x2F,0x00,0x00,0x00},  // !
    // ... 其他字符
    {0x3E,0x51,0x49,0x45,0x3E,0x00},  // 0
    {0x00,0x42,0x7F,0x40,0x00,0x00},  // 1
    // ...
};

/**
 * @brief  显示一个字符（6×8字体）
 * @param  x: 列位置
 * @param  y: 页位置
 * @param  chr: 要显示的字符
 */
void OLED_ShowChar(uint8_t x, uint8_t y, char chr)
{
    OLED_SetCursor(x, y);
    
    for(uint8_t i = 0; i < 6; i++)
    {
        OLED_WriteData(ASCII_6x8[chr - 32][i]);
    }
}
```

### 7.2 显示字符串

```c
/**
 * @brief  显示字符串
 * @param  x: 起始列
 * @param  y: 起始页
 * @param  str: 字符串
 */
void OLED_ShowString(uint8_t x, uint8_t y, char *str)
{
    while(*str != '\0')
    {
        OLED_ShowChar(x, y, *str);
        x += 6;  // 6像素宽度
        if(x > 122)  // 换行
        {
            x = 0;
            y++;
        }
        str++;
    }
}
```

### 7.3 显示图片

```c
/**
 * @brief  显示BMP图片（128×64）
 * @param  BMP: 图片数组（1024字节）
 */
void OLED_DrawBMP(const uint8_t *BMP)
{
    for(uint8_t page = 0; page < 8; page++)
    {
        OLED_WriteCommand(0xB0 + page);  // 设置页
        OLED_WriteCommand(0x00);          // 列低地址
        OLED_WriteCommand(0x10);          // 列高地址
        
        for(uint8_t col = 0; col < 128; col++)
        {
            OLED_WriteData(BMP[page * 128 + col]);
        }
    }
}
```

---

## 八、OLED读取状态

### 8.1 读取状态字节

OLED支持读取状态寄存器，Bit6表示显示开关状态：

```c
/**
 * @brief  读取OLED状态
 * @retval 状态字节
 * @note   Bit6: 0=显示开，1=显示关
 */
uint8_t OLED_ReadStatus(void)
{
    uint8_t status;
    I2C_ReceiveBytes(OLED_ADDR, &status, 1);
    return status;
}
```

### 8.2 状态字节解析

| 位 | 含义 |
|----|------|
| D7 | 保留 |
| D6 | 显示开/关（0=开，1=关） |
| D5-D0 | 保留 |

---

## 九、常见问题排查

### 9.1 屏幕不亮

**排查步骤**：
1. 检查I2C通信是否正常（示波器/逻辑分析仪）
2. 确认是否发送了电荷泵使能命令（0x8D 0x14）
3. 确认是否发送了开显示命令（0xAF）
4. 检查供电是否正常（3.3V/5V）

### 9.2 显示乱码

**原因**：
- 地址设置错误
- 数据格式错误
- 字库不匹配

**解决**：
- 检查控制字节（0x00命令，0x40数据）
- 检查地址模式设置

### 9.3 显示反了/镜像

**解决**：
```c
// 水平翻转
OLED_WriteCommand(0xA1);  // 替代 0xA0

// 垂直翻转
OLED_WriteCommand(0xC8);  // 替代 0xC0
```

### 9.4 对比度不够

```c
// 提高对比度
OLED_WriteCommand(0x81);  // 设置对比度命令
OLED_WriteCommand(0xFF);  // 最大值（0x00~0xFF）
```

---

## 十、完整使用示例

```c
#include "stm32f10x.h"

#define OLED_ADDR   0x78   // 7位地址0x3C << 1

int main(void)
{
    // 初始化I2C
    I2C1_Init();
    
    // 初始化OLED
    OLED_Init();
    
    // 清屏
    OLED_Clear();
    
    // 显示字符串
    OLED_ShowString(0, 0, "Hello World!");
    OLED_ShowString(0, 2, "SSD1306 OLED");
    
    // 读取状态
    uint8_t status = OLED_ReadStatus();
    if((status & 0x40) == 0)
    {
        // 屏幕是开的
    }
    
    while(1);
}
```

---

## 十一、总结

### 11.1 关键要点

1. **I2C地址**：默认 0x3C（7位），写操作时发送 0x78
2. **控制字节**：命令用 0x00，数据用 0x40
3. **必须使能电荷泵**：0x8D 0x14，否则屏幕不亮
4. **GDDRAM结构**：128列×8页（64行），每字节控制8个垂直像素
5. **地址模式**：常用页地址模式（0x20 0x02）

### 11.2 初始化命令记忆口诀

> **关显示 → 设时钟 → 设多路 → 设偏移 → 设起始行 → 开电荷泵 → 设地址模式 → 设映射 → 设引脚 → 设对比度 → 设预充电 → 设VCOMH → 开显示 → 显示开命令**

### 11.3 常用API速查

| 功能 | 函数 |
|------|------|
| 初始化 | `OLED_Init()` |
| 清屏 | `OLED_Clear()` |
| 开/关显示 | `OLED_DisplayOn()` / `OLED_DisplayOff()` |
| 设置光标 | `OLED_SetCursor(x, y)` |
| 显示字符 | `OLED_ShowChar(x, y, chr)` |
| 显示字符串 | `OLED_ShowString(x, y, str)` |
| 显示图片 | `OLED_DrawBMP(bmp)` |
| 读取状态 | `OLED_ReadStatus()` |
