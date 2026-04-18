# IIC写数据详解（主机发送）

## 一、IIC写数据流程概述

### 1.1 完整通信时序

主机向从机写入数据的完整流程：

```
主机:  START  │  地址+W  │    │ 数据1 │    │ 数据2 │    │ ... │ 数据N │    │ STOP
       ─┬─     └─────────┘    └───────┘    └───────┘          └───────┘     ─┬─
         │         ↑              ↑              ↑                ↑         │
从机:    │      ACK(0)        ACK(0)         ACK(0)           ACK(0)       │
              "我在"           "收到"          "继续"            "完成"
```

### 1.2 代码流程图

```
┌─────────┐
│  开始   │
└────┬────┘
     ▼
┌─────────────┐
│ 等总线空闲   │ ← 检查BUSY标志
│  (BUSY=0)   │
└────┬────────┘
     ▼
┌─────────────┐
│  发送START   │
└────┬────────┘
     ▼
┌─────────────┐
│  等SB=1     │ ← 起始信号发送完成
└────┬────────┘
     ▼
┌─────────────┐
│  发送地址    │
│  (Addr+W)   │
└────┬────────┘
     ▼
┌─────────────┐     ┌──────────┐
│  等ADDR=1   │─NO──┤  AF=1?   │─YES→ 发送STOP，返回错误
│             │     └──────────┘
└────┬────────┘
     │YES
     ▼
┌─────────────┐
│  读SR1+SR2   │ ← 清除ADDR标志
└────┬────────┘
     ▼
┌─────────────┐
│  等TXE=1    │ ← 发送寄存器空
└────┬────────┘
     ▼
┌─────────────┐
│  发送数据    │
└────┬────────┘
     ▼
┌─────────────┐     ┌──────────┐
│  等ACK      │─NO──┤  AF=1?   │─YES→ 发送STOP，返回错误
│  (AF=0)     │     └──────────┘
└────┬────────┘
     │YES
     ▼
┌─────────────┐
│  还有数据?   │─YES→ 返回"等TXE=1"继续发送
└────┬────────┘
     │NO
     ▼
┌─────────────┐
│  等BTF=1    │ ← 最后一字节传输完成
└────┬────────┘
     ▼
┌─────────────┐
│  发送STOP    │
└────┬────────┘
     ▼
┌─────────────┐
│   返回OK    │
└─────────────┘
```

---

## 二、完整代码实现

### 2.1 基础发送函数

