# IIC使用方法详解（STM32硬件IIC）

## 一、IIC 外设初始化流程

### 1.1 完整初始化步骤

```c
void I2C1_Init(void)
{
    /* 步骤1：开启外设时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);   // GPIOB时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);  // AFIO时钟（用于重映射）
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);  // I2C1时钟（在APB1总线）
    
    /* 步骤2：配置GPIO引脚 */
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;     // PB6=SCL, PB7=SDA
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_OD;          // 复用开漏输出！
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    /* 步骤3：复位I2C外设（清除异常状态） */
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, DISABLE);
    
    /* 步骤4：配置I2C工作参数 */
    I2C_InitTypeDef I2C_InitStruct;
    I2C_InitStruct.I2C_ClockSpeed = 400000;               // 400kHz（快速模式）
    I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;               // 标准I2C模式
    I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;     // 占空比2:1
    I2C_InitStruct.I2C_OwnAddress1 = 0x00;               // 主机模式设为0
    I2C_InitStruct.I2C_Ack = I2C_Ack_Enable;             // 使能ACK
    I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    
    I2C_Init(I2C1, &I2C_InitStruct);                     // 写入配置
    
    /* 步骤5：使能I2C外设 */
    I2C_Cmd(I2C1, ENABLE);
}
```

### 1.2 配置要点说明

| 配置项 | 常用设置 | 说明 |
|--------|----------|------|
| `GPIO_Mode` | `GPIO_Mode_AF_OD` | **必须用复用开漏输出！** |
| `I2C_ClockSpeed` | 100000 / 400000 | 标准模式100kHz，快速模式400kHz |
| `I2C_DutyCycle` | `I2C_DutyCycle_2` | 快速模式下占空比，2:1或16:9 |
| `I2C_OwnAddress1` | 0x00 | 主机模式设为0，从机模式设自己的地址 |
| `I2C_Ack` | `I2C_Ack_Enable` | 接收数据时需要回应ACK |

### 1.3 引脚重映射（Remap）

STM32F103 的 I2C1 默认引脚是 PB6/PB7，可以重映射到 PB8/PB9：

```c
/* 在GPIO配置前调用重映射 */
GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);

/* 然后配置 PB8/PB9 */
GPIO_InitStruct.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
```

**适用场景**：PB6/PB7 被其他功能占用时

---

## 二、IIC 常用库函数

### 2.1 核心操作函数

```c
/* 生成起始信号 */
void I2C_GenerateSTART(I2C_TypeDef* I2Cx, FunctionalState NewState);

/* 生成停止信号 */
void I2C_GenerateSTOP(I2C_TypeDef* I2Cx, FunctionalState NewState);

/* 发送数据 */
void I2C_SendData(I2C_TypeDef* I2Cx, uint8_t Data);

/* 接收数据 */
uint8_t I2C_ReceiveData(I2C_TypeDef* I2Cx);

/* 发送7位地址 */
void I2C_Send7bitAddress(I2C_TypeDef* I2Cx, uint8_t Address, uint8_t I2C_Direction);

/* 使能/禁用ACK */
void I2C_AcknowledgeConfig(I2C_TypeDef* I2Cx, FunctionalState NewState);

/* 配置POS位（双字节接收用） */
void I2C_NACKPositionConfig(I2C_TypeDef* I2Cx, uint16_t I2C_NACKPosition);
```

### 2.2 状态标志检查函数

```c
/* 获取标志位状态 */
FlagStatus I2C_GetFlagStatus(I2C_TypeDef* I2Cx, uint32_t I2C_FLAG);

/* 清除标志位 */
void I2C_ClearFlag(I2C_TypeDef* I2Cx, uint32_t I2C_FLAG);

/* 读取寄存器（用于清除ADDR标志） */
uint16_t I2C_ReadRegister(I2C_TypeDef* I2Cx, uint8_t I2C_Register);
```

### 2.3 常用标志位说明

| 标志位 | 含义 | 触发条件 | 清除方式 |
|--------|------|----------|----------|
| `I2C_FLAG_SB` | 起始位已发送 | Start信号发送完成 | 读SR1 + 写DR |
| `I2C_FLAG_ADDR` | 地址已发送 | 地址发送并收到ACK | 读SR1 + 读SR2 |
| `I2C_FLAG_BTF` | 字节传输完成 | 1字节传输完成 | 读DR 或 写DR |
| `I2C_FLAG_RXNE` | 接收寄存器非空 | DR中有数据可读 | 读DR |
| `I2C_FLAG_TXE` | 发送寄存器空 | DR可写入新数据 | 写DR |
| `I2C_FLAG_AF` | 应答失败 | 未收到从机ACK | 软件写0清除 |
| `I2C_FLAG_BUSY` | 总线忙 | 总线正在通信 | 收到Stop后自动清除 |

---

## 三、IIC 通信状态机

STM32 的 I2C 外设内部有一个状态机，主程序需要配合状态机操作：

