#ifndef __DRV_PARAM_STORE_H
#define __DRV_PARAM_STORE_H

#include "stm32f10x.h"

uint8_t DrvParamStore_Load(void *payload, uint16_t payloadSize);
uint8_t DrvParamStore_Save(const void *payload, uint16_t payloadSize);

#endif
