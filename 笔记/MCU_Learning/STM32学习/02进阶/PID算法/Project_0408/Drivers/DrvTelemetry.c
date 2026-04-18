#include "DrvTelemetry.h"
#include "VOFA.h"

void DrvTelemetry_Init(void)
{
    VOFA_Init();
}

void DrvTelemetry_SendText(const char *text)
{
    VOFA_SendString(text);
}

void DrvTelemetry_SendFloat3(float ch0, float ch1, float ch2)
{
    VOFA_SendJustFloat3(ch0, ch1, ch2);
}

void DrvTelemetry_SendFloat5(float ch0, float ch1, float ch2, float ch3, float ch4)
{
    VOFA_SendJustFloat5(ch0, ch1, ch2, ch3, ch4);
}

uint8_t DrvTelemetry_TryReadCommand(char *out, uint8_t outSize)
{
    return VOFA_TakeCommand(out, outSize);
}
