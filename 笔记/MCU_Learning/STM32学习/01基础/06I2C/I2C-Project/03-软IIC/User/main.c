#include "stm32f10x.h"
#include "Delay.h"

/* ==================== 软件 I2C 引脚定义 ==================== */
// 使用 PB10 作为 SCL，PB11 作为 SDA（可改为任意 GPIO）
#define I2C_SCL_PORT    GPIOB
#define I2C_SDA_PORT    GPIOB
#define I2C_SCL_PIN     GPIO_Pin_10
#define I2C_SDA_PIN     GPIO_Pin_11

// 引脚操作宏（开漏输出模式）
#define SCL_H()         GPIO_SetBits(I2C_SCL_PORT, I2C_SCL_PIN)
#define SCL_L()         GPIO_ResetBits(I2C_SCL_PORT, I2C_SCL_PIN)
#define SDA_H()         GPIO_SetBits(I2C_SDA_PORT, I2C_SDA_PIN)
#define SDA_L()         GPIO_ResetBits(I2C_SDA_PORT, I2C_SDA_PIN)
#define SCL_READ()      GPIO_ReadInputDataBit(I2C_SCL_PORT, I2C_SCL_PIN)
#define SDA_READ()      GPIO_ReadInputDataBit(I2C_SDA_PORT, I2C_SDA_PIN)

/* ==================== 函数声明 ==================== */
void Soft_I2C_Init(void);                    // 软件 I2C 初始化
void OLED_Test_LED_Init(void);               // OLED测试用的LED初始化

// 软件 I2C 基础时序函数
void Soft_I2C_Start(void);                   // 起始信号
void Soft_I2C_Stop(void);                    // 停止信号
void Soft_I2C_SendByte(uint8_t byte);        // 发送1字节
uint8_t Soft_I2C_ReceiveByte(void);          // 接收1字节
void Soft_I2C_SendACK(uint8_t ack);          // 发送ACK(0)或NACK(1)
uint8_t Soft_I2C_WaitACK(void);              // 等待从机ACK，返回0成功，1失败

// 高级封装函数
int Soft_I2C_SendBytes(uint8_t Addr, uint8_t *pData, uint16_t Size);
int Soft_I2C_ReceiveBytes(uint8_t Addr, uint8_t *pBuffer, uint16_t Size);

/* ==================== 主函数 ==================== */
int main(void)
{
    Soft_I2C_Init();
    OLED_Test_LED_Init();
    
    // 发送命令点亮 OLED 屏幕
    uint8_t commands[] = {0x00, 0x8D, 0x14, 0xAF, 0xA5};
    Soft_I2C_SendBytes(0x78, commands, 5);
    
    // 从 OLED 读取 1 个字节（状态寄存器）
    uint8_t rcvd;
    Soft_I2C_ReceiveBytes(0x78, &rcvd, 1);
    
    // 判断第6位（D6）：0=屏幕点亮，1=屏幕熄灭
    if ((rcvd & (0x01 << 6)) == 0)
    {
        GPIO_ResetBits(GPIOC, GPIO_Pin_13);  // 点亮LED（PC13低电平）
    }
    else
    {
        GPIO_SetBits(GPIOC, GPIO_Pin_13);    // 熄灭LED（PC13高电平）
    }
    
    while(1)
    {
        
    }
}

/**
 * @brief  软件 I2C 初始化
 * @note   配置 PB10(SCL) 和 PB11(SDA) 为开漏输出模式
 *         开漏模式+上拉电阻是实现 I2C 总线的标准方式
 */
void Soft_I2C_Init(void)
{
    // 开启 GPIOB 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    GPIO_InitTypeDef GPIO_InitStruct;
    
    // 配置 SCL (PB10) - 开漏输出
    GPIO_InitStruct.GPIO_Pin = I2C_SCL_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;   // 开漏输出（关键！）
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C_SCL_PORT, &GPIO_InitStruct);
    
    // 配置 SDA (PB11) - 开漏输出
    GPIO_InitStruct.GPIO_Pin = I2C_SDA_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;   // 开漏输出（关键！）
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C_SDA_PORT, &GPIO_InitStruct);
    
    // 初始状态：SCL 和 SDA 都为高电平（空闲状态）
    SCL_H();
    SDA_H();
}

/**
 * @brief  OLED测试用的LED初始化（PC13）
 */
void OLED_Test_LED_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_13;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &GPIO_InitStruct);
    
    GPIO_SetBits(GPIOC, GPIO_Pin_13);  // 默认熄灭
}

/**
 * @brief  软件 I2C 起始信号
 * @note   时序：SCL=1 时，SDA 从高跳变到低
 *         ___
 *  SDA        \____
 *         ___
 *  SCL        \____
 */
void Soft_I2C_Start(void)
{
    SDA_H();
    Delay_us(5);
    SCL_H();
    Delay_us(5);
    SDA_L();        // SCL=1 时，SDA 从高变低
    Delay_us(5);
    SCL_L();        // 拉低时钟，准备发送数据
}

/**
 * @brief  软件 I2C 停止信号
 * @note   时序：SCL=1 时，SDA 从低跳变到高
 *              ___
 *  SDA  ____/
 *              ___
 *  SCL  ____/
 */
void Soft_I2C_Stop(void)
{
    SCL_L();
    Delay_us(5);
    SDA_L();
    Delay_us(5);
    SCL_H();
    Delay_us(5);
    SDA_H();        // SCL=1 时，SDA 从低变高
    Delay_us(5);
}

/**
 * @brief  软件 I2C 发送 1 字节
 * @param  byte: 要发送的数据（8位）
 * @note   时序：SCL=0 时改变 SDA，SCL=1 时保持，从机在 SCL=1 时采样 SDA
 *         先发高位（bit7），后发低位（bit0）
 */
