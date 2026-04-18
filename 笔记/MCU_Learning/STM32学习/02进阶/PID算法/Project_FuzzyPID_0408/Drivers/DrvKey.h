#ifndef __DRV_KEY_H
#define __DRV_KEY_H

#include "stm32f10x.h"

typedef enum {
    DRV_KEY_EVENT_NONE = 0,
    DRV_KEY_EVENT_SHORT = 1,
    DRV_KEY_EVENT_LONG = 2
} DrvKeyEvent_t;

void DrvKey_Init(void);
DrvKeyEvent_t DrvKey_Poll(uint32_t nowMs);

#endif
