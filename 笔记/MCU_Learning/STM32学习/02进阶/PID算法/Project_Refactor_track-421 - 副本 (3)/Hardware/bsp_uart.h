#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "config.h"

void    BspUart_Init(void);
void    BspUart_SendString(const char *s);
void    BspUart_SendBytes(const uint8_t *data, uint16_t len);
uint8_t BspUart_TakeCommand(char *out, uint8_t outSize);
void    BspUart_USART_IRQHandler(void);

/* Mode-aware telemetry: caller provides values, bsp_uart formats by mode */
void BspUart_SendTelemetryStraight(uint32_t tMs, uint32_t experimentId, uint8_t run,
                                   int16_t encL, int16_t encR,
                                   float yaw, float yawRate,
                                   int16_t pwmCore, int16_t headingDiff,
                                   int16_t dPostDZ,
                                   int16_t pwmL, int16_t pwmR,
                                   float hi);

void BspUart_SendTelemetryTrack(uint32_t tMs, uint32_t experimentId, uint8_t run,
                                int16_t encL, int16_t encR,
                                float yaw, float yawRate,
                                int16_t pwmCore, int16_t headingDiff,
                                int16_t pwmL, int16_t pwmR,
                                uint8_t sensorBits, int16_t linePos,
                                int8_t bearingDev, uint8_t crossCount,
                                uint8_t dbgTrackState, uint8_t dbgTurnDir,
                                uint8_t dbgCrossActive,
                                uint8_t dbgTelemState, uint16_t dbgTelemFlags,
                                uint8_t gainStage, uint8_t searchPhase,
                                uint8_t recoverTicks);

void BspUart_SendTelemetrySpin(uint32_t tMs, uint32_t experimentId, uint8_t run,
                               int16_t encL, int16_t encR,
                               float yaw, float yawRate,
                               int16_t pwmL, int16_t pwmR);

void BspUart_SendStat(SystemState_t state, ControlMode_t mode,
                      float skp, float ski, float skd,
                      float akp, float aki, float akd,
                      float lkp, float lki, float lkd,
                      float targetSpeed);

#endif
