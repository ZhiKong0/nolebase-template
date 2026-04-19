/*
 * 纯两态循迹前端:
 * 1. 只保留“直线居中循迹”和“S 弯贴边循迹”两条控制链。
 * 2. 删除找线 / 回中 / capture 渐进接管 / 反向换边软接管等附加状态机。
 * 3. S 弯态下 S4/S5 不参与约束，S1/S8 权重最大，S2/S7 次之，S3/S6 最弱。
 */
#ifndef __LINE_TRACK_H
#define __LINE_TRACK_H

#include "stm32f10x.h"

#define LT_STATE_IDLE    0u
#define LT_STATE_RUNNING 1u

typedef struct {
    uint8_t state;
    uint8_t sensorBits;
    uint8_t sensorCount;
    uint8_t lineDetected;
    /* edgeLockActive 只表示“当前是否已经压到边缘带附近”。
       这个标志只服务串口观测和预触发，不再驱动额外控制分支。 */
    uint8_t edgeLockActive;
    uint8_t sCurveActive;
    uint8_t sCurveEnterConfirmCount;
    uint8_t sCurveExitConfirmCount;
    int8_t sCurveSideSign;

    /* 原始位置由当前状态对应的观测面直接给出:
       直线态=基础 8 路位置;
       S 弯态=中心弱、边缘强后的贴边观测位置。 */
    float rawPosition;
    float filteredPosition;
    float filteredDelta;
    /* targetLinePosition 是当前真正追的线位置:
       直线态追中线，S 弯态追侧边目标带。 */
    float targetLinePosition;
    float positionError;
    float effectiveError;
    float yawCommand;
    float targetYaw;
    float speedScale;
    float headingDiffRatio;
    float headingDiffMin;
    float activeLineKpScale;
    float activeYawLimit;
    float kp;
    float kd;
    uint32_t lastSeenTick;
} LineTrack_State_t;

typedef struct {
    uint8_t sensorBits;
    uint8_t sensorCount;
    uint8_t lineDetected;
    uint8_t edgeLockActive;
    uint8_t sCurveActive;
    float linePosition;
    float targetLinePosition;
    float positionError;
    float effectiveError;
    float yawCommand;
    float targetYaw;
    float speedScale;
    float headingDiffRatio;
    float headingDiffMin;
    float lineKpScale;
    float yawLimit;
} LineTrack_Snapshot_t;

extern LineTrack_State_t g_lineTrack;

void LineTrack_Init(void);
void LineTrack_Start(uint32_t tickMs, float currentYaw);
void LineTrack_Stop(void);
void LineTrack_Update(uint32_t tickMs, float currentYaw, float yawRate);
void LineTrack_CollectIdleSnapshot(float currentYaw, float yawRate, LineTrack_Snapshot_t *out);
uint8_t LineTrack_IsRunning(void);
uint8_t LineTrack_IsSCurveActive(void);
uint8_t LineTrack_IsSpeedConstraintActive(void);
void LineTrack_SetPID(float kp, float kd);
void LineTrack_RefreshTune(void);
float LineTrack_GetTargetYaw(void);
float LineTrack_GetYawCommand(void);
float LineTrack_GetTargetLinePosition(void);
float LineTrack_GetSpeedScale(void);
float LineTrack_GetHeadingDiffRatio(void);
float LineTrack_GetHeadingDiffMin(void);
float LineTrack_GetLineKpScale(void);
float LineTrack_GetYawLimit(void);

#endif
