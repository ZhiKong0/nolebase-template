#ifndef __IIC_H
#define __IIC_H

#include "stm32f10x.h"

/* ==================== 硬件 I2C 引脚配置（默认 I2C1: PB6/PB7）==================== */
// 如需使用重映射（PB8/PB9），在 IIC_Init() 中启用 GPIO_Remap_I2C1

/* ==================== 函数声明 ==================== */

/**
 * @brief  硬件 I2C 初始化（I2C1）
 * @param  None
 * @retval None
 * @note   默认使用 PB6(SCL)/PB7(SDA)，400kHz 快速模式
 *         如需重映射到 PB8/PB9，修改 IIC_Init 中的 GPIO_PinRemapConfig 调用
 */
void IIC_Init(void);

/**
 * @brief  硬件 I2C 发送多字节（阻塞式）
 * @param  Addr: 从机地址（8位格式，bit0=0 表示写）
 * @param  pData: 要发送的数据缓冲区
 * @param  Size: 数据长度
 * @retval 0: 成功，-1: 寻址失败，-2: 数据被拒收
 */
int IIC_SendBytes(uint8_t Addr, uint8_t *pData, uint16_t Size);

/**
 * @brief  硬件 I2C 接收多字节（阻塞式）
 * @param  Addr: 从机地址（8位格式，bit0=1 表示读）
 * @param  pBuffer: 接收数据缓冲区
 * @param  Size: 接收长度
 * @retval 0: 成功，-1: 寻址失败
 * @note   内部自动处理单字节/双字节/多字节的特殊时序
 */
int IIC_ReceiveBytes(uint8_t Addr, uint8_t *pBuffer, uint16_t Size);

/**
 * @brief  硬件 I2C 写寄存器（常用封装）
 * @param  Addr: 从机地址
 * @param  Reg: 寄存器地址
 * @param  Value: 要写入的值
 * @retval 0: 成功，其他: 失败
 */
int IIC_WriteReg(uint8_t Addr, uint8_t Reg, uint8_t Value);

/**
 * @brief  硬件 I2C 读寄存器（常用封装）
 * @param  Addr: 从机地址
 * @param  Reg: 寄存器地址
 * @param  pValue: 返回值指针
 * @retval 0: 成功，其他: 失败
 */
int IIC_ReadReg(uint8_t Addr, uint8_t Reg, uint8_t *pValue);

#endif /* __IIC_H */
