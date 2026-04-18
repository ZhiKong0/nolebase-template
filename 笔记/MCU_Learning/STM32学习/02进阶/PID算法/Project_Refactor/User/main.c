#include "headfile.h"
#include "stm32f10x_it.h"
#include <stdio.h>

// 全局控制系统实例
ControlStraightSystem_t g_straightSys;
ControlTrackSystem_t g_trackSys;
extern volatile uint32_t g_icmReadOkCount;
extern volatile uint32_t g_icmReadFailCount;

typedef enum {
    MAIN_MODE_STRAIGHT = 0u,
    MAIN_MODE_TRACK = 1u,
    MAIN_MODE_COUNT
} MainRunMode_t;

volatile uint8_t g_mainSelectedMode = MAIN_MODE_STRAIGHT;

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

static const char* Main_GetModeName(MainRunMode_t mode)
{
    if (mode == MAIN_MODE_TRACK) {
        return "TRACK";
    }
    return "STRAIGHT";
}

static void Main_ShowModeMenu(MainRunMode_t mode, uint8_t isRunning)
{
    char line1[17];
    char line3[17];
    char line4[17];

    snprintf(line1, sizeof(line1), "MODE:%-11s", Main_GetModeName(mode));
    if (isRunning) {
        snprintf(line3, sizeof(line3), "STATE:RUNNING  ");
    } else {
        snprintf(line3, sizeof(line3), "STATE:READY    ");
    }

    if (mode == MAIN_MODE_TRACK) {
        if (isRunning) {
            snprintf(line4, sizeof(line4), "TRACK:RUNNING  ");
        } else {
            snprintf(line4, sizeof(line4), "TRACK:PENDING  ");
        }
    } else {
        snprintf(line4, sizeof(line4), "TRACK:OFF      ");
    }

    OLED_ShowString(1, 1, line1);
    OLED_ShowString(2, 1, "S=RUN L=MODE   ");
    OLED_ShowString(3, 1, line3);
    OLED_ShowString(4, 1, line4);
}

static uint8_t Main_StartStraightMode(void)
{
    ControlStraight_LoadStableDefaults(&g_straightSys);
    ControlStraight_LockHeading(&g_straightSys);
    return ControlStraight_Start(&g_straightSys);
}

static uint8_t Main_StartTrackMode(void)
{
    return ControlTrack_Start(&g_trackSys);
}

static uint8_t Main_StartSelectedMode(MainRunMode_t mode)
{
    if (mode == MAIN_MODE_STRAIGHT) {
        return Main_StartStraightMode();
    }
    return Main_StartTrackMode();
}

static uint8_t Main_IsSelectedModeRunning(MainRunMode_t mode)
{
    if (mode == MAIN_MODE_STRAIGHT) {
        return g_straightSys.isRunning;
    }
    return g_trackSys.isRunning;
}

static void Main_StopSelectedMode(MainRunMode_t mode)
{
    if (mode == MAIN_MODE_STRAIGHT) {
        ControlStraight_Stop(&g_straightSys);
    } else {
        ControlTrack_Stop(&g_trackSys);
    }
}

int main(void)
{
    const char* resetCause = Main_GetResetCause();
    uint16_t faultCode = FaultTrace_GetAndClearCode();
    char bootLine3[17];
    MainRunMode_t selectedMode = MAIN_MODE_STRAIGHT;
    uint32_t currentTick;

    // 初始化 OLED
    OLED_Init();
    OLED_ShowString(1, 1, "Car Example");
    OLED_ShowString(2, 1, "Initializing...");
    snprintf(bootLine3, sizeof(bootLine3), "RST=%s F=%02u", resetCause, (unsigned)faultCode);
    OLED_ShowString(3, 1, bootLine3);
    OLED_ShowString(4, 1, "BOOT=OLED      ");
    RCC_ClearFlag();
    
    // 初始化按键
    OLED_ShowString(4, 1, "BOOT=KEY       ");
    Key_Init();
    ICM42688_DiagPinsInit();
    
    // 初始化控制系统（阶段A：速度闭环）
    // skipICM=0: 自动初始化 BNO085
    OLED_ShowString(4, 1, "BOOT=CTRL      ");
    ControlStraight_Init(&g_straightSys, 0);
    ControlTrack_Init(&g_trackSys);
    
    // 初始化定时器（1ms中断）
    OLED_ShowString(4, 1, "BOOT=TMR       ");
    Timer_Init();
	
    // 显示就绪状态（诊断界面）
    Main_ShowModeMenu(selectedMode, 0u);
    
    // 主循环
    while (1)
    {
        if (((uint8_t)selectedMode != g_mainSelectedMode) && !Main_IsSelectedModeRunning(selectedMode)) {
            selectedMode = (MainRunMode_t)g_mainSelectedMode;
            Main_ShowModeMenu(selectedMode, 0u);
        }

        if (selectedMode == MAIN_MODE_STRAIGHT) {
            ControlStraight_Background(&g_straightSys);
        } else {
            ControlTrack_Background(&g_trackSys);
        }

        uint8_t keyNum = Key_GetNum();
        if (keyNum == 2) {
            if (!Main_IsSelectedModeRunning(selectedMode)) {
                selectedMode = (MainRunMode_t)((selectedMode + 1u) % MAIN_MODE_COUNT);
                g_mainSelectedMode = (uint8_t)selectedMode;
                Main_ShowModeMenu(selectedMode, 0u);
            }
        } else if (keyNum == 1) {
            if (!Main_IsSelectedModeRunning(selectedMode))
            {
                if (Main_StartSelectedMode(selectedMode)) {
                    Main_ShowModeMenu(selectedMode, 1u);
                } else {
                    Main_ShowModeMenu(selectedMode, 0u);
                }
            }
            else
            {
                Main_StopSelectedMode(selectedMode);
                Main_ShowModeMenu(selectedMode, 0u);
            }
        }
        
        // 更新显示：航向角、误差与PWM等诊断信息
        static uint32_t lastDispTick = 0;
        currentTick = (selectedMode == MAIN_MODE_STRAIGHT) ? g_straightSys.tickCount : g_trackSys.tickCount;
        if ((currentTick - lastDispTick) >= 50) {
            lastDispTick = currentTick;
            Main_ShowModeMenu(selectedMode, Main_IsSelectedModeRunning(selectedMode));
        }
    }
}