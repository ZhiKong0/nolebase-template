# IIC读数据详解（主机接收）

## 一、IIC读数据的特殊性

### 1.1 为什么读数据比写数据复杂

相比写数据，读数据有两个额外的复杂性：

1. **ACK由主机控制**：写数据时从机回ACK；读数据时主机要回ACK/NACK
2. **字节数不同处理不同**：STM32硬件要求单字节、双字节、多字节（≥3）分别处理

### 1.2 STM32 I2C接收的三种情况

| 接收字节数 | 特殊要求 | 关键区别 |
|------------|----------|----------|
| **1字节** | **必须在清除ADDR前禁用ACK和发送STOP** | 单字节时时机要求最严格 |
| **2字节** | **必须使用POS位控制NACK时机** | 用POS位让NACK只作用于第2字节 |
| **≥3字节** | 标准流程 | 先收N-1字节（回ACK），最后1字节（回NACK+STOP） |

### 1.3 为什么单/双字节特殊？

这是 STM32 I2C 硬件的设计特性：

- **移位寄存器和DR双缓冲**：收到数据先进入移位寄存器，再移入DR
- **单字节**：如果不清ADDR前就收数据，硬件自动回ACK，从机会发第2字节
- **双字节**：需要第1字节回ACK，第2字节回NACK，用POS位控制

---

## 二、完整代码实现

### 2.1 完整接收函数

```c
/**
 * @brief  I2C 主机接收数据（支持1/2/多字节）
 * @param  I2Cx: I2C 外设
 * @param  Addr: 从机地址（8位格式，bit0=1 表示读）
 * @param  pBuffer: 接收缓冲区
 * @param  Size: 要接收的数据长度
 * @retval 0: 成功，-1: 寻址失败
 */
int I2C_ReceiveBytes(I2C_TypeDef *I2Cx, uint8_t Addr, uint8_t *pBuffer, uint16_t Size)
{
    uint16_t i;
    
    /* ========== 阶段1：发送起始信号 ========== */
    I2C_GenerateSTART(I2Cx, ENABLE);
    while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_SB) == RESET);
    
    /* ========== 阶段2：发送从机地址（读） ========== */
    I2C_ClearFlag(I2Cx, I2C_FLAG_AF);
    I2C_SendData(I2Cx, Addr | 0x01);  // bit0=1 表示读操作
    
    // 等待从机应答
    while(1)
    {
        if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET)
        {
            I2C_GenerateSTOP(I2Cx, ENABLE);
            return -1;  // 寻址失败
        }
        if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_ADDR) == SET)
            break;  // 寻址成功
    }
    
    /* ========== 阶段3：根据字节数分别处理 ========== */
    
    if(Size == 1)
    {
        /* ===== 单字节接收：最特殊的处理 ===== */
        // ① 先禁用ACK（准备回NACK，而不是ACK）
        I2C_AcknowledgeConfig(I2Cx, DISABLE);
        
        // ② 立即发送STOP信号
        // 注：单字节时必须在接收前设好STOP，否则从机可能多发数据
        I2C_GenerateSTOP(I2Cx, ENABLE);
        
        // ③ 清除ADDR标志（读取SR1）
        I2C_ReadRegister(I2Cx, I2C_Register_SR1);
        
        // ④ 继续读SR2完成清除，同时触发数据接收
        // 此时从机收到地址+读命令，开始发送第1字节
        // 由于已设NACK，从机发送完1字节后收到NACK自动停止
        I2C_ReadRegister(I2Cx, I2C_Register_SR2);
        
        // ⑤ 等待RXNE=1（数据已存入DR寄存器）
        while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);
        
        // ⑥ 读取唯一的1个字节
        pBuffer[0] = I2C_ReceiveData(I2Cx);
    }
    else if(Size == 2)
    {
        /* ===== 双字节接收：使用POS位 ===== */
        // ① 先使能ACK（第1字节要回ACK）
        I2C_AcknowledgeConfig(I2Cx, ENABLE);
        
        // ② 设POS=1，NACK只对"下一个"字节生效（即第2字节）
        // POS位控制NACK作用于当前字节还是下一个字节
        I2C_NACKPositionConfig(I2Cx, I2C_NACKPosition_Next);
        
        // ③ 清除ADDR标志（开始接收第1字节）
        I2C_ReadRegister(I2Cx, I2C_Register_SR1);
        I2C_ReadRegister(I2Cx, I2C_Register_SR2);
        // 读SR2完成清除，第1字节开始传输
        
        // ④ 等待BTF=1（Byte Transfer Finished）
        // BTF=1表示DR和移位寄存器都有数据（2字节都到了）
        // 此时第1字节在DR，第2字节在移位寄存器，从机暂停等待应答
        while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTF) == RESET);
        
        // ⑤ 禁用ACK（对第2字节回NACK）
        I2C_AcknowledgeConfig(I2Cx, DISABLE);
        
        // ⑥ 发送STOP（结束通信）
        // 此时从机正准备发第2字节的ACK位，收到NACK后停止
        I2C_GenerateSTOP(I2Cx, ENABLE);
        
        // ⑦ 读第1字节（DR变空，第2字节从移位寄存器移入DR）
        pBuffer[0] = I2C_ReceiveData(I2Cx);
        
        // ⑧ 等待第2字节移入DR（RXNE=1）
        while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);
        
        // ⑨ 读第2字节（最后一个字节）
        pBuffer[1] = I2C_ReceiveData(I2Cx);
    }
    else
    {
        /* ===== 3字节及以上：标准接收流程 ===== */
        // ① 清除ADDR标志（开始接收第1字节）
        I2C_ReadRegister(I2Cx, I2C_Register_SR1);
        I2C_ReadRegister(I2Cx, I2C_Register_SR2);
        // 读SR2完成清除，从机开始发送数据
        
        // ② 接收前 Size-1 个字节（发ACK）
        for(i = 0; i < Size - 1; i++)
        {
            // 等待当前字节到达（RXNE=1）
            while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);
            
            // 读取数据（DR变空，硬件自动准备接收下一字节）
            pBuffer[i] = I2C_ReceiveData(I2Cx);
            
            // 发送ACK（表示"收到了，请继续发下一个"）
            // ACK在当前字节传输的9th时钟位发出
            I2C_AcknowledgeConfig(I2Cx, ENABLE);
        }
        
        // ③ 最后一个字节：发NACK + STOP，然后读取
        // 禁用ACK（准备回NACK）
        I2C_AcknowledgeConfig(I2Cx, DISABLE);
        
        // 发送STOP（通知从机停止发送）
        // STOP在总线空闲时生效，当前字节传输完成后才生效
        I2C_GenerateSTOP(I2Cx, ENABLE);
        
        // 等待最后一个字节到达
        while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);
        
        // 读取最后一个字节（同时从机收到NACK）
        pBuffer[Size - 1] = I2C_ReceiveData(I2Cx);
    }
    
    return 0;
}
```