```
          ┌──────────┐
          │  空闲    │ ←────── 初始状态
          │ (IDLE)   │
          └────┬─────┘
               │ 发送Start
               ▼
          ┌──────────┐
          │ 起始发送 │ ←── SB=1
          │(SB_SENT) │
          └────┬─────┘
               │ 发送地址
               ▼
          ┌──────────┐
          │ 地址发送 │ ←── ADDR=1
          │(ADDR_SENT)│
          └────┬─────┘
               │
        ┌──────┴──────┐
        ▼             ▼
   ┌─────────┐  ┌─────────┐
   │发送模式 │  │接收模式 │
   │(TX)     │  │(RX)     │
   └────┬────┘  └────┬────┘
        │            │
        ▼            ▼
   ┌─────────┐  ┌─────────┐
   │字节传输 │  │字节接收 │
   │(BTF/TXE)│  │(RXNE)   │
   └────┬────┘  └────┬────┘
        │            │
        ▼            ▼
   ┌─────────┐  ┌─────────┐
   │  停止   │  │  停止   │
   │(Stop)   │  │(Stop)   │
   └────┬────┘  └────┬────┘
        └────────────┘
               │
               ▼
          ┌──────────┐
          │  返回    │
          │  空闲    │
          └──────────┘
```

---

## 四、关键注意事项

### 4.1 清除 ADDR 标志

**这是最容易出错的地方！**

当 `ADDR=1` 时必须立即清除，方法是：**先读 SR1，再读 SR2**

```c
/* 正确做法 */
I2C_ReadRegister(I2Cx, I2C_Register_SR1);  // 读SR1
I2C_ReadRegister(I2Cx, I2C_Register_SR2);  // 读SR2
/* 此时 ADDR 自动清除 */

/* 错误做法 */
I2C_ClearFlag(I2Cx, I2C_FLAG_ADDR);  // 不要用这个！
```

### 4.2 ACK 信号的控制时机

**发送模式**：
- 主机发送数据，从机在 ACK 周期回应
- 无需主机主动控制 ACK

**接收模式**：
- 主机必须在接收前配置 ACK
- 最后一个字节前要禁用 ACK（发送 NACK）

```c
/* 接收多个字节时 */
I2C_AcknowledgeConfig(I2Cx, ENABLE);  // 接收前使能ACK

/* ... 接收数据 ... */

/* 最后一个字节前 */
I2C_AcknowledgeConfig(I2Cx, DISABLE);  // 禁用ACK，准备发NACK
I2C_GenerateSTOP(I2Cx, ENABLE);        // 发送Stop
```

### 4.3 等待标志位的写法

**阻塞式等待（简单场景）**：
```c
while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_SB) == RESET);
/* 等待直到标志置位，注意可能死循环！ */
```

**带超时的等待（推荐）**：
```c
uint32_t timeout = 10000;
while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_SB) == RESET)
{
    if(--timeout == 0) return ERROR;  // 超时处理
}
```

---

## 五、常见问题排查

### 5.1 总线一直 BUSY

**现象**：`I2C_FLAG_BUSY` 一直为 1

**原因**：
1. 上拉电阻未接或虚焊
2. 从机未响应，总线挂死
3. 前一次通信异常终止

**解决**：
```c
/* 1. 检查硬件连接 */
/* 2. 发送时钟脉冲解除死锁 */
void I2C_BusReset(I2C_TypeDef* I2Cx)
{
    /* 切换到GPIO模式，发送9个时钟 */
    for(int i=0; i<9; i++)
    {
        SCL_L(); Delay_us(5);
        SCL_H(); Delay_us(5);
    }
    /* 发送Stop条件 */
    I2C_GenerateSTOP(I2Cx, ENABLE);
}
```

### 5.2 总是收到 AF（Acknowledge Failure）

**现象**：`I2C_FLAG_AF` 置位

**原因**：
1. 从机地址错误
2. 从机未上电或损坏
3. 总线冲突

**排查**：
- 用示波器检查波形
- 确认从机地址（注意7位地址 vs 8位地址+RW位）

### 5.3 发送数据无响应

**排查步骤**：
1. 检查起始信号是否发出（SB=1）
2. 检查地址是否被响应（ADDR=1）
3. 检查发送缓冲区是否空（TXE=1）
4. 检查是否有AF标志

---

## 六、总结

使用 STM32 硬件 I2C 的关键步骤：

1. **初始化**：GPIO时钟 → GPIO配置（复用开漏）→ I2C时钟 → 复位 → 配置参数 → 使能
2. **发送数据**：Start → 等SB → 发地址 → 等ADDR → 清ADDR → 发数据 → 等TXE/BTF → Stop
3. **接收数据**：Start → 等SB → 发地址 → 等ADDR → 清ADDR → 等RXNE → 读数据 → ACK/NACK → Stop

**最重要的三条**：
1. **GPIO必须用复用开漏输出**
2. **收到ADDR后必须读SR1+SR2清除**
3. **接收时最后一个字节前要禁ACK发Stop**
