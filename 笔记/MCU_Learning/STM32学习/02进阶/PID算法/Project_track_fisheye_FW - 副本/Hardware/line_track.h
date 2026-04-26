#ifndef __LINE_TRACK_H
#define __LINE_TRACK_H

#include "stm32f10x.h"
#include "config.h"

#define LT_STATE_IDLE      0u
#define LT_STATE_STARTING  1u
#define LT_STATE_RUNNING   2u

#define LT_DIR_NONE        0u
#define LT_DIR_LEFT        1u
#define LT_DIR_RIGHT       2u

#define LT_TRACK_FOLLOW        0u
#define LT_TRACK_SEARCH_LEFT   1u
#define LT_TRACK_SEARCH_RIGHT  2u
#define LT_TRACK_CROSS         3u

#define LT_SEARCH_PHASE_ARC    0u
#define LT_SEARCH_PHASE_PIVOT  1u

#define LT_CROSS_READY         1u
#define LT_CROSS_SEEN          2u

#define LT_TLM_STATE_TRACK        0u
#define LT_TLM_STATE_STRAIGHT     1u
#define LT_TLM_STATE_SCURVE       2u
#define LT_TLM_STATE_EDGE         3u
#define LT_TLM_STATE_SEARCH_LEFT  4u
#define LT_TLM_STATE_SEARCH_RIGHT 5u
#define LT_TLM_STATE_TRIM_LEFT    6u
#define LT_TLM_STATE_TRIM_RIGHT   7u
#define LT_TLM_STATE_CROSS        8u

#define LT_TLM_FLAG_CENTER        0x0001u
#define LT_TLM_FLAG_STRAIGHT      0x0002u
#define LT_TLM_FLAG_SCURVE        0x0004u
#define LT_TLM_FLAG_SEARCH        0x0008u
#define LT_TLM_FLAG_TRIM          0x0010u
#define LT_TLM_FLAG_CROSS         0x0020u
#define LT_TLM_FLAG_EDGE          0x0040u
#define LT_TLM_FLAG_LOST          0x0080u
#define LT_TLM_FLAG_PIVOT         0x0200u

typedef struct
{
    float    sensorScale[LINE_SENSOR_COUNT];
    float    devRatio;
    float    deadband;
    float    posFilterAlpha;
    float    dFilterAlpha;
    int16_t  staticSteerBias;
    uint16_t diffSlewStep;
    uint8_t  recoverTicks;
    uint16_t searchArcPwmFast;
    uint16_t searchArcPwmSlow;
    uint16_t searchTurnPwmFast;
    uint16_t searchTurnPwmSlow;
    uint16_t searchHalfMaskTicks;
    uint16_t searchTimeoutTicks;
} LineTrack_RuntimeConfig_t;

typedef struct
{
    uint8_t  state;
    uint8_t  trackState;

    uint16_t sensorData;
    uint16_t lastData;

    int8_t   bearingDev;
    int16_t  linePos;
    int16_t  lastValidLinePos;
    int16_t  devSpeed;
    int16_t  lastDevSpeedCmd;

    float    filteredTrackError;
    float    lastFilteredTrackError;
    float    filteredDTerm;
    float    smoothedLinePos;
    uint32_t runStartTickMs;

    uint8_t  crossReleaseTicks;
    uint8_t  crossCount;
    uint8_t  crossState;
    uint8_t  overrunCount;

    uint8_t  lastTurnDir;
    uint8_t  cornerLatchDir;
    uint8_t  searchDir;
    uint8_t  searchPhase;
    uint8_t  searchSeenTicks;
    uint8_t  cornerDone;
    uint8_t  cornerLatchTicks;
    uint8_t  recoverHoldTicks;
    uint16_t searchTicks;

    float    kp;
    float    kd;
    float    activeKp;
    float    activeKd;

    uint8_t  gainStage;
    uint8_t  recoverDir;
    uint8_t  recoverTicks;

    uint8_t  dbgTrackState;
    uint8_t  dbgTurnDir;
    uint8_t  dbgCrossActive;
    uint8_t  dbgTelemState;
    uint8_t  dbgScoreEnabled;
    uint8_t  dbgActiveCount;
    uint8_t  dbgSearchLost;
    uint8_t  dbgSearchReacquired;
    uint8_t  dbgCornerCandidateDir;
    uint8_t  dbgResolvedSearchDir;
    uint8_t  dbgResolvedSource;
    uint16_t dbgTelemFlags;
} LineTrack_State_t;

extern LineTrack_State_t g_lineTrack;
extern LineTrack_RuntimeConfig_t g_lineTrackCfg;

void LineTrack_Init(void);
void LineTrack_Start(uint8_t crossings);
void LineTrack_Stop(void);
void LineTrack_Update(uint32_t tickMs, int16_t basePwm, float currentYawRate);
uint8_t LineTrack_IsRunning(void);
uint8_t LineTrack_PollFullBlackStop(uint32_t tickMs);
void LineTrack_SetPID(float kp, float kd);
void LineTrack_ResetRuntimeConfig(void);
uint8_t LineTrack_ParamSet(const char *key, float value, float *appliedValue);
uint8_t LineTrack_ParamGet(const char *key, float *value);
void LineTrack_ParamList(char *out, uint16_t outSize);

#endif