---

## 三、单字节接收详解

### 3.1 为什么单字节最特殊？

**STM32硬件特性**：
- 收到地址后，硬件自动准备接收数据
- 如果ACK使能，收到数据后硬件自动回ACK，从机继续发下一字节
- 单字节接收必须在**收到数据前就禁用ACK**，否则从机会发第2字节

### 3.2 单字节时序图

```
步骤:    ①关ACK②发STOP  ③清ADDR          ④等RXNE   ⑤读数据
          ↓    ↓        ↓                 ↓         ↓
     SDA   ─┐    │    ┌───┐              ┌───┐     ┌───┐
           └───┐└───┘   └───────────────│   ├─────┤   │
                 ↑     从机发数据        ↑       ↑
     SCL   ────────┐   ┌───┐   ┌───┐   ┌───┐   ┌───┐
                   └───┘   └───┘   └───┘   └───┘   └───
ADDR=0, ACK=0, STOP=1            DR有数据    读取DR
```

### 3.3 关键顺序

```c
// 正确顺序：必须在清ADDR前就准备好NACK和STOP
I2C_AcknowledgeConfig(I2Cx, DISABLE);   // ① 关ACK
I2C_GenerateSTOP(I2Cx, ENABLE);          // ② 发STOP
I2C_ReadRegister(I2Cx, I2C_Register_SR1); // ③ 清ADDR
I2C_ReadRegister(I2Cx, I2C_Register_SR2);
```

**为什么这样？**
- 清ADDR后，硬件立即开始接收数据
- 必须在接收前告诉硬件：只收1字节，收到后回NACK+STOP
- 如果先清ADDR再关ACK，从机已经开始发第2字节了

---

## 四、双字节接收详解

### 4.1 POS位的作用

POS（Packet Error Checking / NACK Position）位控制 NACK 的应用时机：

| POS值 | NACK作用于 |
|-------|------------|
| 0（Current） | 当前正在接收的字节 |
| 1（Next） | 下一个要接收的字节 |

**双字节场景**：
- 第1字节需要回 ACK（让从机继续发）
- 第2字节需要回 NACK（告诉从机停止）
- 设POS=1，使NACK只对"下一个"字节（第2字节）生效

### 4.2 双字节时序图

```
步骤:    ①开ACK②设POS  ③清ADDR      ④等BTF    ⑤关ACK⑥发STOP
          ↓   ↓      ↓            ↓         ↓    ↓
     SDA  ┌─┐ │ ┌────┐    ┌──────┐└───┐ ┌───┐   ┌───┐
          └─┘ └─┘    └────┘      └────┘   └─┘ └─┘ └─┘
                ↑                 ↑             ↑
              第1字节           第2字节也到了  读第1字节
     SCL  ─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐
           └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─
                    
DR:        空  →  第1字节  →  第2字节
移位器:              空   →  第1字节  →  第2字节

BTF=1时：DR=第1字节，移位器=第2字节
```

