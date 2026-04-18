#include "stm32f10x.h"
#include "BspFaultTrace.h"
#include "BspControlTick.h"
#include "DrvDisplay.h"
#include "AppModeManager.h"
#include "stm32f10x_it.h"
#include <stdio.h>

int main(void)
{
    const char* resetCause;
    uint16_t bootCount;
    uint16_t faultCode;
    char line1[17];
    char line2[17];
    char line3[17];

    BspBootTrace_CaptureStartupState();
    resetCause = BspBootTrace_GetResetCauseText();
    bootCount = BspBootTrace_GetCount();
    faultCode = BspBootTrace_GetFaultCode();

    DrvDisplay_Init();
    snprintf(line1, sizeof(line1), "FuzzyPID 0408");
    snprintf(line2, sizeof(line2), "RST=%-4s F=%02u", resetCause, (unsigned)faultCode);
    snprintf(line3, sizeof(line3), "BOOT=%03u", (unsigned)bootCount);
    DrvDisplay_ShowLine(1u, line1);
    DrvDisplay_ShowLine(2u, line2);
    DrvDisplay_ShowLine(3u, line3);
    DrvDisplay_ShowLine(4u, "BOOT=APP INIT");
    RCC_ClearFlag();

    AppModeManager_Init();
    BspControlTick_Init();

    while (1) {
        AppModeManager_Process();
    }
}
