#include "stm32f10x.h"
#include "Encoder_Timer.h"

// 编码器参数
#define ENCODER_PPR        11      // 线数
#define ENCODER_RATIO      30      // 减速比
#define ENCODER_EDGES      (ENCODER_PPR * 4)  // 44边沿/圈

// 速度计算变量
static int16_t lastLeftCount = 0;
static int16_t lastRightCount = 0;

// 初始化 TIM2/TIM3 编码器模式
void Encoder_Timer_Init(void) {
    // 开启时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    
    // 配置 GPIO（PA0/PA1 -> TIM2, PA6/PA7 -> TIM3）
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  // 上拉输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // TIM2 编码器模式（左轮）
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = 65535;  // ARR
    TIM_TimeBaseInitStructure.TIM_Prescaler = 0;   // 不分频
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);
    
    // 编码器接口配置
    TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI12, 
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    
    // TIM3 编码器模式（右轮）
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    
    // 使能定时器
    TIM_Cmd(TIM2, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
    
    // 清零计数
    TIM_SetCounter(TIM2, 0);
    TIM_SetCounter(TIM3, 0);
}

// 读取左轮计数
int16_t Encoder_GetLeft(void) {
    return (int16_t)TIM_GetCounter(TIM2);
}

// 读取右轮计数
int16_t Encoder_GetRight(void) {
    return (int16_t)TIM_GetCounter(TIM3);
}

// 更新速度
void Encoder_UpdateSpeed(Encoder_Data_t *data, uint16_t periodMs) {
    int16_t currentLeft = Encoder_GetLeft();
    int16_t currentRight = Encoder_GetRight();
    
    // 计算速度（当前计数 - 上次计数）
    data->leftSpeed = currentLeft - lastLeftCount;
    data->rightSpeed = currentRight - lastRightCount;
    
    // 累计计数
    data->leftCount += data->leftSpeed;
    data->rightCount += data->rightSpeed;
    
    // 计算转速
    data->leftRPM = Encoder_CountToRPM(data->leftSpeed, periodMs);
    data->rightRPM = Encoder_CountToRPM(data->rightSpeed, periodMs);
    
    // 保存当前值
    lastLeftCount = currentLeft;
    lastRightCount = currentRight;
}

// 获取速度差（左-右）
int16_t Encoder_GetSpeedDiff(void) {
    int16_t left = Encoder_GetLeft();
    int16_t right = Encoder_GetRight();
    return left - right;
}

// 计算转速 RPM
float Encoder_CountToRPM(int16_t countPerPeriod, uint16_t periodMs) {
    // countPerPeriod / ENCODER_EDGES = 圈数（输出轴）
    // 圈数 * (60000 / periodMs) = RPM
    if (periodMs == 0) return 0.0f;
    return (float)countPerPeriod / ENCODER_EDGES * (60000.0f / periodMs);
}
