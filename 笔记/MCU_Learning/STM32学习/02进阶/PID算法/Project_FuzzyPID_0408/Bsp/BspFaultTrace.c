#include "BspFaultTrace.h"
#include "stm32f10x_bkp.h"
#include "stm32f10x_pwr.h"

#define BSP_FAULT_TRACE_MAGIC         0xA500u
#define BSP_BOOT_TRACE_MAGIC          0x5A5Au

static uint16_t s_bootCount = 0u;
static uint8_t s_resetCause = BSP_RESET_CAUSE_UNKNOWN;
static uint16_t s_faultCode = BSP_FAULT_TRACE_NONE;
static uint32_t s_faultCfsr = 0u;
static uint32_t s_faultHfsr = 0u;
static uint32_t s_faultAddr = 0u;

static uint32_t bsp_fault_trace_capture_fault_addr(uint16_t code)
{
    if (code == BSP_FAULT_TRACE_MEM) {
        return SCB->MMFAR;
    }
    if (code == BSP_FAULT_TRACE_BUS) {
        return SCB->BFAR;
    }
    return 0u;
}

static void bsp_boot_trace_enable_backup(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
}

static uint8_t bsp_boot_trace_detect_reset_cause(void)
{
    if (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) != RESET) {
        return BSP_RESET_CAUSE_WWDG;
    }
    if (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) {
        return BSP_RESET_CAUSE_IWDG;
    }
    if (RCC_GetFlagStatus(RCC_FLAG_SFTRST) != RESET) {
        return BSP_RESET_CAUSE_SOFTWARE;
    }
    if (RCC_GetFlagStatus(RCC_FLAG_PORRST) != RESET) {
        return BSP_RESET_CAUSE_POWER;
    }
    if (RCC_GetFlagStatus(RCC_FLAG_PINRST) != RESET) {
        return BSP_RESET_CAUSE_PIN;
    }
    if (RCC_GetFlagStatus(RCC_FLAG_LPWRRST) != RESET) {
        return BSP_RESET_CAUSE_LOW_POWER;
    }
    return BSP_RESET_CAUSE_UNKNOWN;
}

static void bsp_fault_trace_store(uint16_t code)
{
    uint32_t faultAddr = bsp_fault_trace_capture_fault_addr(code);

    bsp_boot_trace_enable_backup();
    BKP_WriteBackupRegister(BKP_DR1, (uint16_t)(BSP_FAULT_TRACE_MAGIC | (code & 0x00FFu)));
    BKP_WriteBackupRegister(BKP_DR4, (uint16_t)(SCB->CFSR & 0xFFFFu));
    BKP_WriteBackupRegister(BKP_DR5, (uint16_t)((SCB->CFSR >> 16) & 0xFFFFu));
    BKP_WriteBackupRegister(BKP_DR6, (uint16_t)(SCB->HFSR & 0xFFFFu));
    BKP_WriteBackupRegister(BKP_DR7, (uint16_t)((SCB->HFSR >> 16) & 0xFFFFu));
    BKP_WriteBackupRegister(BKP_DR8, (uint16_t)(faultAddr & 0xFFFFu));
    BKP_WriteBackupRegister(BKP_DR9, (uint16_t)((faultAddr >> 16) & 0xFFFFu));
    BKP_WriteBackupRegister(BKP_DR10, 0u);
}

