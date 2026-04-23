#ifndef __BSP_OLED_H
#define __BSP_OLED_H

#include "config.h"

void BspOled_Init(void);
void BspOled_ShowStatus(SystemState_t state, ControlMode_t mode,
                        float yaw, float speedL, float speedR,
                        uint32_t experimentId);
void BspOled_ShowIMUInit(uint8_t stage, uint8_t addr, uint8_t failCode);
void BspOled_ShowExperimentId(uint32_t experimentId);
void BspOled_ShowFaultCode(uint16_t code);
void BspOled_Clear(void);

#endif