```c
/**
 * @brief  I2C 主机发送多字节数据
 * @param  I2Cx: I2C 外设（I2C1 或 I2C2）
 * @param  Addr: 从机地址（8位格式，已包含读写位，bit0=0 表示写）
 * @param  pData: 要发送的数据缓冲区
 * @param  Size: 数据长度（字节数）
 * @retval 0: 发送成功，-1: 寻址失败，-2: 数据拒收
 */
int I2C_SendBytes(I2C_TypeDef *I2Cx, uint8_t Addr, uint8_t *pData, uint16_t Size)
{
    /* ========== 阶段1：等待总线空闲 ========== */
    // I2C_FLAG_BUSY 表示总线正在通信中
    // 必须等待空闲才能发起新通信，否则可能冲突
    while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSY) == SET);
    
    /* ========== 阶段2：发送起始信号 ========== */
    // 起始信号：SCL=1 时，SDA 从高电平跳变到低电平
    // 通知所有从机，主机即将发起通信
    I2C_GenerateSTART(I2Cx, ENABLE);
    
    // I2C_FLAG_SB（Start Bit）起始位发送完成标志
    // 必须等待起始信号发送完毕，才能发送地址
    while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_SB) == RESET);
    
    /* ========== 阶段3：发送从机地址 ========== */
    // 清除 AF（Acknowledge Failure）标志
    // 避免之前的失败影响当前通信
    I2C_ClearFlag(I2Cx, I2C_FLAG_AF);
    
    // 发送地址：Addr & 0xFE 确保 bit0 为 0（写操作）
    // 地址格式：7位从机地址 + 1位读写标志（0=写，1=读）
    // 例如从机地址 0x3C，则发送 0x78（0x3C << 1）
    I2C_SendData(I2Cx, Addr & 0xFE);
    
    // 等待从机应答
    // 有两种可能结果：
    // 1. I2C_FLAG_ADDR=1：地址匹配成功，从机已应答
    // 2. I2C_FLAG_AF=1：无应答，从机不存在或地址错误
    while(1)
    {
        if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_ADDR) == SET) 
            break;                          // 寻址成功，退出等待
        
        if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET)  // 寻址失败
        {
            I2C_GenerateSTOP(I2Cx, ENABLE); // 发送停止信号，释放总线
            return -1;                      // 返回错误码：寻址失败
        }
    }
    
    /* ========== 阶段4：清除 ADDR 标志 ========== */
    // 必须通过读取 SR1 和 SR2 寄存器来清除 ADDR 标志
    // 这是 STM32 I2C 硬件要求，不清除会导致总线锁定
    I2C_ReadRegister(I2Cx, I2C_Register_SR1);
    I2C_ReadRegister(I2Cx, I2C_Register_SR2);
    
    /* ========== 阶段5：循环发送数据 ========== */
    for(uint16_t i = 0; i < Size; i++)
    {
        // 等待发送缓冲区空闲
        // I2C_FLAG_TXE（Transmit Empty）发送寄存器空标志
        // 表示可以写入下一个字节
        while(1)
        {
            if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET)  // 检测应答失败
            {
                I2C_GenerateSTOP(I2Cx, ENABLE);
                return -2;                  // 返回错误码：数据拒收
            }
            if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_TXE) == SET)
                break;                      // 发送缓冲区已空，可以发送
        }
        
        // 将数据写入数据寄存器，硬件自动发送
        I2C_SendData(I2Cx, pData[i]);
    }
    
    /* ========== 阶段6：等待发送完成 ========== */
    // I2C_FLAG_BTF（Byte Transfer Finished）字节传输完成标志
    // 表示最后一个字节已发送且收到应答
    while(1)
    {
        if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET)
        {
            I2C_GenerateSTOP(I2Cx, ENABLE);
            return -2;                      // 返回错误码：数据拒收
        }
        if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTF) == SET)
            break;                          // 字节传输完成
    }
    
    /* ========== 阶段7：发送停止信号 ========== */
    // 停止信号：SCL=1 时，SDA 从低电平跳变到高电平
    I2C_GenerateSTOP(I2Cx, ENABLE);
    
    return 0;  // 发送成功
}
```

---

## 三、关键时序详解

### 3.1 起始信号阶段

```c
I2C_GenerateSTART(I2Cx, ENABLE);
while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_SB) == RESET);
```

**时序说明**：
```
     SDA        ┌────────
     ────┐      │
         └──────┘
     SCL   ───────────────
              ↑
           SB=1（起始信号已发送）
```

**注意**：
- SB 标志硬件自动置位
- 必须在 SB=1 后才能写 DR 寄存器发送地址

### 3.2 地址发送阶段

```c
I2C_SendData(I2Cx, Addr & 0xFE);
while(1)
{
    if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_ADDR) == SET) break;
    if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET) { /* 错误处理 */ }
}
```

**时序说明**：
```
主机发送：  地址(7位) + W(0)
            ├───────┤├─┤
从机回应：              ACK(0)
                        └┘
                        ↑
                     ADDR=1
```

**重要**：
- 地址发送后，第9个时钟周期从机拉低 SDA 表示 ACK
- ADDR 标志表示"地址已发送且收到 ACK"
- AF 标志表示"地址发送后收到 NACK"

### 3.3 清除 ADDR 标志

```c
I2C_ReadRegister(I2Cx, I2C_Register_SR1);
I2C_ReadRegister(I2Cx, I2C_Register_SR2);
```

**为什么要这样清除？**

这是 STM32 I2C 外设的硬件设计：
- ADDR 位在 SR1 寄存器
- 读取 SR1 后再读取 SR2，硬件自动清除 ADDR
- 如果不清除，总线会保持占用状态

### 3.4 数据发送阶段

```c
while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_TXE) == RESET);
I2C_SendData(I2Cx, pData[i]);
```

**时序说明**：
```
        TXE=1（可写数据）  数据移入移位寄存器   发送中     TXE=1（可写下一字节）
             ↑                │           │            ↑
             │    ┌───────┐   │    ┌───┐  │    ┌───┐  │
主机:        └────┤ Data1 ├───────┤   ├───┘    │   ├───┘
                  └───────┘       └───┘        └───┘
从机回应:                               ACK(0)      ACK(0)
                                        └┘          └┘
```