### 4.3 BTF标志的含义

**BTF（Byte Transfer Finished）= 1 表示**：
- 数据寄存器（DR）已满
- 移位寄存器也满（下一字节正在接收中）
- **双字节时**：表示2字节都已到达

此时读取DR，第2字节自动从移位器移入DR。

---

## 五、多字节（≥3）接收详解

### 5.1 标准流程

```c
// 清ADDR后开始接收
I2C_ReadRegister(I2Cx, I2C_Register_SR1);
I2C_ReadRegister(I2Cx, I2C_Register_SR2);

// 循环接收前N-1字节，每字节回ACK
for(i = 0; i < Size - 1; i++)
{
    while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);
    pBuffer[i] = I2C_ReceiveData(I2Cx);
    I2C_AcknowledgeConfig(I2Cx, ENABLE);  // 回ACK
}

// 最后1字节：先关ACK发STOP，再读取
I2C_AcknowledgeConfig(I2Cx, DISABLE);
I2C_GenerateSTOP(I2Cx, ENABLE);
while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);
pBuffer[Size - 1] = I2C_ReceiveData(I2Cx);
```

### 5.2 ACK的发送时机

**重要**：ACK不是在读取数据后发送，而是在**接收下一字节时自动发送**。

```
时序:  字节N-1    ACK周期    字节N      NACK周期
      ┌───────┐   ┌───┐   ┌───────┐    ┌───┐
SDA   │D7..D0 │   │ACK│   │D7..D0 │    │NACK│
      └───┬───┘   └─┬─┘   └───┬───┘    └─┬─┘
          │         │         │          │
SCL   ────┴────┬────┴────┬────┴────┬─────┴────
              第9时钟   第9时钟   第9时钟

读取DR使DR变空，从机收到ACK继续发
```

---

## 六、RXNE 和 BTF 的区别

| 标志 | 含义 | 触发条件 | 使用场景 |
|------|------|----------|----------|
| **RXNE** | Receive Not Empty<br>接收寄存器非空 | DR中有可读数据 | 每字节读取前等待 |
| **BTF** | Byte Transfer Finished<br>字节传输完成 | DR和移位器都满 | 双字节接收判断时机 |

### 6.1 单字节接收用 RXNE

```c
// 单字节：只需等数据到达DR
while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);
pBuffer[0] = I2C_ReceiveData(I2Cx);
```

### 6.2 双字节接收用 BTF

```c
// 双字节：等2字节都到（BTF=1表示DR和移位器都有数据）
while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTF) == RESET);
pBuffer[0] = I2C_ReceiveData(I2Cx);  // 读第1字节，第2字节自动移入DR
while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);
pBuffer[1] = I2C_ReceiveData(I2Cx);  // 读第2字节
```

---

## 七、常见问题排查

### 7.1 只收到部分数据

**现象**：应该收3字节，只收到1或2字节

**可能原因**：
- ACK/NACK控制不当，从机提前停止
- 读取时机不对，数据被覆盖

**排查**：
- 检查ACK是否使能（多字节时）
- 检查最后一次是否发了NACK+STOP

### 7.2 死锁在RXNE等待

**现象**：程序卡在 `while(RXNE == RESET)`

**可能原因**：
- 从机未发送数据
- 通信已异常终止

**解决**：加超时检测

```c
uint32_t timeout = 10000;
while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET)
{
    if(--timeout == 0) return ERROR;
}
```

### 7.3 收到错误数据

**可能原因**：
- 地址错误，实际是其他从机在回应
- 总线干扰
- 上拉电阻不合适

---

## 八、总结

### 8.1 三种接收方式对比表

| 字节数 | 关键操作 | ACK/STOP时机 | 特殊标志 |
|--------|----------|--------------|----------|
| **1字节** | 先关ACK+STOP，再清ADDR | 接收前设置 | RXNE |
| **2字节** | 用POS位，使NACK只对第2字节 | BTF=1后关ACK+STOP，再读 | BTF, RXNE |
| **≥3字节** | 先收N-1字节（ACK），最后1字节（NACK+STOP） | 最后字节前关ACK+STOP | RXNE |

### 8.2 关键记忆口诀

> **单字节**：先关ACK发STOP，再清ADDR等RXNE<br>
> **双字节**：开ACK设POS清ADDR，等BTF关ACK读数据<br>
> **多字节**：清ADDR循环收，最后关ACK发STOP

### 8.3 最重要的三点

1. **单字节必须在清ADDR前关ACK+STOP** —— 否则从机多发
2. **双字节必须用POS位** —— 精确控制NACK时机
3. **多字节最后要先关ACK再发STOP** —— 正确结束通信
