#ifndef __LINE_TRACK_H
#define __LINE_TRACK_H

#include "stm32f10x.h"
#include "config.h"

#define LT_STATE_IDLE      0u
#define LT_STATE_STARTING  1u
#define LT_STATE_RUNNING   2u

#define LT_FLAG_STOP       0u
#define LT_FLAG_START      1u

#define LT_DIR_NONE        0u
#define LT_DIR_LEFT        1u
#define LT_DIR_RIGHT       2u

/* Compatibility placeholders for legacy telemetry fields. */
#define LT_ACUTE_IDLE      0u
#define LT_ACUTE_TURNING   2u
#define LT_ACUTE_RECOVER   3u

typedef enum
{
    LT_TUNE_KP_STRAIGHT = 0,
    LT_TUNE_KP_CURVE,
    LT_TUNE_KD_STRAIGHT,
    LT_TUNE_KD_CURVE,
    LT_TUNE_DEADBAND_STRAIGHT,
    LT_TUNE_DEADBAND_CURVE,
    LT_TUNE_LOAD_LOW,
    LT_TUNE_LOAD_HIGH,
    LT_TUNE_CENTER_ANCHOR_STRAIGHT,
    LT_TUNE_CENTER_ANCHOR_CURVE,
    LT_TUNE_STEER_TRIM,
    LT_TUNE_CURVE_BRAKE_GAIN,
    LT_TUNE_CURVE_SPEED_MIN_RATIO
} LineTrack_TuneParam_t;

typedef struct
{
    uint8_t state;
    uint8_t autoFlag;

    int8_t bearingDev;
    int8_t lastBearingDev;

    uint8_t sensorBits;
    uint8_t lastData;

    uint8_t filterTimes;
    uint8_t crossing;
    uint8_t crossCount;
    uint8_t crossState;

    uint16_t overrunCount;
    uint8_t searchActive;
    uint8_t pidBypassActive;
    uint8_t searchDir;
    uint8_t searchAcceptCount;
    uint16_t searchTickCount;
    uint32_t searchStartTick;

    uint8_t cornerDone;

    int16_t devSpeed;
    int16_t weightedPos;
    int16_t trackPwmLeft;
    int16_t trackPwmRight;
    int16_t trackBasePwm;

    float rawLinePos;
    float filteredLinePos;
    float filteredLineRate;
    float previewLinePos;
    float pidLastLinePos;
    float filteredCurveLoad;
    float scheduleAlpha;
    float curveSpeedScale;
    float curveSpeedTarget;
    float cruiseSpeedTarget;
    float activeSteerDeadband;
    float activeCenterAnchor;
    float activeSteerTrim;
    uint8_t centerHold;
    uint8_t centerStableCount;

    float kp;
    float kd;

    /* Compatibility fields retained for current telemetry/main references. */
    uint8_t startupSkipSecondTurnEnabled;
    uint8_t acuteState;
    float acuteStartYaw;
    uint32_t acuteRearmTick;
    uint8_t dbgTrackState;
    uint8_t dbgCornerDir;
    float dbgCornerYawDelta;
    uint8_t dbgCornerBits;
    uint8_t dbgCornerAcceptMask;
    uint8_t dbgCornerYawReady;
    uint8_t dbgCornerAcceptHit;
} LineTrack_State_t;

extern LineTrack_State_t g_lineTrack;

void LineTrack_Init(void);
void LineTrack_Start(uint8_t crossings);
void LineTrack_Stop(void);
void LineTrack_Update(uint32_t tickMs, int16_t basePwm);
uint8_t LineTrack_IsRunning(void);
void LineTrack_SetCruiseSpeed(float speed);
float LineTrack_GetCruiseSpeedTarget(void);
float LineTrack_GetCurveSpeedTarget(void);
void LineTrack_LoadTuneDefaults(void);
void LineTrack_SetDynamicPidEnable(uint8_t enable);
uint8_t LineTrack_GetDynamicPidEnable(void);
uint8_t LineTrack_SetTuneParam(LineTrack_TuneParam_t param, float value);
float LineTrack_GetTuneParam(LineTrack_TuneParam_t param);
float LineTrack_GetScheduleAlpha(void);
float LineTrack_GetActiveSteerDeadband(void);
void LineTrack_SetPID(float kp, float kd);
void LineTrack_SetStartupSkipSecondTurnEnabled(uint8_t enable);

#endif
