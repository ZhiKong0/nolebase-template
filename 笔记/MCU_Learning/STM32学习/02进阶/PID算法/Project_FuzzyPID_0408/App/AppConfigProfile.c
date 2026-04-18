#include "AppConfigProfile.h"
#include "DrvParamStore.h"
#include "AppMotionSupervisor.h"
#include <string.h>

#define APP_CONFIG_PROFILE_VERSION      1u

typedef struct {
    uint32_t version;
    uint8_t runMode;
    uint8_t tuneMode;
    uint8_t reserved0;
    uint8_t reserved1;
    float targetSpeedRpm;
    float headingTrimDeg;
    float trackHeadingGainDegPerUnit;
    AppPidGains_t headingPid;
    AppPidGains_t speedPid;
    AppFuzzyRule_t rules[APP_RULE_COUNT];
    AppFuzzyAxisConfig_t errorAxis;
    AppFuzzyAxisConfig_t rateAxis;
} AppConfigProfileBlob_t;

static void app_config_profile_clear_flags(AppRuntimeContext_t *runtime)
{
    if (runtime == 0) {
        return;
    }

    runtime->profileStored = 0u;
    runtime->profileDirty = 0u;
}

static void app_config_profile_capture(const AppRuntimeContext_t *runtime, AppConfigProfileBlob_t *profile)
{
    if ((runtime == 0) || (profile == 0)) {
        return;
    }

    memset(profile, 0, sizeof(*profile));
    profile->version = APP_CONFIG_PROFILE_VERSION;
    profile->runMode = (uint8_t)runtime->runMode;
    profile->tuneMode = (uint8_t)runtime->learning.tuneMode;
    profile->targetSpeedRpm = runtime->baseTargetSpeedRpm;
    profile->headingTrimDeg = runtime->headingTrimDeg;
    profile->trackHeadingGainDegPerUnit = runtime->trackHeadingGainDegPerUnit;
    profile->headingPid = runtime->learning.fixedHeadingGains;
    profile->speedPid = runtime->speedGains;
    memcpy(profile->rules, runtime->fuzzyTuner.rules, sizeof(profile->rules));
    profile->errorAxis = runtime->fuzzyTuner.errorAxis;
    profile->rateAxis = runtime->fuzzyTuner.errorRateAxis;
}

static uint8_t app_config_profile_is_valid(const AppConfigProfileBlob_t *profile)
{
    if (profile == 0) {
        return 0u;
    }

    if ((profile->version != APP_CONFIG_PROFILE_VERSION) ||
        (profile->runMode >= (uint8_t)APP_RUN_MODE_COUNT) ||
        (profile->tuneMode >= (uint8_t)APP_TUNE_MODE_COUNT)) {
        return 0u;
    }

    return 1u;
}

static void app_config_profile_apply(AppRuntimeContext_t *runtime, const AppConfigProfileBlob_t *profile)
{
    if ((runtime == 0) || (profile == 0)) {
        return;
    }

    (void)AppMotionSupervisor_SetRunMode(runtime, (AppRunMode_t)profile->runMode);
    AppMotionSupervisor_SetTuneMode(runtime, (AppTuneMode_t)profile->tuneMode);
    AppMotionSupervisor_SetTargetSpeed(runtime, profile->targetSpeedRpm);
    AppMotionSupervisor_SetHeadingTrim(runtime, profile->headingTrimDeg);
    AppMotionSupervisor_SetTrackHeadingGain(runtime, profile->trackHeadingGainDegPerUnit);
    AppMotionSupervisor_SetHeadingPid(runtime, &profile->headingPid);
    AppMotionSupervisor_SetSpeedPid(runtime, &profile->speedPid);
    AppMotionSupervisor_SetRuleTable(runtime, profile->rules, APP_RULE_COUNT);
    AppMotionSupervisor_SetMembershipAxes(runtime, &profile->errorAxis, &profile->rateAxis);
    runtime->profileStored = 1u;
    runtime->profileDirty = 0u;
}

uint8_t AppConfigProfile_LoadStartup(AppRuntimeContext_t *runtime)
{
    AppConfigProfileBlob_t profile;

    app_config_profile_clear_flags(runtime);
    if ((runtime == 0) || (runtime->state != APP_STATE_IDLE)) {
        return 0u;
    }

    if (DrvParamStore_Load(&profile, (uint16_t)sizeof(profile)) == 0u) {
        return 0u;
    }

    if (app_config_profile_is_valid(&profile) == 0u) {
        return 0u;
    }

    app_config_profile_apply(runtime, &profile);
    return 1u;
}

uint8_t AppConfigProfile_LoadSaved(AppRuntimeContext_t *runtime)
{
    return AppConfigProfile_LoadStartup(runtime);
}

uint8_t AppConfigProfile_SaveCurrent(AppRuntimeContext_t *runtime)
{
    AppConfigProfileBlob_t profile;

    if ((runtime == 0) || (runtime->state != APP_STATE_IDLE)) {
        return 0u;
    }

    app_config_profile_capture(runtime, &profile);
    if (DrvParamStore_Save(&profile, (uint16_t)sizeof(profile)) == 0u) {
        return 0u;
    }

    runtime->profileStored = 1u;
    runtime->profileDirty = 0u;
    return 1u;
}
