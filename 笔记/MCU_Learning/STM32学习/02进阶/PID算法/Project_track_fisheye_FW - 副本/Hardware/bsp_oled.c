#include "bsp_oled.h"
#include "OLED.h"
#include <stdio.h>

static uint32_t s_lastExperimentId = 0u;
static uint8_t s_experimentLineValid = 0u;

void BspOled_Init(void)
{
    OLED_Init();
    OLED_Clear();
    s_experimentLineValid = 0u;
    OLED_ShowString(1, 1, "System Ready");
}

void BspOled_ShowStatus(SystemState_t state, ControlMode_t mode,
                        float yaw, float speedL, float speedR,
                        uint32_t experimentId)
{
    char buf[17];

    /* Line 1: State */
    switch (state) {
    case SYS_STOP:     OLED_ShowString(1, 1, "STOP    "); break;
    case SYS_STRAIGHT: OLED_ShowString(1, 1, "STRAIGHT"); break;
    case SYS_TRACKING:
        if (mode == MODE_TRACK3) OLED_ShowString(1, 1, "TRACK3  ");
        else OLED_ShowString(1, 1, "TRACK   ");
        break;
    case SYS_SPINNING: OLED_ShowString(1, 1, "SPIN    "); break;
    default:           OLED_ShowString(1, 1, "???     "); break;
    }

    /* Line 1 col 10: Mode indicator */
    if (mode == MODE_TRACK)
        OLED_ShowString(1, 10, "T");
    else if (mode == MODE_TRACK3)
        OLED_ShowString(1, 10, "3");
    else
        OLED_ShowString(1, 10, "S");

    /* Line 2: Yaw */
    sprintf(buf, "Y:%+7.1f", (double)yaw);
    OLED_ShowString(2, 1, buf);

    /* Line 3: Speed L/R */
    sprintf(buf, "L:%+5.0f R:%+5.0f", (double)speedL, (double)speedR);
    OLED_ShowString(3, 1, buf);

    BspOled_ShowExperimentId(experimentId);
}

void BspOled_ShowIMUInit(uint8_t stage, uint8_t addr, uint8_t failCode)
{
    char buf[17];
    s_experimentLineValid = 0u;
    OLED_ShowString(4, 1, "                ");
    sprintf(buf, "IMU:%u A:%02X F:%u", (unsigned)stage, (unsigned)addr, (unsigned)failCode);
    OLED_ShowString(4, 1, buf);
}

void BspOled_ShowExperimentId(uint32_t experimentId)
{
    char buf[17];

    if (s_experimentLineValid && s_lastExperimentId == experimentId)
        return;

    sprintf(buf, "EXP:%04lu        ", (unsigned long)experimentId);
    OLED_ShowString(4, 1, buf);
    s_lastExperimentId = experimentId;
    s_experimentLineValid = 1u;
}

void BspOled_ShowFaultCode(uint16_t code)
{
    const char *label = "NONE";
    char buf[17];

    switch (code)
    {
    case 1u: label = "HARD"; break;
    case 2u: label = "MEM";  break;
    case 3u: label = "BUS";  break;
    case 4u: label = "USG";  break;
    default: break;
    }

    OLED_ShowString(2, 1, "                ");
    if (code == 0u)
        sprintf(buf, "FLT:%s", label);
    else
        sprintf(buf, "FLT:%s(%u)", label, (unsigned)code);
    OLED_ShowString(2, 1, buf);
}

void BspOled_Clear(void)
{
    OLED_Clear();
    s_experimentLineValid = 0u;
}

void BspOled_SetFullOn(uint8_t enable)
{
    OLED_SetEntireDisplayOn(enable ? 1u : 0u);
}
