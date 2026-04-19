/*
 * 串口板级封装说明:
 * 1. USART2 只暴露“字符串发送 / 命令接收 / 遥测格式化”三类能力。
 * 2. 命令帧格式统一为 "#...!"，解析后的业务含义由 main.c 解释。
 * 3. 遥测函数只负责格式化，不持有控制状态，所有数据都由调用方显式传入。
 */
#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "config.h"

/* 初始化 USART2、发送中断和接收状态机。 */
void    BspUart_Init(void);
/* 发送以 '\0' 结尾的 C 字符串，内部进入环形发送缓冲。 */
void    BspUart_SendString(const char *s);
/* 发送原始字节流，适合二进制或拼帧后的数据。 */
void    BspUart_SendBytes(const uint8_t *data, uint16_t len);
/* 若收到完整 "#...!" 命令帧，则拷贝到 out 并返回 1。 */
uint8_t BspUart_TakeCommand(char *out, uint8_t outSize);
/* USART2 中断入口，由 stm32f10x_it.c 转发。 */
void    BspUart_USART_IRQHandler(void);

/* 直线模式遥测: 输出编码器、姿态、速度环、航向环等关键观测量。 */
void BspUart_SendTelemetryStraight(uint32_t tMs, uint32_t experimentId, uint8_t run,
                                   int16_t encL, int16_t encR,
                                   float yaw, float yawRate,
                                   int16_t pwmCore, int16_t headingDiff,
                                   int16_t dPostDZ,
                                   int16_t pwmL, int16_t pwmR,
                                   float hi,
                                   float targetSpeed, float rampTarget);

/* 循迹模式遥测: 在直线模式字段之外补充读线结果和目标航向。 */
void BspUart_SendTelemetryTrack(uint32_t tMs, uint32_t experimentId, uint8_t run,
                                int16_t encL, int16_t encR,
                                float yaw, float yawRate,
                                int16_t pwmCore, int16_t headingDiff,
                                int16_t pwmL, int16_t pwmR,
                                uint8_t sensorBits, float linePos,
                                float positionError, float yawCommand,
                                float targetYaw, uint8_t lineDetected,
                                float targetSpeed, float rampTarget,
                                float speedScale, uint8_t captureActive,
                                float captureAuthorityScale, uint8_t captureSwitchActive, float captureUnloadScale, float recenterScale,
                                uint8_t sCurveActive, float headingDiffRatio,
                                float yawLimit, float lineKpScale);

/* 输出当前模式下关键 PID 参数，方便串口侧做参数快照。 */
void BspUart_SendStat(SystemState_t state, ControlMode_t mode,
                      float skp, float ski, float skd,
                      float akp, float aki, float akd,
                      float lkp, float lki, float lkd,
                      float targetSpeed);

#endif
