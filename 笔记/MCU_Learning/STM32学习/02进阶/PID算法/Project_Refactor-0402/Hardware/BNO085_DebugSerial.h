#ifndef __BNO085_DEBUGSERIAL_H
#define __BNO085_DEBUGSERIAL_H

#include "stm32f10x.h"
#include "ICM42688.h"

void BNO085_DebugSerial_Send(uint32_t tickCount, uint8_t isRunning, const ICM42688_Data_t *data);

#endif
