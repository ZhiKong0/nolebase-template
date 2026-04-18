/**
 * @file Encoder_Timer.h
 * @brief 编码器驱动实现
 * @description 使用定时器编码器模式读取左右轮编码器
 */

#include "Encoder_Timer.h"

/*===========================================================================
 * 私有定义
 *========================================================================*/

// 默认配置
#define ENCODER_PULSES           390         // 每轮脉冲数（需根据实际编码器调整）
#define ENCODER_MAX_COUNT        65535       // 16位计数器最大值

// 左编码器引脚（TIM4）
#define ENCODER_LEFT_TIM         TIM4
#define ENCODER_LEFT_PIN_A       GPIO_Pin_6     // PD6 - TIM4_CH1
#define ENCODER_LEFT_PIN_B       GPIO_Pin_7     // PD7 - TIM4_CH2
#define ENCODER_LEFT_PORT        GPIOD

// 右编码器引脚（TIM5）
#define ENCODER_RIGHT_TIM        TIM5
#define ENCODER_RIGHT_PIN_A      GPIO_Pin_0     // PA0 - TIM5_CH1
#define ENCODER_RIGHT_PIN_B      GPIO_Pin_1     // PA1 - TIM5_CH2
#define ENCODER_RIGHT_PORT       GPIOA

// 速度计算参数
#define ENCODER_SPEED_SAMPLE_MS  10          // 速度采样周期（ms）
#define ENCODER_SPEED_SCALE      1           // 速度缩放因子

/*===========================================================================
 * 私有变量
 *========================================================================*/

static EncoderConfig_t g_encoderConfig;
static EncoderData_t g_encoderData;
static int16_t g_leftCountPrev = 0;
static int16_t g_rightCountPrev = 0;
static int16_t g_leftSpeedRaw = 0;
static int16_t g_rightSpeedRaw = 0;

/*===========================================================================
 * 私有函数
 *========================================================================*/

/**
 * @brief 定时器编码器模式初始化
 */
static void encoder_tim_init(TIM_TypeDef* tim, uint8_t mode, uint16_t filter) {
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_ICInitTypeDef TIM_ICInitStructure;
    
    // 定时器基础配置
    TIM_TimeBaseStructure.TIM_Period = ENCODER_MAX_COUNT - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = 0;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(tim, &TIM_TimeBaseStructure);
    
    // 设置编码器模式
    TIM_EncoderInterfaceConfig(tim, mode, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    
    // 使能ARR预加载
    TIM_ARRPreloadConfig(tim, ENABLE);
    
    // 使能定时器
    TIM_Cmd(tim, ENABLE);
}

/**
 * @brief GPIO初始化
 */
static void encoder_gpio_init(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    
    // 使能GPIO时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOD, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4 | RCC_APB1Periph_TIM5, ENABLE);
    
    // 左编码器引脚
    GPIO_InitStructure.GPIO_Pin = ENCODER_LEFT_PIN_A | ENCODER_LEFT_PIN_B;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ENCODER_LEFT_PORT, &GPIO_InitStructure);
    
    // 右编码器引脚
    GPIO_InitStructure.GPIO_Pin = ENCODER_RIGHT_PIN_A | ENCODER_RIGHT_PIN_B;
    GPIO_Init(ENCODER_RIGHT_PORT, &GPIO_InitStructure);
}

/*===========================================================================
 * 公共API实现
 *========================================================================*/

void Encoder_Timer_Init(void) {
    // 初始化默认配置
    g_encoderConfig.timLeft = ENCODER_LEFT_TIM;
    g_encoderConfig.timRight = ENCODER_RIGHT_TIM;
    g_encoderConfig.encoderMode = TIM_EncoderMode_TI12;
    g_encoderConfig.filter = 0;
    g_encoderConfig.maxCount = ENCODER_MAX_COUNT;
    g_encoderConfig.revolutionPulses = ENCODER_PULSES;
    
    // 初始化状态
    g_encoderData.leftCount = 0;
    g_encoderData.rightCount = 0;
    g_encoderData.leftDelta = 0;
    g_encoderData.rightDelta = 0;
    g_encoderData.leftSpeed = 0;
    g_encoderData.rightSpeed = 0;
    g_encoderData.leftDir = 1;
    g_encoderData.rightDir = 1;
    g_encoderData.initialized = HW_TRUE;
    
    g_leftCountPrev = 0;
    g_rightCountPrev = 0;
    
    // 初始化硬件
    encoder_gpio_init();
    encoder_tim_init(ENCODER_LEFT_TIM, TIM_EncoderMode_TI12, 0);
    encoder_tim_init(ENCODER_RIGHT_TIM, TIM_EncoderMode_TI12, 0);
}

