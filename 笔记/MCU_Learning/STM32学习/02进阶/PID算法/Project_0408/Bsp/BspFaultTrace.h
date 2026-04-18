#ifndef __BSP_FAULT_TRACE_H
#define __BSP_FAULT_TRACE_H

#include "stm32f10x.h"

#define BSP_FAULT_TRACE_NONE          0x0000u
#define BSP_FAULT_TRACE_HARD          0x0001u
#define BSP_FAULT_TRACE_MEM           0x0002u
#define BSP_FAULT_TRACE_BUS           0x0003u
#define BSP_FAULT_TRACE_USAGE         0x0004u

void BspFaultTrace_StoreAndReset(uint16_t code);
uint16_t FaultTrace_GetAndClearCode(void);

#endif
