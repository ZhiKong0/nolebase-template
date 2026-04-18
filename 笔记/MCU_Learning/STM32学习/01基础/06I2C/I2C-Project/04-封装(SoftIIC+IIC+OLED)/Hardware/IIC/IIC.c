#include "IIC.h"

/**
 * @brief  硬件 I2C 初始化（I2C1）
 * @note   默认使用 PB6(SCL)/PB7(SDA)，标准模式 100kHz
 *         如需使用重映射 PB8/PB9，取消 GPIO_PinRemapConfig 的注释
 */
void IIC_Init(void)
{
    // 开启 I2C1 和 GPIOB 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    // 如需重映射，开启 AFIO 时钟并调用重映射函数
    // RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    
    // 配置 GPIO 为复用开漏模式
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;  // PB6=SCL, PB7=SDA
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_OD;        // 复用开漏
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // 如需重映射到 PB8/PB9，取消下面两行的注释
    // GPIO_InitStruct.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    // GPIO_Init(GPIOB, &GPIO_InitStruct);
    // GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);
    
    // 复位 I2C1
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, DISABLE);
    
    // 配置 I2C 参数
    I2C_InitTypeDef I2C_InitStruct;
    I2C_InitStruct.I2C_ClockSpeed = 100000;               // 100kHz 标准模式
    I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;              // I2C 模式
    I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;    // 占空比 2:1
    I2C_InitStruct.I2C_Ack = I2C_Ack_Enable;           // 默认使能 ACK
    I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;  // 7位地址
    I2C_InitStruct.I2C_OwnAddress1 = 0x00;               // 主机地址（任意值即可）
    I2C_Init(I2C1, &I2C_InitStruct);
    
    // 使能 I2C1
    I2C_Cmd(I2C1, ENABLE);
}

/**
 * @brief  硬件 I2C 发送多字节
 */
int IIC_SendBytes(uint8_t Addr, uint8_t *pData, uint16_t Size)
{
    // 等待总线空闲
    while(I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY) == SET);
    
    // 发送起始信号
    I2C_GenerateSTART(I2C1, ENABLE);
    while(I2C_GetFlagStatus(I2C1, I2C_FLAG_SB) == RESET);
    
    // 清除 AF 标志
    I2C_ClearFlag(I2C1, I2C_FLAG_AF);
    
    // 发送地址（写操作）
    I2C_SendData(I2C1, Addr & 0xFE);
    
    // 等待寻址结果
    while(1)
    {
        if(I2C_GetFlagStatus(I2C1, I2C_FLAG_ADDR) == SET) break;
        if(I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) == SET)
        {
            I2C_GenerateSTOP(I2C1, ENABLE);
            return -1;  // 寻址失败
        }
    }
    
    // 清除 ADDR 标志
    I2C_ReadRegister(I2C1, I2C_Register_SR1);
    I2C_ReadRegister(I2C1, I2C_Register_SR2);
    
    // 发送数据
    for(uint16_t i = 0; i < Size; i++)
    {
        // 等待发送缓冲区空闲
        while(1)
        {
            if(I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) == SET)
            {
                I2C_GenerateSTOP(I2C1, ENABLE);
                return -2;  // 数据被拒收
            }
            if(I2C_GetFlagStatus(I2C1, I2C_FLAG_TXE) == SET) break;
        }
        I2C_SendData(I2C1, pData[i]);
    }
    
    // 等待发送完成
    while(1)
    {
        if(I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) == SET)
        {
            I2C_GenerateSTOP(I2C1, ENABLE);
            return -2;
        }
        if(I2C_GetFlagStatus(I2C1, I2C_FLAG_BTF) == SET) break;
    }
    
    // 发送停止信号
    I2C_GenerateSTOP(I2C1, ENABLE);
    return 0;
}

/**
 * @brief  硬件 I2C 接收多字节
 * @note   正确处理单字节/双字节/多字节的特殊时序
 */
