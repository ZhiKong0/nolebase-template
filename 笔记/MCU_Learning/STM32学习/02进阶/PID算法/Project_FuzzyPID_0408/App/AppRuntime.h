#ifndef __APP_RUNTIME_H
#define __APP_RUNTIME_H

#include "AppTypes.h"
#include "AppPid.h"
#include "AppFuzzy.h"
#include "AppLearning.h"
#include "AppPerformance.h"
#include "AppProtocol.h"

typedef enum {
    APP_FAULT_NONE = 0u,
    APP_FAULT_IMU_INIT = 1u,
    APP_FAULT_IMU_LOST = 2u,
    APP_FAULT_BAD_COMMAND = 3u
} AppFaultCode_t;

typedef struct {
    AppRunMode_t runMode;
    AppState_t state;
    uint32_t tickMs;
    uint32_t lastControlMs;
    uint32_t lastFuzzyMs;
    uint32_t lastTelemetryMs;
    uint32_t lastDisplayMs;
    uint16_t faultCode;
    uint16_t imuMissTicks;
    AppControlSnapshot_t snapshot;
    AppMotorCommand_t motorCommand;
    AppPidController_t headingPid;
    AppPidController_t forwardSpeedPid;
    AppPidController_t leftSpeedPid;
    AppPidController_t rightSpeedPid;
    AppFuzzyTuner_t fuzzyTuner;
    AppLearningManager_t learning;
    AppPerformanceMonitor_t performance;
    AppTelemetryFrame_t telemetry;
    AppPidGains_t speedGains;
    AppPidGains_t appliedHeadingGains;
    float baseTargetSpeedRpm;
    float headingLockDeg;
    float headingTrimDeg;
    float trackHeadingGainDegPerUnit;
    float maxWheelSpeedRpm;
    float maxSteeringRpm;
    float filteredLeftSpeedRpm;
    float filteredRightSpeedRpm;
    float filteredAvgSpeedRpm;
    float forwardSpeedEstimateRpm;
    float speedFilterAlpha;
    float prevAppliedLeftPwm;
    float prevAppliedRightPwm;
    float prevHeadingErrorDeg;
    float currentHeadingErrorDeg;
    float currentHeadingRateDegPerSec;
    int32_t straightLeftCountAccum;
    int32_t straightRightCountAccum;
    int16_t maxPwm;
    uint8_t straightSpeedWindowTicks;
    uint8_t profileStored;
    uint8_t profileDirty;
} AppRuntimeContext_t;

#endif
