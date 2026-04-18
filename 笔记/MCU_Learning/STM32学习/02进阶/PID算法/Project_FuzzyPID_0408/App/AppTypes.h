#ifndef __APP_TYPES_H
#define __APP_TYPES_H

#include "stm32f10x.h"
#include "DrvEncoder.h"
#include "DrvTrackSensor.h"
#include "DrvImuBno08x.h"

#define APP_CONTROL_PERIOD_MS      10u
#define APP_FUZZY_PERIOD_MS        50u
#define APP_TELEMETRY_PERIOD_MS    100u
#define APP_DISPLAY_PERIOD_MS      100u
#define APP_RULE_COUNT             49u
#define APP_FUZZY_SET_COUNT        7u

typedef enum {
    APP_RUN_MODE_STRAIGHT = 0,
    APP_RUN_MODE_TRACK = 1,
    APP_RUN_MODE_COUNT
} AppRunMode_t;

typedef enum {
    APP_TUNE_MODE_FIXED = 0,
    APP_TUNE_MODE_FUZZY = 1,
    APP_TUNE_MODE_LEARNING = 2,
    APP_TUNE_MODE_COUNT
} AppTuneMode_t;

typedef enum {
    APP_STATE_IDLE = 0,
    APP_STATE_ARMING,
    APP_STATE_RUNNING,
    APP_STATE_STOPPING,
    APP_STATE_FAULT
} AppState_t;

typedef struct {
    float kp;
    float ki;
    float kd;
} AppPidGains_t;

typedef struct {
    DrvEncoderSample_t encoder;
    DrvTrackSensorSample_t track;
    DrvImuBno08xData_t imu;
    uint32_t timeMs;
    uint8_t imuValid;
    float headingDeg;
    float headingRateDegPerSec;
    float leftSpeedRpm;
    float rightSpeedRpm;
    float avgSpeedRpm;
    float linePosition;
    uint8_t lineValid;
} AppControlSnapshot_t;

typedef struct {
    float targetHeadingDeg;
    float targetSpeedRpm;
    float leftTargetRpm;
    float rightTargetRpm;
    float headingErrorDeg;
    float headingRateErrorDegPerSec;
    float headingControl;
    float trackHeadingBiasDeg;
    float basePwm;
    float steeringPwm;
    float rawLeftPwm;
    float rawRightPwm;
    int16_t leftPwm;
    int16_t rightPwm;
    uint8_t motorEnable;
} AppMotorCommand_t;

typedef struct {
    float ise;
    float windowIse;
    float overshoot;
    float settlingTime;
    float absPeakError;
    uint8_t settlingReached;
    uint8_t degraded;
} AppPerformance_t;

typedef struct {
    uint16_t kpMilli;
    uint16_t kiMilli;
    uint16_t kdMilli;
} AppFuzzyRule_t;

typedef struct {
    int16_t centers[APP_FUZZY_SET_COUNT];
    int16_t sigmas[APP_FUZZY_SET_COUNT];
} AppFuzzyAxisConfig_t;

typedef struct {
    AppRunMode_t runMode;
    AppTuneMode_t tuneMode;
    AppState_t state;
    uint16_t bootCount;
    uint16_t faultTraceCode;
    uint8_t resetCauseCode;
    uint32_t faultCfsr;
    uint32_t faultHfsr;
    uint32_t faultAddr;
    AppControlSnapshot_t snapshot;
    AppMotorCommand_t command;
    AppPerformance_t performance;
    AppPidGains_t headingGains;
    AppPidGains_t speedGains;
    float filteredLeftSpeedRpm;
    float filteredRightSpeedRpm;
    float filteredAvgSpeedRpm;
    float forwardSpeedEstimateRpm;
    float configuredTargetSpeedRpm;
    float headingTrimDeg;
    float headingIntegral;
    uint8_t retrainRequested;
    uint8_t profileStored;
    uint8_t profileDirty;
} AppTelemetryFrame_t;

#endif
