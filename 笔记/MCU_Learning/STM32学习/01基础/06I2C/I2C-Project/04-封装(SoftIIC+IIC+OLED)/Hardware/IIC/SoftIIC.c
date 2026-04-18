#include "SoftIIC.h"
#include "Delay.h"

/* ==================== 引脚操作宏 ==================== */
#define SCL_H()         GPIO_SetBits(SOFT_I2C_SCL_PORT, SOFT_I2C_SCL_PIN)
#define SCL_L()         GPIO_ResetBits(SOFT_I2C_SCL_PORT, SOFT_I2C_SCL_PIN)
#define SDA_H()         GPIO_SetBits(SOFT_I2C_SDA_PORT, SOFT_I2C_SDA_PIN)
#define SDA_L()         GPIO_ResetBits(SOFT_I2C_SDA_PORT, SOFT_I2C_SDA_PIN)
#define SCL_READ()      GPIO_ReadInputDataBit(SOFT_I2C_SCL_PORT, SOFT_I2C_SCL_PIN)
#define SDA_READ()      GPIO_ReadInputDataBit(SOFT_I2C_SDA_PORT, SOFT_I2C_SDA_PIN)

/* ==================== 基础时序函数 ==================== */

/**
 * @brief  软件 I2C 初始化
 */
void SoftIIC_Init(void)
{
    // 开启 GPIOB 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    GPIO_InitTypeDef GPIO_InitStruct;
    
    // 配置 SCL - 开漏输出
    GPIO_InitStruct.GPIO_Pin = SOFT_I2C_SCL_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SOFT_I2C_SCL_PORT, &GPIO_InitStruct);
    
    // 配置 SDA - 开漏输出
    GPIO_InitStruct.GPIO_Pin = SOFT_I2C_SDA_PIN;
    GPIO_Init(SOFT_I2C_SDA_PORT, &GPIO_InitStruct);
    
    // 初始状态：SCL 和 SDA 都为高电平（空闲状态）
    SCL_H();
    SDA_H();
}

/**
 * @brief  软件 I2C 起始信号
 * @note   时序：SCL=1 时，SDA 从高跳变到低
 */
void SoftIIC_Start(void)
{
    SDA_H();
    Delay_us(5);
    SCL_H();
    Delay_us(5);
    SDA_L();
    Delay_us(5);
    SCL_L();
}

/**
 * @brief  软件 I2C 停止信号
 * @note   时序：SCL=1 时，SDA 从低跳变到高
 */
void SoftIIC_Stop(void)
{
    SCL_L();
    Delay_us(5);
    SDA_L();
    Delay_us(5);
    SCL_H();
    Delay_us(5);
    SDA_H();
    Delay_us(5);
}

/**
 * @brief  软件 I2C 发送一个字节
 */
void SoftIIC_SendByte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        SCL_L();
        Delay_us(5);
        
        // 设置 SDA 为要发送的位（从高位开始）
        if (byte & 0x80)
            SDA_H();
        else
            SDA_L();
        
        Delay_us(5);
        SCL_H();
        Delay_us(5);
        
        byte <<= 1;
    }
    SCL_L();
}

/**
 * @brief  软件 I2C 等待从机应答
 * @retval 0: 收到ACK，1: 未收到ACK
 */
uint8_t SoftIIC_WaitACK(void)
{
    uint8_t ack = 0;
    
    SDA_H();        // 释放 SDA
    Delay_us(5);
    SCL_H();        // 拉高时钟
    Delay_us(5);
    
    if (SDA_READ() == 1)
        ack = 1;    // SDA=1，无应答
    
    SCL_L();
    Delay_us(5);
    
    return ack;
}

/**
 * @brief  软件 I2C 接收一个字节
 */
uint8_t SoftIIC_ReceiveByte(void)
{
    uint8_t i, byte = 0;
    
    SDA_H();        // 释放 SDA
    
    for (i = 0; i < 8; i++)
    {
        byte <<= 1;
        
        SCL_L();
        Delay_us(5);
        SCL_H();
        Delay_us(5);
        
        if (SDA_READ() == 1)
            byte |= 0x01;
    }
    
    SCL_L();
    return byte;
}

/**
 * @brief  软件 I2C 发送应答信号
 * @param  ack: 0=ACK，1=NACK
 */
void SoftIIC_SendACK(uint8_t ack)
{
    SCL_L();
    Delay_us(5);
    
    if (ack == 0)
        SDA_L();    // ACK
    else
        SDA_H();    // NACK
    
    Delay_us(5);
    SCL_H();
    Delay_us(5);
    SCL_L();
    Delay_us(5);
}

/* ==================== 高级封装函数 ==================== */

/**
 * @brief  软件 I2C 发送多字节
 */
int SoftIIC_SendBytes(uint8_t Addr, uint8_t *pData, uint16_t Size)
{
    uint16_t i;
    
    SoftIIC_Start();
    
    // 发送地址（写操作，bit0=0）
    SoftIIC_SendByte(Addr & 0xFE);
    if (SoftIIC_WaitACK() != 0)
    {
        SoftIIC_Stop();
        return -1;
    }
    
    // 发送数据
    for (i = 0; i < Size; i++)
    {
        SoftIIC_SendByte(pData[i]);
        if (SoftIIC_WaitACK() != 0)
        {
            SoftIIC_Stop();
            return -2;
        }
    }
    
    SoftIIC_Stop();
    return 0;
}

/**
 * @brief  软件 I2C 接收多字节
 */
int SoftIIC_ReceiveBytes(uint8_t Addr, uint8_t *pBuffer, uint16_t Size)
{
    uint16_t i;
    
    SoftIIC_Start();
    
    // 发送地址（读操作，bit0=1）
    SoftIIC_SendByte(Addr | 0x01);
    if (SoftIIC_WaitACK() != 0)
    {
        SoftIIC_Stop();
        return -1;
    }
    
    // 接收数据
    for (i = 0; i < Size; i++)
    {
        pBuffer[i] = SoftIIC_ReceiveByte();
        
        if (i < Size - 1)
            SoftIIC_SendACK(0);     // 继续接收
        else
            SoftIIC_SendACK(1);     // 停止接收
    }
    
    SoftIIC_Stop();
    return 0;
}

/**
 * @brief  软件 I2C 写寄存器
 */
int SoftIIC_WriteReg(uint8_t Addr, uint8_t Reg, uint8_t Value)
{
    uint8_t data[2] = {Reg, Value};
    return SoftIIC_SendBytes(Addr, data, 2);
}

/**
 * @brief  软件 I2C 读寄存器
 */
int SoftIIC_ReadReg(uint8_t Addr, uint8_t Reg, uint8_t *pValue)
{
    // 先发送寄存器地址
    int result = SoftIIC_SendBytes(Addr, &Reg, 1);
    if (result != 0) return result;
    
    // 重新启动并读取
    return SoftIIC_ReceiveBytes(Addr, pValue, 1);
}

/**
 * @brief  发送 OLED 命令
 */
int SoftIIC_SendOLEDCommand(uint8_t Addr, uint8_t Command)
{
    uint8_t data[2] = {0x00, Command};  // 0x00 = 控制字节，表示后续是命令
    return SoftIIC_SendBytes(Addr, data, 2);
}

/**
 * @brief  发送 OLED 数据
 */
int SoftIIC_SendOLEDData(uint8_t Addr, uint8_t Data)
{
    uint8_t data[2] = {0x40, Data};     // 0x40 = 控制字节，表示后续是数据
    return SoftIIC_SendBytes(Addr, data, 2);
}
