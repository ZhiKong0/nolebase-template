#include "DrvTrackSensor.h"
#include "BoardConfig.h"

static GPIO_TypeDef * const s_trackPorts[8] = {
    BOARD_TRACK_S1_PORT,
    BOARD_TRACK_S2_PORT,
    BOARD_TRACK_S3_PORT,
    BOARD_TRACK_S4_PORT,
    BOARD_TRACK_S5_PORT,
    BOARD_TRACK_S6_PORT,
    BOARD_TRACK_S7_PORT,
    BOARD_TRACK_S8_PORT
};

static const uint16_t s_trackPins[8] = {
    BOARD_TRACK_S1_PIN,
    BOARD_TRACK_S2_PIN,
    BOARD_TRACK_S3_PIN,
    BOARD_TRACK_S4_PIN,
    BOARD_TRACK_S5_PIN,
    BOARD_TRACK_S6_PIN,
    BOARD_TRACK_S7_PIN,
    BOARD_TRACK_S8_PIN
};

static const float s_trackWeights[8] = {
    -3.5f, -2.5f, -1.5f, -0.5f, 0.5f, 1.5f, 2.5f, 3.5f
};

static uint16_t s_lostFrames = 0u;
static int8_t s_lastDirection = 0;

static uint8_t drv_track_is_active(GPIO_TypeDef *port, uint16_t pin)
{
    uint8_t level = (GPIO_ReadInputDataBit(port, pin) == Bit_SET) ? 1u : 0u;
#if BOARD_TRACK_ACTIVE_LOW
    return level ? 0u : 1u;
#else
    return level;
#endif
}

void DrvTrackSensor_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.GPIO_Pin = BOARD_TRACK_S1_PIN | BOARD_TRACK_S2_PIN | BOARD_TRACK_S3_PIN;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = BOARD_TRACK_S4_PIN | BOARD_TRACK_S5_PIN | BOARD_TRACK_S6_PIN | BOARD_TRACK_S7_PIN;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin = BOARD_TRACK_S8_PIN;
    GPIO_Init(GPIOC, &gpio);

    s_lostFrames = 0u;
    s_lastDirection = 0;
}

void DrvTrackSensor_Sample(DrvTrackSensorSample_t *sample)
{
    uint8_t i;
    uint8_t bits = 0u;
    uint8_t count = 0u;
    float sum = 0.0f;

    if (sample == 0) {
        return;
    }

    for (i = 0u; i < 8u; i++) {
        if (drv_track_is_active(s_trackPorts[i], s_trackPins[i]) != 0u) {
            bits |= (uint8_t)(1u << i);
            count++;
            sum += s_trackWeights[i];
        }
    }

    sample->activeMask = bits;
    sample->activeCount = count;
    sample->hasLine = (count > 0u) ? 1u : 0u;

    if (count > 0u) {
        sample->rawPosition = sum / (float)count;
        if (sample->rawPosition > 0.05f) {
            s_lastDirection = 1;
        } else if (sample->rawPosition < -0.05f) {
            s_lastDirection = -1;
        }
        s_lostFrames = 0u;
    } else {
        sample->rawPosition = 0.0f;
        if (s_lostFrames < 0xFFFFu) {
            s_lostFrames++;
        }
    }

    sample->lostFrames = s_lostFrames;
    sample->lastDirection = s_lastDirection;
}