void Soft_I2C_SendByte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        SCL_L();                            // 拉低时钟，准备改变数据线
        Delay_us(5);
        
        // 设置 SDA 为要发送的位（从高位开始）
        if (byte & 0x80)                    // bit7 为 1
            SDA_H();
        else
            SDA_L();
        
        Delay_us(5);
        SCL_H();                            // 拉高时钟，从机在此时读取 SDA
        Delay_us(5);
        
        byte <<= 1;                         // 左移，准备发下一位
    }
    SCL_L();                                // 拉低时钟，准备接收 ACK
}

/**
 * @brief  软件 I2C 等待从机应答
 * @retval 0: 收到 ACK，1: 未收到 ACK
 * @note   主机释放 SDA，从机在第 9 个时钟周期拉低 SDA 表示 ACK
 */
uint8_t Soft_I2C_WaitACK(void)
{
    uint8_t ack = 0;
    
    SDA_H();                                // 释放 SDA（开漏模式下主机置高，让从机可以拉低）
    Delay_us(5);
    SCL_H();                                // 拉高时钟，准备读取 ACK
    Delay_us(5);
    
    if (SDA_READ() == 1)                    // 读取 SDA 状态
        ack = 1;                            // SDA=1，无应答（NACK）
    
    SCL_L();                                // 拉低时钟，结束 ACK 周期
    Delay_us(5);
    
    return ack;
}

/**
 * @brief  软件 I2C 接收 1 字节
 * @retval 收到的 1 字节数据
 * @note   主机控制 SCL，在每个 SCL=1 时读取 SDA
 *         先收高位（bit7），后收低位（bit0）
 */
uint8_t Soft_I2C_ReceiveByte(void)
{
    uint8_t i, byte = 0;
    
    SDA_H();                                // 释放 SDA，准备读取从机数据
    
    for (i = 0; i < 8; i++)
    {
        byte <<= 1;                         // 左移，为新位腾出位置
        
        SCL_L();                            // 拉低时钟，通知从机准备数据
        Delay_us(5);
        SCL_H();                            // 拉高时钟，从机数据已稳定在 SDA 上
        Delay_us(5);
        
        if (SDA_READ() == 1)                // 读取 SDA 状态
            byte |= 0x01;                   // 收到 1
        // 否则 byte 最低位保持 0
    }
    
    SCL_L();                                // 拉低时钟，准备发送 ACK/NACK
    return byte;
}

/**
 * @brief  软件 I2 C 发送应答信号
 * @param  ack: 0=ACK（继续接收），1=NACK（停止接收）
 * @note   主机在第 9 个时钟周期控制 SDA：
 *         SDA=0 表示 ACK，SDA=1 表示 NACK
 */
void Soft_I2C_SendACK(uint8_t ack)
{
    SCL_L();                                // 拉低时钟，准备设置 SDA
    Delay_us(5);
    
    if (ack == 0)
        SDA_L();                            // ACK：拉低 SDA
    else
        SDA_H();                            // NACK：置高 SDA
    
    Delay_us(5);
    SCL_H();                                // 拉高时钟，从机读取 ACK/NACK
    Delay_us(5);
    SCL_L();                                // 拉低时钟，结束 ACK 周期
    Delay_us(5);
}

/**
 * @brief  软件 I2C 发送多字节（带起始和停止信号）
 * @param  Addr: 从机地址（8位格式，bit0=0 表示写）
 * @param  pData: 数据缓冲区
 * @param  Size: 数据长度
 * @retval 0: 成功，-1: 寻址失败，-2: 数据拒收
 */
int Soft_I2C_SendBytes(uint8_t Addr, uint8_t *pData, uint16_t Size)
{
    uint16_t i;
    
    Soft_I2C_Start();                       // 起始信号
    
    Soft_I2C_SendByte(Addr & 0xFE);         // 发送地址（写操作，bit0=0）
    if (Soft_I2C_WaitACK() != 0)            // 等待从机应答
    {
        Soft_I2C_Stop();                    // 无应答，发送停止信号
        return -1;                          // 返回：寻址失败
    }
    
    for (i = 0; i < Size; i++)
    {
        Soft_I2C_SendByte(pData[i]);        // 发送数据
        if (Soft_I2C_WaitACK() != 0)        // 等待从机应答
        {
            Soft_I2C_Stop();
            return -2;                      // 返回：数据拒收
        }
    }
    
    Soft_I2C_Stop();                        // 停止信号
    return 0;
}

/**
 * @brief  软件 I2C 接收多字节（带起始和停止信号）
 * @param  Addr: 从机地址（8位格式，bit0=1 表示读）
 * @param  pBuffer: 接收缓冲区
 * @param  Size: 接收长度
 * @retval 0: 成功，-1: 寻址失败
 */
int Soft_I2C_ReceiveBytes(uint8_t Addr, uint8_t *pBuffer, uint16_t Size)
{
    uint16_t i;
    
    Soft_I2C_Start();                       // 起始信号
    
    Soft_I2C_SendByte(Addr | 0x01);         // 发送地址（读操作，bit0=1）
    if (Soft_I2C_WaitACK() != 0)            // 等待从机应答
    {
        Soft_I2C_Stop();
        return -1;                          // 返回：寻址失败
    }
    
    for (i = 0; i < Size; i++)
    {
        pBuffer[i] = Soft_I2C_ReceiveByte(); // 接收数据
        
        if (i < Size - 1)
            Soft_I2C_SendACK(0);            // 不是最后一个字节，回 ACK（继续收）
        else
            Soft_I2C_SendACK(1);            // 最后一个字节，回 NACK（停止）
    }
    
    Soft_I2C_Stop();                        // 停止信号
    return 0;
}
