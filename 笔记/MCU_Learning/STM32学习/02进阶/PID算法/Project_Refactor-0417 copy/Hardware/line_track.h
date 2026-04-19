/*
 * 基础循迹骨架说明:
 * 1. 本模块负责“读线 -> 转向命令/速度倍率/权限”的前端感知与参考量生成。
 * 2. 直线模式仍走速度环 + 航向环；循迹模式当前走速度环 + 前端直接差速输出。
 * 3. 这样拆开的原因是把“线怎么看”和“电机怎么打”分成两层，后续调线和调车互不干扰。
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
    /* captureActive=1 表示当前处于“边缘捕获态”，允许使用更激进的拉入策略。 */
    uint8_t captureActive;
    /* capture 也改成连续确认，不再把第一拍边缘位型直接当成“满额强钳位”。 */
    uint8_t captureConfirmCount;
    /* captureSwitchActive=1 表示当前 capture 是“刚换边后的第一段”，
       应按更软的接管参数慢一点拉起来。 */
    uint8_t captureSwitchActive;
    /* exp0409 的 S 弯态只有一个布尔态:
       一旦切入，就统一使用更高的 kp/yaw_limit/diff 权限。 */
    uint8_t sCurveActive;
    /* 入弯证据连续确认计数:
       避免仅凭单拍角速度或位置波动就提前切进 scurve。 */
    uint8_t sCurveEnterConfirmCount;
    /* 用少量连续确认拍数避免 S 弯态在中心附近一闪一灭。 */
    uint8_t sCurveExitConfirmCount;
    /* recenterActive=1 表示“中间灯重新出现后”的回中重锁链正在生效。 */
    uint8_t recenterActive;

    /* 原始位置来自 8 路传感器权重平均；filteredPosition 是控制使用的平滑量。 */
    float rawPosition;
    float filteredPosition;
    /* filteredDelta 近似表示“线在视野里移动的速度”。 */
    float filteredDelta;
    /* positionError 已扣除了中心偏置，0 附近表示车身大致压在线中心。 */
    float positionError;
    /* effectiveError 是经过捕获态增强后的控制误差，直接决定转向和降速强度。 */
    float effectiveError;
    /* yawCommand 是前端直接生成的循迹转向命令。
       当前版本里它既保留“等效航向偏置”的物理意义，也直接参与循迹模式差速输出。 */
    float yawCommand;
    /* targetYaw 现在主要保留给串口分析，便于观察“如果仍按目标航向解释，这一拍会指向哪里”。 */
    float targetYaw;
    /* speedScale 是给速度环的倍率，弯大/丢线时会主动降速。 */
    float speedScale;
    /* 循迹前端还会告诉航向环“这一拍允许打多大差速”。 */
    float headingDiffRatio;
    float headingDiffMin;
    /* 为了做遥测分析，顺手把当前真实使用的前端 P 缩放和航向上限也留出来。 */
    float activeLineKpScale;
    float activeYawLimit;
    /* captureAuthorityScale 表示 capture 这条链当前实际生效了几成。
       第一拍先从 entry_scale 起步，连续确认后再拉满。 */
    float captureAuthorityScale;
    /* captureRateReliefScale 表示“顺向角速度卸力”当前压了几成。
       1.00 表示不卸力，越接近 0 表示车头已经顺着边缘方向在转，
       capture 应主动收一收。 */
    float captureRateReliefScale;
    /* scurveAuthorityScale 表示当前 S 弯态实际还剩多少权限。
       capture 刚抓到边缘时接近 1；capture 退出后的尾段会逐步降下来，
       避免整段都按满权限硬顶。 */
    float sCurveAuthorityScale;
    /* recenterScale 表示本拍回中重锁实际生效几成，便于遥测判断
       “是不是已经重新压回中间了，但前端还没来得及撤掉边缘偏置”。 */
    float recenterScale;

    /* 这里的 kp/kd 是“位置 -> 目标航向”的前端增益，不是电机闭环 PID。 */
    float kp;
    float kd;

    /* 最近一次确认看到线的时刻，用于区分“短暂空窗”和“真正丢线”。 */
    uint32_t lastSeenTick;
} LineTrack_State_t;

