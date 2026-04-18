#include "DrvDisplay.h"
#include "OLED.h"

void DrvDisplay_Init(void)
{
    OLED_Init();
}

void DrvDisplay_ShowLine(uint8_t line, const char *text)
{
    uint8_t i;
    char buf[17];

    for (i = 0u; i < 16u; i++) {
        buf[i] = ' ';
    }
    buf[16] = '\0';

    if (text != 0) {
        for (i = 0u; i < 16u && text[i] != '\0'; i++) {
            buf[i] = text[i];
        }
    }

    OLED_ShowString(line, 1u, buf);
}
