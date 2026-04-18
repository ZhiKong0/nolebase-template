#include "VOFA.h"
#include "usart.h"

void VOFA_SendJustFloat5(float ch0, float ch1, float ch2, float ch3, float ch4)
{
    union { float f; uint8_t b[4]; } u;
    uint8_t tail0, tail1, tail2, tail3;

    tail0 = 0x00u;
    tail1 = 0x00u;
    tail2 = 0x80u;
    tail3 = 0x7Fu;

    u.f = ch0; USART2_SendByte(u.b[0]); USART2_SendByte(u.b[1]); USART2_SendByte(u.b[2]); USART2_SendByte(u.b[3]);
    u.f = ch1; USART2_SendByte(u.b[0]); USART2_SendByte(u.b[1]); USART2_SendByte(u.b[2]); USART2_SendByte(u.b[3]);
    u.f = ch2; USART2_SendByte(u.b[0]); USART2_SendByte(u.b[1]); USART2_SendByte(u.b[2]); USART2_SendByte(u.b[3]);
    u.f = ch3; USART2_SendByte(u.b[0]); USART2_SendByte(u.b[1]); USART2_SendByte(u.b[2]); USART2_SendByte(u.b[3]);
    u.f = ch4; USART2_SendByte(u.b[0]); USART2_SendByte(u.b[1]); USART2_SendByte(u.b[2]); USART2_SendByte(u.b[3]);

    USART2_SendByte(tail0);
    USART2_SendByte(tail1);
    USART2_SendByte(tail2);
    USART2_SendByte(tail3);
}

void VOFA_SendJustFloat4(float ch0, float ch1, float ch2, float ch3)
{
    union { float f; uint8_t b[4]; } u;
    uint8_t tail[4] = {0x00, 0x00, 0x80, 0x7F};
    uint8_t i;

    u.f = ch0; for (i = 0; i < 4; i++) USART2_SendByte(u.b[i]);
    u.f = ch1; for (i = 0; i < 4; i++) USART2_SendByte(u.b[i]);
    u.f = ch2; for (i = 0; i < 4; i++) USART2_SendByte(u.b[i]);
    u.f = ch3; for (i = 0; i < 4; i++) USART2_SendByte(u.b[i]);

    USART2_SendBuffer(tail, 4);
}
