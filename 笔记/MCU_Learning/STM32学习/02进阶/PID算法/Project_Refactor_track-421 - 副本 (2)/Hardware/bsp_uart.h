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

void BspUart_SendTelemetrySpin(uint32_t tMs, uint32_t experimentId, uint8_t run,
                               int16_t encL, int16_t encR,
                               float yaw, float yawRate,
                               int16_t pwmL, int16_t pwmR);

void BspUart_SendTelemetryTrack(uint32_t tMs, uint32_t experimentId, uint8_t run,
                                int16_t encL, int16_t encR,
                                float yaw, float yawRate,
                                int16_t pwmCore, int16_t headingDiff,
                                int16_t pwmL, int16_t pwmR,
                                uint8_t sensorBits, float linePos,
                                uint8_t pidBypassActive,
                                float gainAlpha, float activeLkp, float activeLkd,
                                float activeCenterAnchor, float activeSteerTrim,
                                float curveTargetSpeed,
                                uint8_t acuteState, float acuteYawDelta,
                                uint32_t acuteRearmTick,
                                uint8_t dbgTrackState, uint8_t dbgCornerDir,
                                float dbgCornerYawDelta, uint8_t dbgCornerBits,
                                uint8_t dbgCornerAcceptMask,
                                uint8_t dbgCornerYawReady,
                                uint8_t dbgCornerAcceptHit);

void BspUart_SendStat(SystemState_t state, ControlMode_t mode,
                      float skp, float ski, float skd,
                      float akp, float aki, float akd,
                      float lkp, float lki, float lkd,
                      float targetSpeed,
                      uint8_t dynamicPidEnabled,
                      float kpStraight, float kpCurve,
                      float kdStraight, float kdCurve,
                      float deadbandStraight, float deadbandCurve,
                      float loadLow, float loadHigh,
                      float centerAnchorStraight, float centerAnchorCurve,
                      float steerTrim,
                      float curveBrakeGain,
                      float curveSpeedMinRatio);

#endif
