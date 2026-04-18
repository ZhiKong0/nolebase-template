#include "AppFuzzy.h"

static const uint16_t s_gaussianLutQ15[33] = {
    32767u, 32512u, 31759u, 30542u, 28917u, 26953u, 24734u, 22345u,
    19874u, 17402u, 15002u, 12732u, 10638u, 8750u, 7086u, 5650u,
    4435u, 3427u, 2607u, 1952u, 1440u, 1045u, 747u, 526u,
    364u, 248u, 167u, 110u, 72u, 46u, 29u, 18u, 11u
};

static int16_t app_fuzzy_clamp_i16(int32_t value, int16_t minValue, int16_t maxValue)
{
    if (value < (int32_t)minValue) {
        return minValue;
    }
    if (value > (int32_t)maxValue) {
        return maxValue;
    }
    return (int16_t)value;
}

static uint16_t app_fuzzy_clamp_u16(int32_t value, uint16_t minValue, uint16_t maxValue)
{
    if (value < (int32_t)minValue) {
        return minValue;
    }
    if (value > (int32_t)maxValue) {
        return maxValue;
    }
    return (uint16_t)value;
}

static int16_t app_fuzzy_round_to_i16(float value)
{
    if (value >= 0.0f) {
        return (int16_t)(value + 0.5f);
    }
    return (int16_t)(value - 0.5f);
}

static uint16_t app_fuzzy_membership_q15(int16_t input, int16_t center, int16_t sigma)
{
    int32_t distance;
    int32_t index;

    if (sigma <= 0) {
        sigma = 1;
    }

    distance = (int32_t)input - (int32_t)center;
    if (distance < 0) {
        distance = -distance;
    }

    index = (distance * 8 + (sigma / 2)) / sigma;
    if (index < 0) {
        index = 0;
    }
    if (index > 32) {
        index = 32;
    }
    return s_gaussianLutQ15[index];
}

static void app_fuzzy_apply_default_axes(AppFuzzyTuner_t *tuner)
{
    static const int16_t defaultErrorCenters[APP_FUZZY_SET_COUNT] = {-30, -20, -10, 0, 10, 20, 30};
    static const int16_t defaultErrorSigmas[APP_FUZZY_SET_COUNT] = {8, 6, 4, 3, 4, 6, 8};
    static const int16_t defaultRateCenters[APP_FUZZY_SET_COUNT] = {-20, -13, -7, 0, 7, 13, 20};
    static const int16_t defaultRateSigmas[APP_FUZZY_SET_COUNT] = {6, 5, 3, 2, 3, 5, 6};
    uint8_t i;

    for (i = 0u; i < APP_FUZZY_SET_COUNT; i++) {
        tuner->errorAxis.centers[i] = defaultErrorCenters[i];
        tuner->errorAxis.sigmas[i] = defaultErrorSigmas[i];
        tuner->errorRateAxis.centers[i] = defaultRateCenters[i];
        tuner->errorRateAxis.sigmas[i] = defaultRateSigmas[i];
    }
}

static void app_fuzzy_apply_default_rules(AppFuzzyTuner_t *tuner)
{
    uint8_t errorIndex;
    uint8_t rateIndex;

    for (errorIndex = 0u; errorIndex < APP_FUZZY_SET_COUNT; errorIndex++) {
        for (rateIndex = 0u; rateIndex < APP_FUZZY_SET_COUNT; rateIndex++) {
            uint8_t ruleIndex = (uint8_t)(errorIndex * APP_FUZZY_SET_COUNT + rateIndex);
            int16_t errorMag = (int16_t)((errorIndex > 3u) ? (errorIndex - 3u) : (3u - errorIndex));
            int16_t rateMag = (int16_t)((rateIndex > 3u) ? (rateIndex - 3u) : (3u - rateIndex));
            int32_t kp = 900 + errorMag * 220 + rateMag * 70;
            int32_t ki = 1600 - errorMag * 220 - rateMag * 90;
            int32_t kd = 650 + rateMag * 260 + errorMag * 40;

            tuner->rules[ruleIndex].kpMilli = app_fuzzy_clamp_u16(kp, 500u, 2000u);
            tuner->rules[ruleIndex].kiMilli = app_fuzzy_clamp_u16(ki, 200u, 3000u);
            tuner->rules[ruleIndex].kdMilli = app_fuzzy_clamp_u16(kd, 300u, 2500u);
        }
    }
}

void AppFuzzyTuner_Init(AppFuzzyTuner_t *tuner, const AppPidGains_t *baseGains)
{
    if (tuner == 0) {
        return;
    }

    tuner->baseGains.kp = 0.0f;
    tuner->baseGains.ki = 0.0f;
    tuner->baseGains.kd = 0.0f;
    tuner->lastGains = tuner->baseGains;

    if (baseGains != 0) {
        tuner->baseGains = *baseGains;
        tuner->lastGains = *baseGains;
    }

    app_fuzzy_apply_default_axes(tuner);
    app_fuzzy_apply_default_rules(tuner);
}

