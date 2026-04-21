#ifndef __LINE_TRACK_H
#define __LINE_TRACK_H

#include "stm32f10x.h"

#define LT_STATE_IDLE      0u
#define LT_STATE_STARTING  1u
#define LT_STATE_RUNNING   2u

#define LT_FLAG_STOP       0u
#define LT_FLAG_START      1u

#define LT_DIR_LEFT        1u
#define LT_DIR_RIGHT       2u

#define LT_DBG_TRACK       0u
#define LT_DBG_CROSS       1u
#define LT_DBG_CORNER      2u
#define LT_DBG_LOSS        3u
#define LT_DBG_STRAIGHT    4u
#define LT_DBG_CURVE       5u

#define LT_MASK_LEFT_FAR   0x03u
#define LT_MASK_LEFT       0x04u
#define LT_MASK_CENTER_L   0x08u
#define LT_MASK_CENTER_R   0x10u
#define LT_MASK_CENTER     0x18u
#define LT_MASK_RIGHT      0x20u
#define LT_MASK_RIGHT_FAR  0xC0u
#define LT_MASK_LEFT_ALL   0x07u
#define LT_MASK_RIGHT_ALL  0xE0u

typedef struct
{
    uint8_t  state;
    uint8_t  autoFlag;

    int8_t   bearingDev;
    int8_t   lastBearingDev;
    uint8_t  sensorBits;
    uint8_t  lastData;
    uint8_t  lastDirectionalBits;
    uint16_t lastDirectionalScore;

    uint8_t  overrunCount;
    uint8_t  filterTimes;
    uint8_t  crossing;
    uint8_t  crossCount;
    uint8_t  crossState;
    uint8_t  crossDetectTicks;

    uint8_t  cornerTurning;
    uint8_t  cornerDone;
    uint8_t  cornerDir;
    uint8_t  lastTrendDir;
    uint8_t  traceLogicState;
    uint8_t  lossSearchActive;
    uint8_t  lossSoftHoldActive;
    uint8_t  lossSearchDir;
    uint8_t  lossHoldReported;
    uint8_t  cornerRecoverTicks;
    uint8_t  centerLockTicks;
    uint8_t  straightAssistArmed;
    uint8_t  straightStableTicks;
    uint8_t  straightAssistTicks;
    uint8_t  straightPeakBitDelta;
    uint8_t  curveProfileActive;
    uint8_t  curveEnterTicks;
    uint8_t  curveExitTicks;
    uint8_t  cornerFlipUsed;
    uint32_t cornerStartTick;
    float    cornerStartYaw;
    float    straightPeakYawRate;

    int16_t  weightedPos;
    int16_t  straightPeakPos;
    int16_t  devSpeed;
    int16_t  lastTrackOutL;
    int16_t  lastTrackOutR;

    float    kp;
    float    kd;

    uint8_t  dbgTrackState;
    uint8_t  dbgCornerDir;
    float    dbgCornerYawDelta;
    uint8_t  dbgCornerBits;
} LineTrack_State_t;

typedef struct
{
    float    lineKp;
    float    lineKd;
    int16_t  basePwmMin;
    int16_t  basePwmMax;
    int16_t  cornerBasePwmMax;
    int16_t  devPwmMax;
    int16_t  edgeBasePwmMax;
    int16_t  edgeDevPwmMax;
    uint8_t  sharpTurnDev;
    int16_t  posCenterDeadband;
    int16_t  posNearThreshold;
    int16_t  posMidThreshold;
    int16_t  posEdgeThreshold;
    int16_t  positionTrim;
    uint8_t  crossMinCount;
    uint8_t  crossConfirmTicks;
    uint8_t  widePatternCount;
    uint16_t crossFilter;
    uint8_t  dtermStepClamp;
    uint8_t  dtermWideClamp;
    uint8_t  centerBearingSlew;
    uint8_t  normalBearingSlew;
    uint8_t  straightArmBearing;
    uint8_t  straightEnterTicks;
    uint8_t  straightHoldTicks;
    int16_t  straightPosThreshold;
    int16_t  straightDevPwmMax;
    uint8_t  straightCenterScalePct;
    float    straightYawRateArmDeg;
    float    straightYawRateEnterDeg;
    int16_t  straightPosDeltaThreshold;
    uint8_t  straightBitDeltaArm;
    uint8_t  straightBitDeltaStable;
    uint8_t  curveEnterTicks;
    uint8_t  curveExitTicks;
    int16_t  curveBasePwmMax;
    int16_t  curveDevPwmMax;
    float    curveYawRateEnterDeg;
    float    curveYawRateExitDeg;
    int16_t  curvePosEnterThreshold;
    int16_t  curvePosExitThreshold;
    uint8_t  curveBitDeltaEnter;
    uint8_t  curveBitDeltaExit;
    int16_t  curveWeightOuter;
    int16_t  curveWeightMid;
    int16_t  curveWeightInner;
    uint8_t  cornerStrongSideHits;
    uint8_t  cornerOppositeMaxHits;
    uint8_t  cornerConfirmTicks;
    uint8_t  cornerFastConfirmTicks;
    uint8_t  lossEnterTicks;
    uint8_t  lossForceCornerTicks;
    uint8_t  lossForceRequireRef;
    uint8_t  lossSearchBearing;
    int16_t  lossHoldBasePwmMax;
    int16_t  lossHoldDevPwmMax;
    int16_t  lossSearchBasePwmMax;
    int16_t  lossSearchDevPwmMax;
    uint8_t  overrunLimitTicks;
    int16_t  turnPwm;
    int16_t  turnPwmMin;
    int16_t  cornerExitPosThreshold;
    uint16_t cornerTimeoutMs;
    float    cornerResumeSpeedMax;
    uint8_t  cornerRecoverTicks;
    uint8_t  centerLockTicks;
    int16_t  centerLockDevPwmMax;
    int16_t  recoverBasePwmMax;
    int16_t  recoverDevPwmMax;
    float    cornerFlipYawDeg;
} LineTrack_RuntimeConfig_t;

extern LineTrack_State_t g_lineTrack;

void LineTrack_Init(void);
void LineTrack_Start(uint8_t crossings);
void LineTrack_Stop(void);
void LineTrack_Update(uint32_t tickMs, int16_t basePwm, float currentYaw, float currentYawRate);
uint8_t LineTrack_IsRunning(void);
void LineTrack_SetPID(float kp, float kd);
const LineTrack_RuntimeConfig_t *LineTrack_GetRuntimeConfig(void);
void LineTrack_ResetRuntimeConfig(void);
uint8_t LineTrack_SetRuntimeParam(const char *name, float value);
void LineTrack_DumpRuntimeConfig(void);

#endif
