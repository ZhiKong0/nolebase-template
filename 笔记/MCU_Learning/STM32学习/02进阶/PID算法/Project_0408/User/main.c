#include "headfile.h"
#include "stm32f10x_it.h"
#include <stdio.h>

static AppModeManager_t g_app;

static const char* Main_GetResetCause(void)
{
    if (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) != RESET) return "WWDG";
    if (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) return "IWDG";
    if (RCC_GetFlagStatus(RCC_FLAG_SFTRST) != RESET) return "SFT ";
    if (RCC_GetFlagStatus(RCC_FLAG_PORRST) != RESET) return "POR ";
    if (RCC_GetFlagStatus(RCC_FLAG_PINRST) != RESET) return "PIN ";
    if (RCC_GetFlagStatus(RCC_FLAG_LPWRRST) != RESET) return "LPWR";
    return "UNKN";
}

int main(void)
{
    const char* resetCause = Main_GetResetCause();
    uint16_t faultCode = FaultTrace_GetAndClearCode();
    char line1[17];
    char line2[17];
    char line3[17];

    DrvDisplay_Init();
    snprintf(line1, sizeof(line1), "Car Refactor");
    snprintf(line2, sizeof(line2), "RST=%-4s F=%02u", resetCause, (unsigned)faultCode);
    snprintf(line3, sizeof(line3), "BOOT=DISPLAY");
    DrvDisplay_ShowLine(1u, line1);
    DrvDisplay_ShowLine(2u, line2);
    DrvDisplay_ShowLine(3u, line3);
    DrvDisplay_ShowLine(4u, "BOOT=INIT");
    RCC_ClearFlag();

    AppModeManager_Init(&g_app);
    BspControlTick_Init();

    while (1) {
        AppModeManager_Process(&g_app);
    }
}
