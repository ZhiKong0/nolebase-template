#ifndef __APP_LEARNING_H
#define __APP_LEARNING_H

#include "AppTypes.h"

typedef struct {
    AppTuneMode_t tuneMode;
    AppPidGains_t fixedHeadingGains;
    AppPidGains_t activeHeadingGains;
    AppPidGains_t hostHeadingTarget;
    uint8_t hostHeadingValid;
    float kpSlewPerSecond;
    float kiSlewPerSecond;
    float kdSlewPerSecond;
    float baselineWindowIse;
    uint8_t retrainRequested;
} AppLearningManager_t;

void AppLearningManager_Init(AppLearningManager_t *manager, const AppPidGains_t *fixedHeadingGains);
void AppLearningManager_SetTuneMode(AppLearningManager_t *manager, AppTuneMode_t tuneMode);
void AppLearningManager_SetFixedHeadingGains(AppLearningManager_t *manager, const AppPidGains_t *fixedHeadingGains);
void AppLearningManager_RequestHeadingGains(AppLearningManager_t *manager, const AppPidGains_t *headingGains);
void AppLearningManager_ClearRetrainRequest(AppLearningManager_t *manager);
void AppLearningManager_Update(AppLearningManager_t *manager,
                               const AppPidGains_t *fuzzyHeadingGains,
                               const AppPerformance_t *performance,
                               float dtSeconds,
                               AppPidGains_t *outHeadingGains);

#endif
