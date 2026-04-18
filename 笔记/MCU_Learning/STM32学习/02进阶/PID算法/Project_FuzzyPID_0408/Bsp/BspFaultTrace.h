#ifndef __BSP_FAULT_TRACE_H
#define __BSP_FAULT_TRACE_H

#include "stm32f10x.h"

#define BSP_FAULT_TRACE_NONE          0x0000u
#define BSP_FAULT_TRACE_HARD          0x0001u
#define BSP_FAULT_TRACE_MEM           0x0002u
#define BSP_FAULT_TRACE_BUS           0x0003u
#define BSP_FAULT_TRACE_USAGE         0x0004u

#define BSP_RESET_CAUSE_UNKNOWN       0u
#define BSP_RESET_CAUSE_WWDG          1u
#define BSP_RESET_CAUSE_IWDG          2u
#define BSP_RESET_CAUSE_SOFTWARE      3u
#define BSP_RESET_CAUSE_POWER         4u
#define BSP_RESET_CAUSE_PIN           5u
#define BSP_RESET_CAUSE_LOW_POWER     6u

void BspBootTrace_CaptureResetState(void);
void BspBootTrace_CaptureStartupState(void);
uint16_t BspBootTrace_GetCount(void);
uint8_t BspBootTrace_GetResetCauseCode(void);
const char* BspBootTrace_GetResetCauseText(void);
uint16_t BspBootTrace_GetFaultCode(void);
const char* BspBootTrace_GetFaultText(void);
uint32_t BspBootTrace_GetFaultCfsr(void);
uint32_t BspBootTrace_GetFaultHfsr(void);
uint32_t BspBootTrace_GetFaultAddr(void);
const char* BspBootTrace_GetFaultAddrSourceText(void);

void BspFaultTrace_StoreAndReset(uint16_t code);
uint16_t FaultTrace_GetAndClearCode(void);

#endif