typedef struct {
    /* 待机快照专门服务“起跑前观测”，不携带运行中的历史状态。 */
    uint8_t sensorBits;
    uint8_t sensorCount;
    uint8_t lineDetected;
    uint8_t captureActive;
    uint8_t captureSwitchActive;
    uint8_t sCurveActive;
    float linePosition;
    float positionError;
    float effectiveError;
    float yawCommand;
    float targetYaw;
    float speedScale;
    float captureAuthorityScale;
    float captureRateReliefScale;
    float recenterScale;
    float headingDiffRatio;
    float headingDiffMin;
    float lineKpScale;
    float yawLimit;
} LineTrack_Snapshot_t;

extern LineTrack_State_t g_lineTrack;

/* 初始化循迹模块的内部状态和默认读线参数。 */
void LineTrack_Init(void);
/* 进入循迹模式时调用，锁定起始航向并清空历史状态。 */
void LineTrack_Start(uint32_t tickMs, float currentYaw);
/* 主动退出循迹模式时调用，清空内部状态。 */
void LineTrack_Stop(void);
/* 每个控制周期调用一次，刷新当前目标航向和速度倍率。 */
void LineTrack_Update(uint32_t tickMs, float currentYaw, float yawRate);
/* 在未运行状态下采一帧“空载循迹快照”，给待机心跳和脚本预触发使用。 */
void LineTrack_CollectIdleSnapshot(float currentYaw, float yawRate, LineTrack_Snapshot_t *out);
/* 查询循迹状态机是否仍处于运行态。 */
uint8_t LineTrack_IsRunning(void);
/* 查询当前是否处于 S 弯拉回态。 */
uint8_t LineTrack_IsSCurveActive(void);
/* 查询当前前端是否正处于“主动限速”的约束态。 */
uint8_t LineTrack_IsSpeedConstraintActive(void);
/* 在线调整“位置 -> 目标航向”的前端增益。 */
void LineTrack_SetPID(float kp, float kd);
/* 把统一参数层里的当前值刷新到循迹前端缓存。 */
void LineTrack_RefreshTune(void);
/* 读取当前生成的绝对目标航向。 */
float LineTrack_GetTargetYaw(void);
/* 读取当前前端直接生成的循迹转向命令。 */
float LineTrack_GetYawCommand(void);
/* 读取当前建议的速度倍率，供主循环临时压低目标速度。 */
float LineTrack_GetSpeedScale(void);
/* 读取当前循迹模式允许的差速比例上限。 */
float LineTrack_GetHeadingDiffRatio(void);
/* 读取当前循迹模式允许的最小差速下限。 */
float LineTrack_GetHeadingDiffMin(void);
/* 读取当前实际使用的前端 P 缩放，便于串口分析“何时切进 S 弯态”。 */
float LineTrack_GetLineKpScale(void);
/* 读取当前实际使用的目标航向上限。 */
float LineTrack_GetYawLimit(void);
/* 读取当前 capture 实际权限，便于分析“第一拍到底放了多大钳位”。 */
float LineTrack_GetCaptureAuthorityScale(void);
/* 读取当前顺向角速度卸力缩放，便于分析“是不是车头已经在转，但 capture 还没收手”。 */
float LineTrack_GetCaptureRateReliefScale(void);
/* 读取当前 S 弯态权限缩放，便于串口分析“还在 S 弯态，但是不是已经开始放权”。 */
float LineTrack_GetSCurveAuthorityScale(void);
/* 读取当前回中重锁缩放，便于分析“回中后摆头”问题。 */
float LineTrack_GetRecenterScale(void);

#endif
