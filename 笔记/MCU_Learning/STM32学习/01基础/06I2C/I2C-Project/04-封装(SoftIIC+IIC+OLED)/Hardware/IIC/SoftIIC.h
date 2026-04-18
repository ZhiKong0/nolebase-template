#ifndef __SOFT_IIC_H
#define __SOFT_IIC_H

#include "stm32f10x.h"

/* ==================== 软件 I2C 引脚配置 ==================== */
// 默认使用 PB7 作为 SCL，PB8 作为 SDA（与 OLED 统一）
// 可通过修改下面的宏定义更改为任意 GPIO

#define SOFT_I2C_SCL_PORT    GPIOB
#define SOFT_I2C_SDA_PORT    GPIOB
#define SOFT_I2C_SCL_PIN     GPIO_Pin_7
#define SOFT_I2C_SDA_PIN     GPIO_Pin_8

/* ==================== 函数声明 ==================== */

/**
 * @brief  软件 I2C 初始化
 * @param  None
 * @retval None
 * @note   配置 SCL 和 SDA 为开漏输出模式，初始状态为高电平
 */
void SoftIIC_Init(void);

/**
 * @brief  软件 I2C 起始信号
 * @param  None
 * @retval None
 */
void SoftIIC_Start(void);

/**
 * @brief  软件 I2C 停止信号
 * @param  None
 * @retval None
 */
void SoftIIC_Stop(void);

/**
 * @brief  软件 I2C 发送一个字节
 * @param  byte: 要发送的数据
 * @retval None
 */
void SoftIIC_SendByte(uint8_t byte);

/**
 * @brief  软件 I2C 接收一个字节
 * @param  None
 * @retval 接收到的数据
 */
uint8_t SoftIIC_ReceiveByte(void);

/**
 * @brief  软件 I2C 发送应答信号
 * @param  ack: 0=ACK(继续接收)，1=NACK(停止接收)
 * @retval None
 */
void SoftIIC_SendACK(uint8_t ack);

/**
 * @brief  软件 I2C 等待从机应答
 * @param  None
 * @retval 0: 收到ACK，1: 收到NACK
 */
uint8_t SoftIIC_WaitACK(void);

/**
 * @brief  软件 I2C 发送多字节（带起始和停止信号）
 * @param  Addr: 从机地址（8位格式，bit0=0 表示写）
 * @param  pData: 数据缓冲区
 * @param  Size: 数据长度
 * @retval 0: 成功，-1: 寻址失败，-2: 数据被拒收
 */
int SoftIIC_SendBytes(uint8_t Addr, uint8_t *pData, uint16_t Size);

/**
 * @brief  软件 I2C 接收多字节（带起始和停止信号）
 * @param  Addr: 从机地址（8位格式，bit0=1 表示读）
 * @param  pBuffer: 接收缓冲区
 * @param  Size: 接收长度
 * @retval 0: 成功，-1: 寻址失败
 */
int SoftIIC_ReceiveBytes(uint8_t Addr, uint8_t *pBuffer, uint16_t Size);

/**
 * @brief  软件 I2C 写寄存器（常用封装）
 * @param  Addr: 从机地址
 * @param  Reg: 寄存器地址
 * @param  Value: 要写入的值
 * @retval 0: 成功，其他: 失败
 */
int SoftIIC_WriteReg(uint8_t Addr, uint8_t Reg, uint8_t Value);

/**
 * @brief  软件 I2C 读寄存器（常用封装）
 * @param  Addr: 从机地址
 * @param  Reg: 寄存器地址
 * @param  pValue: 返回值指针
 * @retval 0: 成功，其他: 失败
 */
int SoftIIC_ReadReg(uint8_t Addr, uint8_t Reg, uint8_t *pValue);

/**
 * @brief  软件 I2C 发送命令到 OLED
 * @param  Addr: OLED地址（通常为0x78）
 * @param  Command: 要发送的命令
 * @retval 0: 成功，其他: 失败
 * @note   自动添加0x00控制字节
 */
int SoftIIC_SendOLEDCommand(uint8_t Addr, uint8_t Command);

/**
 * @brief  软件 I2C 发送数据到 OLED
 * @param  Addr: OLED地址（通常为0x78）
 * @param  Data: 要发送的数据
 * @retval 0: 成功，其他: 失败
 * @note   自动添加0x40控制字节
 */
int SoftIIC_SendOLEDData(uint8_t Addr, uint8_t Data);

#endif /* __SOFT_IIC_H */
