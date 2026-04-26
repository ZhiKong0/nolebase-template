#include "motor_driver.h"
#include "config.h"
#include <string.h>

static int16_t abs_i16(int16_t v)
{
    return (v < 0) ? (int16_t)(-v) : v;
}

static int16_t s_actualPwmL = 0;
static int16_t s_actualPwmR = 0;

/* ========== Motor (TB6612 + TIM1 PWM) ========== */

void MotorDriver_Init(void)
{
    GPIO_InitTypeDef g;
    TIM_TimeBaseInitTypeDef tb;
    TIM_OCInitTypeDef oc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;

    g.GPIO_Pin = MOTOR_STBY_PIN;
    GPIO_Init(MOTOR_STBY_PORT, &g);
    GPIO_ResetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);

    g.GPIO_Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN;
    GPIO_Init(MOTOR_AIN1_PORT, &g);
    GPIO_ResetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN | MOTOR_AIN2_PIN);

    g.GPIO_Pin = MOTOR_BIN1_PIN;
    GPIO_Init(MOTOR_BIN1_PORT, &g);
    GPIO_ResetBits(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);

    g.GPIO_Pin = MOTOR_BIN2_PIN;
    GPIO_Init(MOTOR_BIN2_PORT, &g);
    GPIO_ResetBits(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);

    g.GPIO_Mode = GPIO_Mode_AF_PP;
    g.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOA, &g);

    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = MOTOR_PWM_PERIOD - 1;
    tb.TIM_Prescaler = MOTOR_PWM_PRESCALER - 1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(MOTOR_PWM_TIM, &tb);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = 0;
    TIM_OC1Init(MOTOR_PWM_TIM, &oc);
    TIM_OC2Init(MOTOR_PWM_TIM, &oc);

    TIM_CtrlPWMOutputs(MOTOR_PWM_TIM, ENABLE);
    TIM_Cmd(MOTOR_PWM_TIM, ENABLE);
}

void MotorDriver_Enable(void)
{
    GPIO_SetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
}

void MotorDriver_Disable(void)
{
    GPIO_ResetBits(MOTOR_STBY_PORT, MOTOR_STBY_PIN);
}

void MotorDriver_Stop(void)
{
    GPIO_ResetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN | MOTOR_AIN2_PIN);
    GPIO_ResetBits(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);
    GPIO_ResetBits(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);
    TIM_SetCompare1(MOTOR_PWM_TIM, 0);
    TIM_SetCompare2(MOTOR_PWM_TIM, 0);
    s_actualPwmL = 0;
    s_actualPwmR = 0;
}