**标志位含义**：
- TXE=1：数据寄存器空，可以写入新数据
- BTF=1：字节传输完成（移位寄存器也空）

---

## 四、错误处理详解

### 4.1 寻址失败（AF=1）

```c
if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET)
{
    I2C_GenerateSTOP(I2Cx, ENABLE);  // 必须发STOP释放总线！
    return -1;                       // 返回：寻址失败
}
```

**可能原因**：
| 原因 | 排查方法 |
|------|----------|
| 地址错误 | 确认从机7位地址，左移1位得到8位地址 |
| 从机未上电 | 检查从机供电 |
| 上拉电阻问题 | 检查SDA/SCL电平 |
| 从机忙 | 检查从机是否正被其他主机访问 |

### 4.2 数据拒收（发送过程中 AF=1）

```c
if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET)
{
    I2C_GenerateSTOP(I2Cx, ENABLE);
    return -2;  // 返回：数据拒收
}
```

**可能原因**：
- 从机缓冲区满
- 从机进入错误状态
- 通信过程中受到干扰

### 4.3 总线忙（BUSY=1）

**现象**：等待 BUSY 清零超时

**原因**：
- 上一次通信未正常结束（没有收到STOP）
- 其他主机正在占用总线

**解决方法**：
```c
/* 发送时钟脉冲解除死锁 */
void I2C_UnlockBus(I2C_TypeDef* I2Cx)
{
    /* 1. 禁用I2C外设 */
    I2C_Cmd(I2Cx, DISABLE);
    
    /* 2. 将SCL和SDA配置为GPIO开漏输出 */
    /* ... GPIO配置代码 ... */
    
    /* 3. 发送9个SCL时钟脉冲 */
    for(int i = 0; i < 9; i++)
    {
        GPIO_ResetBits(GPIOB, GPIO_Pin_6);  // SCL = 0
        Delay_us(5);
        GPIO_SetBits(GPIOB, GPIO_Pin_6);    // SCL = 1
        Delay_us(5);
    }
    
    /* 4. 发送Stop条件 */
    GPIO_ResetBits(GPIOB, GPIO_Pin_7);      // SDA = 0
    Delay_us(5);
    GPIO_SetBits(GPIOB, GPIO_Pin_6);        // SCL = 1
    Delay_us(5);
    GPIO_SetBits(GPIOB, GPIO_Pin_7);        // SDA = 1 (Stop)
    
    /* 5. 重新初始化I2C */
    I2C_Init(...);
}
```

---

## 五、发送单个字节的简化版

如果只需要发送1个字节，可以简化代码：

```c
/**
 * @brief  发送单字节（简化版，不检查ACK）
 * @param  Addr: 从机地址
 * @param  Data: 要发送的数据
 */
void I2C_SendByte(I2C_TypeDef *I2Cx, uint8_t Addr, uint8_t Data)
{
    // 等待空闲
    while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSY) == SET);
    
    // 起始信号
    I2C_GenerateSTART(I2Cx, ENABLE);
    while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_SB) == RESET);
    
    // 发送地址
    I2C_SendData(I2Cx, Addr & 0xFE);
    while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_ADDR) == RESET);
    
    // 清除ADDR
    I2C_ReadRegister(I2Cx, I2C_Register_SR1);
    I2C_ReadRegister(I2Cx, I2C_Register_SR2);
    
    // 发送数据
    I2C_SendData(I2Cx, Data);
    while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTF) == RESET);
    
    // 停止
    I2C_GenerateSTOP(I2Cx, ENABLE);
}
```

---

## 六、总结

IIC 写数据的关键步骤记忆口诀：

> **起始等SB，发地址等ADDR，清标志读SR，发数据等TXE，完成等BTF，最后发STOP。**

关键要点：
1. **BUSY检查**：发起通信前必须确认总线空闲
2. **SB等待**：起始信号发送完成才能发地址
3. **ADDR检测**：检测从机是否响应
4. **双读清ADDR**：必须读SR1再读SR2清除ADDR
5. **TXE等待**：发送寄存器空才能写入新数据
6. **BTF等待**：最后一字节传输完成才能发STOP
7. **AF检测**：全程检测AF，出现则发STOP并返回错误
