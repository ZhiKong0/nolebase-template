#ifndef __TRACKER_H
#define __TRACKER_H

#include "stm32f10x.h"

// 74HC4051 引脚定义 (完全按照你的硬件)
#define TRACKER_S0_PIN    GPIO_Pin_2
#define TRACKER_S0_PORT   GPIOA

#define TRACKER_S1_PIN    GPIO_Pin_11
#define TRACKER_S1_PORT   GPIOA

#define TRACKER_S2_PIN    GPIO_Pin_10
#define TRACKER_S2_PORT   GPIOA

#define TRACKER_Z_PIN     GPIO_Pin_3
#define TRACKER_Z_PORT    GPIOA

#define TRACKER_CHANNEL_NUM    8

// 循迹数据结构体
typedef struct {
    uint8_t raw_data;   // 原始8位数据 bit0~7 对应 Y0~Y7
    uint8_t channel[8]; // 单通道状态 0=白线 1=黑线
} Tracker_Typedef;

extern Tracker_Typedef tracker;

// 函数声明
void Tracker_Init(void);
void Tracker_Scan(void);
uint8_t Tracker_Get(uint8_t ch);
void xunji_8(void);

#endif