void MotorDriver_ActiveBrake(void)
{
    MotorDriver_Enable();

    /* TB6612 short-brake mode: IN1/IN2 driven to the same level while PWM is active.
       Keep telemetry PWM at zero because this is a hold state, not a drive command. */
    GPIO_SetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
    GPIO_SetBits(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
    GPIO_SetBits(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);
    GPIO_SetBits(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);
    TIM_SetCompare1(MOTOR_PWM_TIM, MOTOR_BRAKE_PWM);
    TIM_SetCompare2(MOTOR_PWM_TIM, MOTOR_BRAKE_PWM);
    s_actualPwmL = 0;
    s_actualPwmR = 0;
}

static void motor_set_left(int16_t speed)
{
    speed = (int16_t)(speed * MOTOR_LEFT_DIR_SIGN);

    if (speed > MOTOR_PWM_PERIOD) speed = MOTOR_PWM_PERIOD;

    if (speed >= 0) {
        GPIO_SetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
        GPIO_ResetBits(MOTOR_AIN1_PORT, MOTOR_AIN2_PIN);
    } else {
        GPIO_ResetBits(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
        GPIO_SetBits(MOTOR_AIN1_PORT, MOTOR_AIN2_PIN);
        speed = -speed;
    }
    if (speed > MOTOR_PWM_PERIOD) speed = MOTOR_PWM_PERIOD;
    TIM_SetCompare1(MOTOR_PWM_TIM, (uint16_t)speed);
}

static void motor_set_right(int16_t speed)
{
    speed = (int16_t)(speed * MOTOR_RIGHT_DIR_SIGN);

    if (speed >= 0) {
        GPIO_SetBits(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);
        GPIO_ResetBits(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);
    } else {
        GPIO_ResetBits(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);
        GPIO_SetBits(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);
        speed = -speed;
    }
    if (speed > MOTOR_PWM_PERIOD) speed = MOTOR_PWM_PERIOD;
    TIM_SetCompare2(MOTOR_PWM_TIM, (uint16_t)speed);
}

static int16_t apply_deadzone(int16_t pwm)
{
    if (pwm > 0 && pwm < MOTOR_DEADZONE) return MOTOR_DEADZONE;
    if (pwm < 0 && pwm > -MOTOR_DEADZONE) return (int16_t)(-MOTOR_DEADZONE);
    return pwm;
}

void MotorDriver_SetDiffPWM(int16_t left, int16_t right, int16_t dPostDZ)
{
    (void)dPostDZ; /* legacy parameter, no longer used */
    int16_t l = apply_deadzone(left);
    int16_t r = apply_deadzone(right);
    if (l < 0) l = 0;
    if (r < 0) r = 0;
    s_actualPwmL = l;
    s_actualPwmR = r;
    motor_set_left(l);
    motor_set_right(r);
}

void MotorDriver_SetCoreDiff(int16_t core, int16_t diff)
{
    /* Apply deadzone to the BASE speed (core) only.
       Then add heading diff ON TOP so it is never swallowed by DZ.
       This ensures heading corrections work even at low speed. */
    int16_t base = apply_deadzone(core);
    int16_t l = (int16_t)(base + diff);
    int16_t r = (int16_t)(base - diff);
    /* Keep both motors non-negative (forward only when core > 0) */
    if (core > 0) {
        if (l < 0) l = 0;
        if (r < 0) r = 0;
    } else if (core < 0) {
        if (l > 0) l = 0;
        if (r > 0) r = 0;
    }
    if (l > MOTOR_PWM_MAX) l = MOTOR_PWM_MAX;
    if (l < -MOTOR_PWM_MAX) l = -MOTOR_PWM_MAX;
    if (r > MOTOR_PWM_MAX) r = MOTOR_PWM_MAX;
    if (r < -MOTOR_PWM_MAX) r = -MOTOR_PWM_MAX;
    s_actualPwmL = l;
    s_actualPwmR = r;
    motor_set_left(l);
    motor_set_right(r);
}

void MotorDriver_SetTurnPWM(int16_t left, int16_t right)
{
    /* Raw PWM for pivot turns: positive=forward, negative=backward.
       No deadzone applied — corners need precise low-speed control. */
    if (left > MOTOR_PWM_MAX) left = MOTOR_PWM_MAX;
    if (left < -MOTOR_PWM_MAX) left = -MOTOR_PWM_MAX;
    if (right > MOTOR_PWM_MAX) right = MOTOR_PWM_MAX;
    if (right < -MOTOR_PWM_MAX) right = -MOTOR_PWM_MAX;
    s_actualPwmL = left;
    s_actualPwmR = right;
    motor_set_left(left);
    motor_set_right(right);
}

int16_t MotorDriver_GetActualL(void) { return s_actualPwmL; }
int16_t MotorDriver_GetActualR(void) { return s_actualPwmR; }

/* ========== Encoder (TIM2 left, TIM3 right) ========== */

void Encoder_Init(void)
{
    GPIO_InitTypeDef g;
    TIM_TimeBaseInitTypeDef tb;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_Init(GPIOA, &g);

    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = 65535;
    tb.TIM_Prescaler = 0;
    tb.TIM_RepetitionCounter = 0;

    TIM_TimeBaseInit(ENC_LEFT_TIM, &tb);
    TIM_EncoderInterfaceConfig(ENC_LEFT_TIM, TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    ENC_LEFT_TIM->CCMR1 = (uint16_t)(
        (ENC_LEFT_TIM->CCMR1 & (uint16_t)~((uint16_t)(0x0F << 4) | (uint16_t)(0x0F << 12))) |
        (uint16_t)((ENC_IC_FILTER & 0x0F) << 4) |
        (uint16_t)((ENC_IC_FILTER & 0x0F) << 12));

    TIM_TimeBaseInit(ENC_RIGHT_TIM, &tb);
    TIM_EncoderInterfaceConfig(ENC_RIGHT_TIM, TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    ENC_RIGHT_TIM->CCMR1 = (uint16_t)(
        (ENC_RIGHT_TIM->CCMR1 & (uint16_t)~((uint16_t)(0x0F << 4) | (uint16_t)(0x0F << 12))) |
        (uint16_t)((ENC_IC_FILTER & 0x0F) << 4) |
        (uint16_t)((ENC_IC_FILTER & 0x0F) << 12));

    TIM_Cmd(ENC_LEFT_TIM, ENABLE);
    TIM_Cmd(ENC_RIGHT_TIM, ENABLE);
    TIM_SetCounter(ENC_LEFT_TIM, 0);
    TIM_SetCounter(ENC_RIGHT_TIM, 0);
}

void Encoder_Reset(void)
{
    TIM_SetCounter(ENC_LEFT_TIM, 0);
    TIM_SetCounter(ENC_RIGHT_TIM, 0);
}

void Encoder_Update(Encoder_Data_t *data)
{
    int16_t dl, dr;

    if (!data) return;

    dl = (int16_t)TIM_GetCounter(ENC_LEFT_TIM);
    TIM_SetCounter(ENC_LEFT_TIM, 0);
    dr = (int16_t)TIM_GetCounter(ENC_RIGHT_TIM);
    TIM_SetCounter(ENC_RIGHT_TIM, 0);

    dl = (int16_t)(dl * ENC_LEFT_SIGN);
    dr = (int16_t)(dr * ENC_RIGHT_SIGN);

    data->rawLeftDelta = dl;
    data->rawRightDelta = dr;
    data->leftClamped = 0;
    data->rightClamped = 0;

    if (abs_i16(dl) > (int16_t)ENC_MAX_DELTA) {
        dl = 0;
        data->leftClamped = 1;
    }
    if (abs_i16(dr) > (int16_t)ENC_MAX_DELTA) {
        dr = 0;
        data->rightClamped = 1;
    }

    data->leftSpeed = dl;
    data->rightSpeed = dr;
    data->leftCount += dl;
    data->rightCount += dr;
}
