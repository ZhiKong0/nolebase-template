#ifndef __APP_FUZZY_H
#define __APP_FUZZY_H

#include "AppTypes.h"

typedef struct {
    AppFuzzyAxisConfig_t errorAxis;
    AppFuzzyAxisConfig_t errorRateAxis;
    AppFuzzyRule_t rules[APP_RULE_COUNT];
    AppPidGains_t baseGains;
    AppPidGains_t lastGains;
} AppFuzzyTuner_t;

void AppFuzzyTuner_Init(AppFuzzyTuner_t *tuner, const AppPidGains_t *baseGains);
void AppFuzzyTuner_SetBaseGains(AppFuzzyTuner_t *tuner, const AppPidGains_t *baseGains);
uint8_t AppFuzzyTuner_UpdateRules(AppFuzzyTuner_t *tuner, const AppFuzzyRule_t *rules, uint16_t count);
void AppFuzzyTuner_SetAxes(AppFuzzyTuner_t *tuner,
                           const AppFuzzyAxisConfig_t *errorAxis,
                           const AppFuzzyAxisConfig_t *errorRateAxis);
void AppFuzzyTuner_Evaluate(AppFuzzyTuner_t *tuner,
                            float errorDeg,
                            float errorRateDegPerSec,
                            AppPidGains_t *outGains);

#endif
