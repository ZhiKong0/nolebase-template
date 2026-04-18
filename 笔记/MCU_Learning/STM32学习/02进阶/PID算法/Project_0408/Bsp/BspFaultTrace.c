#include "BspFaultTrace.h"
#include "stm32f10x_bkp.h"
#include "stm32f10x_pwr.h"

#define BSP_FAULT_TRACE_MAGIC         0xA500u

static void bsp_fault_trace_store(uint16_t code)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    BKP_WriteBackupRegister(BKP_DR1, (uint16_t)(BSP_FAULT_TRACE_MAGIC | (code & 0x00FFu)));
}

void BspFaultTrace_StoreAndReset(uint16_t code)
{
    bsp_fault_trace_store(code);
    NVIC_SystemReset();
    while (1) {
    }
}

uint16_t FaultTrace_GetAndClearCode(void)
{
    uint16_t raw;
    uint16_t out = BSP_FAULT_TRACE_NONE;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    raw = BKP_ReadBackupRegister(BKP_DR1);
    if ((raw & 0xFF00u) == BSP_FAULT_TRACE_MAGIC) {
        out = (uint16_t)(raw & 0x00FFu);
    }
    BKP_WriteBackupRegister(BKP_DR1, 0u);

    return out;
}
