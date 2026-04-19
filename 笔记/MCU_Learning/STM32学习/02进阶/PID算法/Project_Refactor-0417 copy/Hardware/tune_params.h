/*
 * 统一调参参数层:
 * 1. 这里集中保存“默认值 + 当前运行值 + 参数元数据”。
 * 2. 目标是把以后会反复试错的量从业务代码里抽出来，统一由串口接口读写。
 * 3. 控制链仍然在 pid_controller / line_track / main 中执行，这里只做参数拥有者。
 */
#ifndef __TUNE_PARAMS_H
#define __TUNE_PARAMS_H

#include "config.h"

typedef struct {
    /* target/kp/ki/kd 统一描述一个速度环或航向环的基础 PID 组。 */
    float target;
    float kp;
    float ki;
    float kd;
} TuneLoopParams_t;

typedef struct {
    /* 直线航向环除了 PID 外，还保留差速权限参数。 */
    float kp;
    float ki;
    float kd;
    float diffRatio;
    float diffMin;
} TuneStraightHeadingParams_t;

typedef struct {
    /* trackLine 只保留直线态基础前端:
       中线误差 -> yawCommand / 权限 / 降速。
       不再包含 capture / recenter / 找线链。 */
    float kp;
    float kd;
    float posLpf;
    float derivLpf;
    float centerBias;
    float targetYawLimit;
    float diffRatio;
    float diffMin;
    /* lineLossHoldMs 允许离散循迹头在短空窗时保留上一拍参考量，
       但不再附带任何找线或重新接管逻辑。 */
    uint32_t lineLossHoldMs;
    uint32_t lineLossTimeoutMs;
    float lineLossYawDecay;
    float lineLossSpeedScale;
    float curveSpeedPosStart;
    float curveSpeedPosFull;
    float curveSpeedScaleMin;
} TuneTrackLineParams_t;

typedef struct {
    /* exp0409 的 S 弯组只保留:
       1. 进入/退出阈值;
       2. 误差成形和传感器参与系数;
       3. S 弯态专属的 kp/yaw/diff/speed 权限。 */
    float enterYawRate;
    float enterError;
    float enterDelta;
    float enterYawCommand;
    float exitError;
    /* 中间弱、边缘强的误差成形参数。 */
    float centerZone;
    float centerGain;
    float edgeGain;
    /* S 弯态下各组传感器的参与系数。 */
    float centerSensorGain;
    float innerSensorGain;
    float outerSensorGain;
    float edgeSensorGain;
    /* S 弯态不再默认把线往中间拉，而是把目标位置压到侧边“目标带”:
       1. linePos 还偏里时，先贴近 S2/S7 一带；
       2. linePos 已经接近最外侧时，再把目标带推向 S1/S8。 */
    float sideTargetPosStart;
    float sideTargetPosFull;
    float sideTargetInner;
    float sideTargetOuter;
    /* ks=1.20 就来自这里，exp0409 日志里已经能直接看到。 */
    float lineKpScale;
    float yawLimit;
    float diffRatio;
    float diffMin;
    /* S 弯态仍然可以约束最低速度倍率。 */
    float speedScaleMin;
    float lossSpeedScaleMin;
    /* 第一段换向结束后，允许更积极地退出 S 弯态。 */
    float exitCenterError;
    float exitCenterDelta;
    float exitCenterYawRate;
    uint32_t exitConfirmCount;
} TuneTrackSCurveParams_t;

typedef struct {
    /* common 组覆盖两个模式共享的动态参数。 */
    float headingTrim;
    float headingIntegralZone;
    float headingIntegralAtten;
    float speedEntry;
    /* startSpeed 只影响一次实验刚启动那一拍的内部斜坡起点。
       设为 0 表示仍然直接以目标速度起跑。 */
    float startSpeed;
    float speedRampRate;
    /* 下降斜坡单独拆出来，方便把“弯中减速响应”做得比起步加速更柔或更狠。 */
    float speedRampDownRate;
    uint32_t speedCoreSlewStep;
    float speedOutputLimit;
    float speedFeedforwardGain;
    /* 约束态里只削弱“负积分”这一支，避免速度环把临时减速整段积成长期掉速。 */
    float speedNegIntegralAtten;
    /* 约束解除后按固定速率释放负积分债，让 pwmCore 能更快回到正常档位。 */
    float speedIntegralReleaseRate;
    float pidDerivLpfAlpha;
    float encoderSpeedLpfAlpha;
    float gyroFastLpfAlpha;
} TuneCommonParams_t;

typedef struct {
    TuneLoopParams_t straightSpeed;
    TuneStraightHeadingParams_t straightHeading;
    TuneLoopParams_t trackSpeed;
    TuneTrackLineParams_t trackLine;
    TuneTrackSCurveParams_t trackSCurve;
    TuneCommonParams_t common;
} TuneRuntime_t;

typedef enum {
    TUNE_PARAM_F32 = 0,
    TUNE_PARAM_U32 = 1
} TuneParamType_t;

typedef struct {
    const char *key;
    const char *group;
    const char *wireType;
    const char *storageType;
    const char *unit;
    TuneParamType_t type;
    uint16_t offset;
    float minValue;
    float maxValue;
    float step;
    float resolution;
    float scale;
    uint8_t persist;
} TuneParamMeta_t;

/* 上电时把当前运行参数恢复成工程默认值。 */
void TuneParams_Init(void);
/* 查询当前运行参数快照。 */
const TuneRuntime_t *TuneParams_Get(void);
/* 按 group 或全部恢复默认值，返回实际恢复的参数数量。 */
uint16_t TuneParams_LoadDefaults(const char *group);
/* 参数表总数，用于 LIST / GET_GROUP 枚举。 */
uint16_t TuneParams_Count(void);
/* 判断某个参数是否属于指定 group。 */
uint8_t TuneParams_GroupMatches(uint16_t index, const char *group);
/* 格式化参数元数据和当前值，适合 #PLIST!。 */
uint8_t TuneParams_FormatListLine(uint16_t index, char *out, uint16_t outSize);
/* 格式化单个参数当前值，适合 #PGET=...!。 */
uint8_t TuneParams_FormatValueLine(const char *key, char *out, uint16_t outSize);
/* 按文本写入一个参数，并返回实际落地值回执。 */
uint8_t TuneParams_SetByText(const char *key, const char *valueText, char *out, uint16_t outSize);
/* 兼容旧命令时，把短命令名映射到统一参数 key。 */
const char *TuneParams_MapLegacyKey(ControlMode_t mode, const char *legacyKey);

#endif