int IIC_ReceiveBytes(uint8_t Addr, uint8_t *pBuffer, uint16_t Size)
{
    uint16_t i;
    
    // 发送起始信号
    I2C_GenerateSTART(I2C1, ENABLE);
    while(I2C_GetFlagStatus(I2C1, I2C_FLAG_SB) == RESET);
    
    // 清除 AF 标志
    I2C_ClearFlag(I2C1, I2C_FLAG_AF);
    
    // 发送地址（读操作）
    I2C_SendData(I2C1, Addr | 0x01);
    
    // 等待寻址结果
    while(1)
    {
        if(I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) == SET)
        {
            I2C_GenerateSTOP(I2C1, ENABLE);
            return -1;
        }
        if(I2C_GetFlagStatus(I2C1, I2C_FLAG_ADDR) == SET) break;
    }
    
    // 根据接收字节数使用不同策略
    if(Size == 1)
    {
        // 单字节接收：先禁用ACK和发送STOP，再清除ADDR
        I2C_AcknowledgeConfig(I2C1, DISABLE);
        I2C_GenerateSTOP(I2C1, ENABLE);
        I2C_ReadRegister(I2C1, I2C_Register_SR1);
        I2C_ReadRegister(I2C1, I2C_Register_SR2);
        
        // 等待数据到达
        while(I2C_GetFlagStatus(I2C1, I2C_FLAG_RXNE) == RESET);
        pBuffer[0] = I2C_ReceiveData(I2C1);
    }
    else if(Size == 2)
    {
        // 双字节接收：使用POS位
        I2C_AcknowledgeConfig(I2C1, ENABLE);
        I2C_NACKPositionConfig(I2C1, I2C_NACKPosition_Next);
        I2C_ReadRegister(I2C1, I2C_Register_SR1);
        I2C_ReadRegister(I2C1, I2C_Register_SR2);
        
        // 等待字节传输完成
        while(I2C_GetFlagStatus(I2C1, I2C_FLAG_BTF) == RESET);
        
        // 禁用ACK并发送STOP
        I2C_AcknowledgeConfig(I2C1, DISABLE);
        I2C_GenerateSTOP(I2C1, ENABLE);
        
        // 读取两个字节
        pBuffer[0] = I2C_ReceiveData(I2C1);
        while(I2C_GetFlagStatus(I2C1, I2C_FLAG_RXNE) == RESET);
        pBuffer[1] = I2C_ReceiveData(I2C1);
    }
    else
    {
        // 3字节及以上：标准流程
        I2C_ReadRegister(I2C1, I2C_Register_SR1);
        I2C_ReadRegister(I2C1, I2C_Register_SR2);
        
        // 接收前 Size-1 字节，发送ACK
        for(i = 0; i < Size - 1; i++)
        {
            while(I2C_GetFlagStatus(I2C1, I2C_FLAG_RXNE) == RESET);
            pBuffer[i] = I2C_ReceiveData(I2C1);
            I2C_AcknowledgeConfig(I2C1, ENABLE);
        }
        
        // 最后一个字节：发送NACK和STOP
        I2C_AcknowledgeConfig(I2C1, DISABLE);
        I2C_GenerateSTOP(I2C1, ENABLE);
        while(I2C_GetFlagStatus(I2C1, I2C_FLAG_RXNE) == RESET);
        pBuffer[Size - 1] = I2C_ReceiveData(I2C1);
    }
    
    return 0;
}

/**
 * @brief  硬件 I2C 写寄存器
 */
int IIC_WriteReg(uint8_t Addr, uint8_t Reg, uint8_t Value)
{
    uint8_t data[2] = {Reg, Value};
    return IIC_SendBytes(Addr, data, 2);
}

/**
 * @brief  硬件 I2C 读寄存器
 */
int IIC_ReadReg(uint8_t Addr, uint8_t Reg, uint8_t *pValue)
{
    // 先发送寄存器地址（写操作）
    int result = IIC_SendBytes(Addr, &Reg, 1);
    if(result != 0) return result;
    
    // 重新启动并读取数据
    return IIC_ReceiveBytes(Addr, pValue, 1);
}