void Encoder_Timer_InitWithConfig(EncoderConfig_t *config) {
    if (!config) {
        Encoder_Timer_Init();
        return;
    }
    
    g_encoderConfig = *config;
    
    g_encoderData.initialized = HW_TRUE;
    g_leftCountPrev = 0;
    g_rightCountPrev = 0;
    
    encoder_gpio_init();
    encoder_tim_init(g_encoderConfig.timLeft, g_encoderConfig.encoderMode, g_encoderConfig.filter);
    encoder_tim_init(g_encoderConfig.timRight, g_encoderConfig.encoderMode, g_encoderConfig.filter);
}

void Encoder_ReadCount(int16_t *leftCount, int16_t *rightCount) {
    if (!leftCount || !rightCount) return;
    
    *leftCount = (int16_t)g_encoderConfig.timLeft->CNT;
    *rightCount = (int16_t)g_encoderConfig.timRight->CNT;
}

void Encoder_ReadDelta(int16_t *leftDelta, int16_t *rightDelta) {
    if (!leftDelta || !rightDelta) return;
    
    *leftDelta = g_encoderData.leftDelta;
    *rightDelta = g_encoderData.rightDelta;
}

void Encoder_UpdateSpeed(int16_t periodMs) {
    int16_t leftCount, rightCount;
    int16_t leftDelta, rightDelta;
    int16_t maxCount;
    
    // 读取当前计数
    leftCount = (int16_t)g_encoderConfig.timLeft->CNT;
    rightCount = (int16_t)g_encoderConfig.timRight->CNT;
    
    // 计算增量（处理溢出）
    maxCount = g_encoderConfig.maxCount;
    
    // 左轮增量
    if (leftCount >= g_leftCountPrev) {
        leftDelta = leftCount - g_leftCountPrev;
    } else {
        leftDelta = (maxCount - g_leftCountPrev) + leftCount;
    }
    if (leftDelta > maxCount / 2) {
        leftDelta -= maxCount;
    }
    
    // 右轮增量
    if (rightCount >= g_rightCountPrev) {
        rightDelta = rightCount - g_rightCountPrev;
    } else {
        rightDelta = (maxCount - g_rightCountPrev) + rightCount;
    }
    if (rightDelta > maxCount / 2) {
        rightDelta -= maxCount;
    }
    
    // 应用方向
    leftDelta *= g_encoderData.leftDir;
    rightDelta *= g_encoderData.rightDir;
    
    // 保存状态
    g_encoderData.leftCount = leftCount;
    g_encoderData.rightCount = rightCount;
    g_encoderData.leftDelta = leftDelta;
    g_encoderData.rightDelta = rightDelta;
    
    // 保存限幅后的值（防止异常值）
    if (leftDelta > 500) leftDelta = 500;
    if (leftDelta < -500) leftDelta = -500;
    if (rightDelta > 500) rightDelta = 500;
    if (rightDelta < -500) rightDelta = -500;
    
    g_encoderData.leftDeltaClamped = leftDelta;
    g_encoderData.rightDeltaClamped = rightDelta;
    
    // 计算速度（脉冲/控制周期）
    // 注意：这里需要根据控制周期进行调整
    if (periodMs > 0 && periodMs != ENCODER_SPEED_SAMPLE_MS) {
        float scale = (float)ENCODER_SPEED_SAMPLE_MS / (float)periodMs;
        g_encoderData.leftSpeed = (int16_t)((float)leftDelta * scale);
        g_encoderData.rightSpeed = (int16_t)((float)rightDelta * scale);
    } else {
        g_encoderData.leftSpeed = leftDelta;
        g_encoderData.rightSpeed = rightDelta;
    }
    
    // 更新previous count
    g_leftCountPrev = leftCount;
    g_rightCountPrev = rightCount;
}

EncoderData_t* Encoder_GetData(void) {
    return &g_encoderData;
}

void Encoder_SetLeftSign(int8_t dir) {
    if (dir != 0) {
        g_encoderData.leftDir = (dir > 0) ? 1 : -1;
    }
}

void Encoder_SetRightSign(int8_t dir) {
    if (dir != 0) {
        g_encoderData.rightDir = (dir > 0) ? 1 : -1;
    }
}

void Encoder_Reset(void) {
    g_encoderConfig.timLeft->CNT = 0;
    g_encoderConfig.timRight->CNT = 0;
    
    g_leftCountPrev = 0;
    g_rightCountPrev = 0;
    
    g_encoderData.leftDelta = 0;
    g_encoderData.rightDelta = 0;
    g_encoderData.leftSpeed = 0;
    g_encoderData.rightSpeed = 0;
    g_encoderData.leftDeltaClamped = 0;
    g_encoderData.rightDeltaClamped = 0;
}

void Encoder_SetMaxCount(int16_t maxCount) {
    g_encoderConfig.maxCount = maxCount;
}
