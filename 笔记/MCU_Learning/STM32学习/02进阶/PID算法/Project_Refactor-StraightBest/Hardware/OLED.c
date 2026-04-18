#include "stm32f10x.h"
#include "config.h"
#include "OLED.h"
#include "OLED_Font.h"

static void oled_delay(void) {
    for (volatile uint8_t i = 0; i < 12; i++) {
    }
}

static void OLED_W_SCL(uint8_t x) {
    GPIO_WriteBit(OLED_SCL_PORT, OLED_SCL_PIN, (BitAction)(x ? Bit_SET : Bit_RESET));
}

static void OLED_W_SDA(uint8_t x) {
    GPIO_WriteBit(OLED_SDA_PORT, OLED_SDA_PIN, (BitAction)(x ? Bit_SET : Bit_RESET));
}

static void OLED_I2C_Init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef g;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Pin = OLED_SCL_PIN;
    GPIO_Init(OLED_SCL_PORT, &g);
    g.GPIO_Pin = OLED_SDA_PIN;
    GPIO_Init(OLED_SDA_PORT, &g);

    OLED_W_SCL(1);
    OLED_W_SDA(1);
}

static void OLED_I2C_Start(void) {
    OLED_W_SDA(1);
    OLED_W_SCL(1);
    oled_delay();
    OLED_W_SDA(0);
    oled_delay();
    OLED_W_SCL(0);
}

static void OLED_I2C_Stop(void) {
    OLED_W_SDA(0);
    OLED_W_SCL(1);
    oled_delay();
    OLED_W_SDA(1);
    oled_delay();
}

static void OLED_I2C_SendByte(uint8_t Byte) {
    for (uint8_t i = 0; i < 8; i++) {
        OLED_W_SDA((Byte & 0x80) ? 1 : 0);
        oled_delay();
        OLED_W_SCL(1);
        oled_delay();
        OLED_W_SCL(0);
        Byte <<= 1;
    }
    OLED_W_SCL(1);
    oled_delay();
    OLED_W_SCL(0);
}

static void OLED_WriteCommand(uint8_t Command) {
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_SendByte(0x00);
    OLED_I2C_SendByte(Command);
    OLED_I2C_Stop();
}

static void OLED_WriteData(uint8_t Data) {
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_SendByte(0x40);
    OLED_I2C_SendByte(Data);
    OLED_I2C_Stop();
}

static void OLED_SetCursor(uint8_t Y, uint8_t X) {
    OLED_WriteCommand(0xB0 | Y);
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));
    OLED_WriteCommand(0x00 | (X & 0x0F));
}

void OLED_Clear(void) {
    for (uint8_t j = 0; j < 8; j++) {
        OLED_SetCursor(j, 0);
        for (uint8_t i = 0; i < 128; i++) {
            OLED_WriteData(0x00);
        }
    }
}

static void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char) {
    uint8_t c = (uint8_t)Char;
    if (c < ' ' || c > '~') {
        c = ' ';
    }
    uint8_t idx = (uint8_t)(c - ' ');

    OLED_SetCursor((uint8_t)((Line - 1) * 2), (uint8_t)((Column - 1) * 8));
    for (uint8_t i = 0; i < 8; i++) {
        OLED_WriteData(OLED_F8x16[idx][i]);
    }

    OLED_SetCursor((uint8_t)((Line - 1) * 2 + 1), (uint8_t)((Column - 1) * 8));
    for (uint8_t i = 0; i < 8; i++) {
        OLED_WriteData(OLED_F8x16[idx][i + 8]);
    }
}

static uint32_t OLED_Pow(uint32_t X, uint32_t Y) {
    uint32_t Result = 1;
    while (Y--) {
        Result *= X;
    }
    return Result;
}

void OLED_Init(void) {
    OLED_I2C_Init();

    OLED_WriteCommand(0xAE);
    OLED_WriteCommand(0xD5);
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8);
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xD3);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0xDA);
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81);
    OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9);
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB);
    OLED_WriteCommand(0x30);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0x8D);
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0xAF);

    OLED_Clear();
}

void OLED_ShowString(uint8_t line, uint8_t col, char *str) {
    uint8_t i = 0;
    if (!str) return;
    while (str[i] != '\0') {
        OLED_ShowChar(line, (uint8_t)(col + i), str[i]);
        i++;
        if ((uint8_t)(col + i) > 16) break;
    }
}

void OLED_ShowNum(uint8_t line, uint8_t col, uint32_t num, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        uint32_t div = OLED_Pow(10, (uint32_t)(len - i - 1));
        char ch = (char)(num / div % 10 + '0');
        OLED_ShowChar(line, (uint8_t)(col + i), ch);
    }
}

void OLED_ShowSignedNum(uint8_t line, uint8_t col, int32_t num, uint8_t len) {
    uint32_t v;
    if (num >= 0) {
        OLED_ShowChar(line, col, '+');
        v = (uint32_t)num;
    } else {
        OLED_ShowChar(line, col, '-');
        v = (uint32_t)(-num);
    }
    for (uint8_t i = 0; i < len; i++) {
        uint32_t div = OLED_Pow(10, (uint32_t)(len - i - 1));
        char ch = (char)(v / div % 10 + '0');
        OLED_ShowChar(line, (uint8_t)(col + i + 1), ch);
    }
}