void AppFuzzyTuner_SetBaseGains(AppFuzzyTuner_t *tuner, const AppPidGains_t *baseGains)
{
    if ((tuner == 0) || (baseGains == 0)) {
        return;
    }

    tuner->baseGains = *baseGains;
}

uint8_t AppFuzzyTuner_UpdateRules(AppFuzzyTuner_t *tuner, const AppFuzzyRule_t *rules, uint16_t count)
{
    uint16_t i;

    if ((tuner == 0) || (rules == 0) || (count < APP_RULE_COUNT)) {
        return 0u;
    }

    for (i = 0u; i < APP_RULE_COUNT; i++) {
        tuner->rules[i].kpMilli = app_fuzzy_clamp_u16((int32_t)rules[i].kpMilli, 500u, 2000u);
        tuner->rules[i].kiMilli = app_fuzzy_clamp_u16((int32_t)rules[i].kiMilli, 200u, 3000u);
        tuner->rules[i].kdMilli = app_fuzzy_clamp_u16((int32_t)rules[i].kdMilli, 300u, 2500u);
    }

    return 1u;
}

void AppFuzzyTuner_SetAxes(AppFuzzyTuner_t *tuner,
                           const AppFuzzyAxisConfig_t *errorAxis,
                           const AppFuzzyAxisConfig_t *errorRateAxis)
{
    uint8_t i;

    if (tuner == 0) {
        return;
    }

    if (errorAxis != 0) {
        for (i = 0u; i < APP_FUZZY_SET_COUNT; i++) {
            tuner->errorAxis.centers[i] = errorAxis->centers[i];
            tuner->errorAxis.sigmas[i] = app_fuzzy_clamp_i16(errorAxis->sigmas[i], 1, 30);
        }
    }

    if (errorRateAxis != 0) {
        for (i = 0u; i < APP_FUZZY_SET_COUNT; i++) {
            tuner->errorRateAxis.centers[i] = errorRateAxis->centers[i];
            tuner->errorRateAxis.sigmas[i] = app_fuzzy_clamp_i16(errorRateAxis->sigmas[i], 1, 30);
        }
    }
}

void AppFuzzyTuner_Evaluate(AppFuzzyTuner_t *tuner,
                            float errorDeg,
                            float errorRateDegPerSec,
                            AppPidGains_t *outGains)
{
    uint16_t errorMembership[APP_FUZZY_SET_COUNT];
    uint16_t rateMembership[APP_FUZZY_SET_COUNT];
    uint8_t i;
    uint8_t j;
    int16_t errorInput;
    int16_t rateInput;
    uint32_t weightSum = 0u;
    uint64_t kpWeightedSum = 0u;
    uint64_t kiWeightedSum = 0u;
    uint64_t kdWeightedSum = 0u;
    uint32_t kpMilli = 1000u;
    uint32_t kiMilli = 1000u;
    uint32_t kdMilli = 1000u;

    if ((tuner == 0) || (outGains == 0)) {
        return;
    }

    errorInput = app_fuzzy_clamp_i16((int32_t)app_fuzzy_round_to_i16(errorDeg), -30, 30);
    rateInput = app_fuzzy_clamp_i16((int32_t)app_fuzzy_round_to_i16(errorRateDegPerSec), -20, 20);

    for (i = 0u; i < APP_FUZZY_SET_COUNT; i++) {
        errorMembership[i] = app_fuzzy_membership_q15(errorInput,
                                                      tuner->errorAxis.centers[i],
                                                      tuner->errorAxis.sigmas[i]);
        rateMembership[i] = app_fuzzy_membership_q15(rateInput,
                                                     tuner->errorRateAxis.centers[i],
                                                     tuner->errorRateAxis.sigmas[i]);
    }

    for (i = 0u; i < APP_FUZZY_SET_COUNT; i++) {
        for (j = 0u; j < APP_FUZZY_SET_COUNT; j++) {
            uint8_t ruleIndex = (uint8_t)(i * APP_FUZZY_SET_COUNT + j);
            uint32_t weight = ((uint32_t)errorMembership[i] * (uint32_t)rateMembership[j] + 16384u) >> 15;

            weightSum += weight;
            kpWeightedSum += (uint64_t)weight * (uint64_t)tuner->rules[ruleIndex].kpMilli;
            kiWeightedSum += (uint64_t)weight * (uint64_t)tuner->rules[ruleIndex].kiMilli;
            kdWeightedSum += (uint64_t)weight * (uint64_t)tuner->rules[ruleIndex].kdMilli;
        }
    }

    if (weightSum > 0u) {
        kpMilli = (uint32_t)(kpWeightedSum / (uint64_t)weightSum);
        kiMilli = (uint32_t)(kiWeightedSum / (uint64_t)weightSum);
        kdMilli = (uint32_t)(kdWeightedSum / (uint64_t)weightSum);
    }

    outGains->kp = tuner->baseGains.kp * ((float)kpMilli / 1000.0f);
    outGains->ki = tuner->baseGains.ki * ((float)kiMilli / 1000.0f);
    outGains->kd = tuner->baseGains.kd * ((float)kdMilli / 1000.0f);
    tuner->lastGains = *outGains;
}