static uint16_t bsp_fault_trace_read_and_clear(void)
{
    uint16_t raw;
    uint16_t out = BSP_FAULT_TRACE_NONE;

    bsp_boot_trace_enable_backup();

    raw = BKP_ReadBackupRegister(BKP_DR1);
    if ((raw & 0xFF00u) == BSP_FAULT_TRACE_MAGIC) {
        out = (uint16_t)(raw & 0x00FFu);
    }
    BKP_WriteBackupRegister(BKP_DR1, 0u);
    s_faultCfsr = ((uint32_t)BKP_ReadBackupRegister(BKP_DR5) << 16) |
                  (uint32_t)BKP_ReadBackupRegister(BKP_DR4);
    s_faultHfsr = ((uint32_t)BKP_ReadBackupRegister(BKP_DR7) << 16) |
                  (uint32_t)BKP_ReadBackupRegister(BKP_DR6);
    s_faultAddr = ((uint32_t)BKP_ReadBackupRegister(BKP_DR9) << 16) |
                  (uint32_t)BKP_ReadBackupRegister(BKP_DR8);
    BKP_WriteBackupRegister(BKP_DR4, 0u);
    BKP_WriteBackupRegister(BKP_DR5, 0u);
    BKP_WriteBackupRegister(BKP_DR6, 0u);
    BKP_WriteBackupRegister(BKP_DR7, 0u);
    BKP_WriteBackupRegister(BKP_DR8, 0u);
    BKP_WriteBackupRegister(BKP_DR9, 0u);
    BKP_WriteBackupRegister(BKP_DR10, 0u);

    return out;
}

void BspBootTrace_CaptureResetState(void)
{
    uint16_t count;

    s_resetCause = bsp_boot_trace_detect_reset_cause();
    bsp_boot_trace_enable_backup();

    if (BKP_ReadBackupRegister(BKP_DR3) == BSP_BOOT_TRACE_MAGIC) {
        count = BKP_ReadBackupRegister(BKP_DR2);
        if (count == 0xFFFFu) {
            count = 1u;
        } else {
            count++;
        }
    } else {
        count = 1u;
        BKP_WriteBackupRegister(BKP_DR3, BSP_BOOT_TRACE_MAGIC);
    }

    BKP_WriteBackupRegister(BKP_DR2, count);
    s_bootCount = count;
}

void BspBootTrace_CaptureStartupState(void)
{
    BspBootTrace_CaptureResetState();
    s_faultCode = bsp_fault_trace_read_and_clear();
}

uint16_t BspBootTrace_GetCount(void)
{
    return s_bootCount;
}

uint8_t BspBootTrace_GetResetCauseCode(void)
{
    return s_resetCause;
}

const char* BspBootTrace_GetResetCauseText(void)
{
    switch (s_resetCause) {
        case BSP_RESET_CAUSE_WWDG: return "WWDG";
        case BSP_RESET_CAUSE_IWDG: return "IWDG";
        case BSP_RESET_CAUSE_SOFTWARE: return "SFT";
        case BSP_RESET_CAUSE_POWER: return "POR";
        case BSP_RESET_CAUSE_PIN: return "PIN";
        case BSP_RESET_CAUSE_LOW_POWER: return "LPWR";
        default: return "UNKN";
    }
}

uint16_t BspBootTrace_GetFaultCode(void)
{
    return s_faultCode;
}

const char* BspBootTrace_GetFaultText(void)
{
    switch (s_faultCode) {
        case BSP_FAULT_TRACE_HARD: return "hard";
        case BSP_FAULT_TRACE_MEM: return "mem";
        case BSP_FAULT_TRACE_BUS: return "bus";
        case BSP_FAULT_TRACE_USAGE: return "usage";
        default: return "none";
    }
}

uint32_t BspBootTrace_GetFaultCfsr(void)
{
    return s_faultCfsr;
}

uint32_t BspBootTrace_GetFaultHfsr(void)
{
    return s_faultHfsr;
}

uint32_t BspBootTrace_GetFaultAddr(void)
{
    return s_faultAddr;
}

const char* BspBootTrace_GetFaultAddrSourceText(void)
{
    if (s_faultCode == BSP_FAULT_TRACE_MEM) {
        return "mmfar";
    }
    if (s_faultCode == BSP_FAULT_TRACE_BUS) {
        return "bfar";
    }
    return "none";
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
    return bsp_fault_trace_read_and_clear();
}
