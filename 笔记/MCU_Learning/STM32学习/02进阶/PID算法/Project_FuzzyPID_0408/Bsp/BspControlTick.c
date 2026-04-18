#include "BspControlTick.h"
#include "BoardConfig.h"

static volatile uint32_t s_tickMs = 0u;
static volatile uint16_t s_pendingTicks = 0u;

void BspControlTick_Init(void)
{
    TIM_TimeBaseInitTypeDef tb;
    NVIC_InitTypeDef irq;

    RCC_APB1PeriphClockCmd(BOARD_CONTROL_TICK_RCC, ENABLE);

    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = 1000u - 1u;
    tb.TIM_Prescaler = 72u - 1u;
    tb.TIM_RepetitionCounter = 0u;
    TIM_TimeBaseInit(BOARD_CONTROL_TICK_TIMER, &tb);

    TIM_ClearITPendingBit(BOARD_CONTROL_TICK_TIMER, TIM_IT_Update);
    TIM_ITConfig(BOARD_CONTROL_TICK_TIMER, TIM_IT_Update, ENABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    irq.NVIC_IRQChannel = BOARD_CONTROL_TICK_IRQn;
    irq.NVIC_IRQChannelPreemptionPriority = 2u;
    irq.NVIC_IRQChannelSubPriority = 0u;
    irq.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&irq);

    s_tickMs = 0u;
    s_pendingTicks = 0u;
    TIM_Cmd(BOARD_CONTROL_TICK_TIMER, ENABLE);
}

void BspControlTick_IrqHandler(void)
{
    if (TIM_GetITStatus(BOARD_CONTROL_TICK_TIMER, TIM_IT_Update) == RESET) {
        return;
    }

    TIM_ClearITPendingBit(BOARD_CONTROL_TICK_TIMER, TIM_IT_Update);
    s_tickMs++;
    if (s_pendingTicks < 1000u) {
        s_pendingTicks++;
    }
}

uint8_t BspControlTick_ConsumeOneTick(void)
{
    uint32_t primask;
    uint8_t out = 0u;

    primask = __get_PRIMASK();
    __disable_irq();
    if (s_pendingTicks > 0u) {
        s_pendingTicks--;
        out = 1u;
    }
    if (primask == 0u) {
        __enable_irq();
    }

    return out;
}

uint32_t BspControlTick_GetMillis(void)
{
    return s_tickMs;
}
