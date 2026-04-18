#include "AppLearning.h"

static float app_learning_clamp(float value, float minValue, float maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static float app_learning_slew(float current, float target, float maxStep)
{
    if (target > (current + maxStep)) {
        return current + maxStep;
    }
    if (target < (current - maxStep)) {
        return current - maxStep;
    }
    return target;
}

static void app_learning_limit_heading(AppPidGains_t *gains)
{
    gains->kp = app_learning_clamp(gains->kp, 0.10f, 6.00f);
    gains->ki = app_learning_clamp(gains->ki, 0.00f, 2.00f);
    gains->kd = app_learning_clamp(gains->kd, 0.00f, 2.00f);
}

void AppLearningManager_Init(AppLearningManager_t *manager, const AppPidGains_t *fixedHeadingGains)
{
    if (manager == 0) {
        return;
    }

    manager->tuneMode = APP_TUNE_MODE_FIXED;
    manager->fixedHeadingGains.kp = 0.0f;
    manager->fixedHeadingGains.ki = 0.0f;
    manager->fixedHeadingGains.kd = 0.0f;
    manager->activeHeadingGains = manager->fixedHeadingGains;
    manager->hostHeadingTarget = manager->fixedHeadingGains;
    manager->hostHeadingValid = 0u;
    manager->kpSlewPerSecond = 1.20f;
    manager->kiSlewPerSecond = 0.60f;
    manager->kdSlewPerSecond = 0.80f;
    manager->baselineWindowIse = 0.0f;
    manager->retrainRequested = 0u;

    if (fixedHeadingGains != 0) {
        manager->fixedHeadingGains = *fixedHeadingGains;
        app_learning_limit_heading(&manager->fixedHeadingGains);
        manager->activeHeadingGains = manager->fixedHeadingGains;
        manager->hostHeadingTarget = manager->fixedHeadingGains;
    }
}

void AppLearningManager_SetTuneMode(AppLearningManager_t *manager, AppTuneMode_t tuneMode)
{
    if (manager == 0) {
        return;
    }

    manager->tuneMode = tuneMode;
}

void AppLearningManager_SetFixedHeadingGains(AppLearningManager_t *manager, const AppPidGains_t *fixedHeadingGains)
{
    if ((manager == 0) || (fixedHeadingGains == 0)) {
        return;
    }

    manager->fixedHeadingGains = *fixedHeadingGains;
    app_learning_limit_heading(&manager->fixedHeadingGains);
    if (manager->tuneMode == APP_TUNE_MODE_FIXED) {
        manager->activeHeadingGains = manager->fixedHeadingGains;
    }
}

void AppLearningManager_RequestHeadingGains(AppLearningManager_t *manager, const AppPidGains_t *headingGains)
{
    if ((manager == 0) || (headingGains == 0)) {
        return;
    }

    manager->hostHeadingTarget = *headingGains;
    app_learning_limit_heading(&manager->hostHeadingTarget);
    manager->hostHeadingValid = 1u;
}

void AppLearningManager_ClearRetrainRequest(AppLearningManager_t *manager)
{
    if (manager == 0) {
        return;
    }

    manager->retrainRequested = 0u;
}

void AppLearningManager_Update(AppLearningManager_t *manager,
                               const AppPidGains_t *fuzzyHeadingGains,
                               const AppPerformance_t *performance,
                               float dtSeconds,
                               AppPidGains_t *outHeadingGains)
{
    AppPidGains_t target;

    if ((manager == 0) || (fuzzyHeadingGains == 0) || (outHeadingGains == 0)) {
        return;
    }

    target = manager->fixedHeadingGains;
    if (manager->tuneMode == APP_TUNE_MODE_FUZZY) {
        target = *fuzzyHeadingGains;
    } else if (manager->tuneMode == APP_TUNE_MODE_LEARNING) {
        if (manager->hostHeadingValid != 0u) {
            target = manager->hostHeadingTarget;
        } else {
            target = *fuzzyHeadingGains;
        }
    }
    app_learning_limit_heading(&target);

    manager->activeHeadingGains.kp = app_learning_slew(manager->activeHeadingGains.kp,
                                                       target.kp,
                                                       manager->kpSlewPerSecond * dtSeconds);
    manager->activeHeadingGains.ki = app_learning_slew(manager->activeHeadingGains.ki,
                                                       target.ki,
                                                       manager->kiSlewPerSecond * dtSeconds);
    manager->activeHeadingGains.kd = app_learning_slew(manager->activeHeadingGains.kd,
                                                       target.kd,
                                                       manager->kdSlewPerSecond * dtSeconds);
    app_learning_limit_heading(&manager->activeHeadingGains);

    if (performance != 0) {
        if ((manager->baselineWindowIse <= 0.0f) && (performance->windowIse > 0.0f) && (performance->settlingReached != 0u)) {
            manager->baselineWindowIse = performance->windowIse;
        } else if ((manager->baselineWindowIse > 0.0f) && (performance->windowIse < manager->baselineWindowIse * 0.85f)) {
            manager->baselineWindowIse = performance->windowIse;
        } else if ((manager->baselineWindowIse > 0.0f) && (performance->windowIse > manager->baselineWindowIse * 1.20f)) {
            manager->retrainRequested = 1u;
        }
    }

    *outHeadingGains = manager->activeHeadingGains;
}
