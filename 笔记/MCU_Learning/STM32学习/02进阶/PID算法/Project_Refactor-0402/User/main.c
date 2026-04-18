#include "headfile.h"
#include "stm32f10x_it.h"
#include <stdio.h>

// 全局控制系统实例
ControlSystem_t g_controlSys;
extern volatile uint32_t g_icmReadOkCount;
extern volatile uint32_t g_icmReadFailCount;

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

static void Main_ShowDiagTemplate(void)
{
    OLED_ShowString(1, 1, "DIAG T0000 OK000");
    OLED_ShowString(2, 1, "F000 Y0000 E000");
    OLED_ShowString(3, 1, "C000 L000 R000 ");
    OLED_ShowString(4, 1, "R0W00A00S0P00 ");
}

static void Main_ShowPinPage(uint8_t page)
{
    char line1[17];
    char line2[17];
    char line3[17];
    char line4[17];

    if (page == 0u) {
        snprintf(line1, sizeof(line1), "PIN LIVE 1/4   ");
        snprintf(line2, sizeof(line2), "SCL=%u SDA=%u    ",
                 (unsigned)ICM42688_GetSclLevel(),
                 (unsigned)ICM42688_GetSdaLevel());
        snprintf(line3, sizeof(line3), "RST=%u INT=%u    ",
                 (unsigned)ICM42688_GetResetLevel(),
                 (unsigned)ICM42688_GetIntLevel());
        snprintf(line4, sizeof(line4), "1NEXT 2EXIT    ");
    } else if (page == 1u) {
        snprintf(line1, sizeof(line1), "PIN CFG  2/4   ");
        snprintf(line2, sizeof(line2), "ADDR=0 CS=MAN  ");
        snprintf(line3, sizeof(line3), "PS0=0 PS1=0    ");
        snprintf(line4, sizeof(line4), "CFG=MANUAL     ");
    } else if (page == 2u) {
        snprintf(line1, sizeof(line1), "PIN MAP  3/4   ");
        snprintf(line2, sizeof(line2), "SCL=B12 SDA13  ");
        snprintf(line3, sizeof(line3), "RST=B14 INT=A15");
        snprintf(line4, sizeof(line4), "1NEXT 2EXIT    ");
    } else {
        snprintf(line1, sizeof(line1), "I2C ACK  4/4   ");
        snprintf(line2, sizeof(line2), "28=%u 29=%u      ",
                 (unsigned)ICM42688_DiagProbeAddr(0x28u),
                 (unsigned)ICM42688_DiagProbeAddr(0x29u));
        snprintf(line3, sizeof(line3), "4A=%u 4B=%u      ",
                 (unsigned)ICM42688_DiagProbeAddr(0x4Au),
                 (unsigned)ICM42688_DiagProbeAddr(0x4Bu));
        snprintf(line4, sizeof(line4), "1NEXT 2EXIT    ");
    }

    OLED_ShowString(1, 1, line1);
    OLED_ShowString(2, 1, line2);
    OLED_ShowString(3, 1, line3);
    OLED_ShowString(4, 1, line4);
}

int main(void)
{
    const char* resetCause = Main_GetResetCause();
    uint16_t faultCode = FaultTrace_GetAndClearCode();
    char bootLine3[17];
    char line4[17];
    uint8_t showPinMenu = 0u;
    uint8_t pinPage = 0u;

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
    Control_Init(&g_controlSys, 0);
    
    // 初始化定时器（1ms中断）
    OLED_ShowString(4, 1, "BOOT=TMR       ");
    Timer_Init();
	
    // 显示就绪状态（诊断界面）
    Main_ShowDiagTemplate();
    
    // 主循环
    while (1)
    {
        Control_Background(&g_controlSys);

        uint8_t keyNum = Key_GetNum();
        if (keyNum == 2) {
            showPinMenu = showPinMenu ? 0u : 1u;
            if (showPinMenu) {
                pinPage = 0u;
                Main_ShowPinPage(pinPage);
            } else {
                Main_ShowDiagTemplate();
            }
        } else if (showPinMenu && keyNum == 1) {
            pinPage++;
            if (pinPage >= 4u) pinPage = 0u;
            Main_ShowPinPage(pinPage);
        } else if (keyNum == 1) {
            if (!g_controlSys.isRunning)
            {
                OLED_ShowString(1, 1, "Starting...     ");
                Control_LoadStableDefaults(&g_controlSys);
                Control_LockHeading(&g_controlSys);  // 锁定当前航向
                if (Control_Start(&g_controlSys)) {
                    OLED_ShowString(1, 1, "Running...      ");
                } else {
                    OLED_ShowString(1, 1, "Start Fail      ");
                    Main_ShowDiagTemplate();
                }
            }
            else
            {
                Control_Stop(&g_controlSys);
                Main_ShowDiagTemplate();
            }
        }
        
        // 更新显示：航向角、误差与PWM等诊断信息
        static uint32_t lastDispTick = 0;
        if ((g_controlSys.tickCount - lastDispTick) >= 50) {
            lastDispTick = g_controlSys.tickCount;
            if (showPinMenu) {
                Main_ShowPinPage(pinPage);
            } else {
                OLED_ShowNum(1, 7, (uint32_t)(g_controlSys.tickCount % 10000), 4);
                OLED_ShowNum(1, 14, (uint32_t)(g_icmReadOkCount % 1000), 3);
                OLED_ShowNum(2, 2, (uint32_t)(g_icmReadFailCount % 1000), 3);

                OLED_ShowSignedNum(2, 7, (int32_t)(g_controlSys.icm.yaw * 10.0f), 4);
                OLED_ShowSignedNum(2, 13, (int32_t)(g_controlSys.angleErr * 10.0f), 3);
                OLED_ShowSignedNum(3, 2, (int32_t)(g_controlSys.angleOut * 10.0f), 3);
                OLED_ShowSignedNum(3, 8, (int32_t)g_controlSys.leftPWM, 3);
                OLED_ShowSignedNum(3, 13, (int32_t)g_controlSys.rightPWM, 3);
                snprintf(line4, sizeof(line4), "R%uW%02XA%02XS%uP%02X",
                         (unsigned)g_controlSys.isRunning,
                         (unsigned)ICM42688_GetWhoAmI(),
                         (unsigned)ICM42688_GetI2CAddr(),
                         (unsigned)ICM42688_GetInitStage(),
                         (unsigned)ICM42688_GetLastProbeAddr());
                OLED_ShowString(4, 1, line4);
            }
        }
    }
}