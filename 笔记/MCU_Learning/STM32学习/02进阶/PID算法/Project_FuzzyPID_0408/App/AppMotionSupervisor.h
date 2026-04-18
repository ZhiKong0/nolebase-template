#ifndef __APP_MOTION_SUPERVISOR_H
#define __APP_MOTION_SUPERVISOR_H

#include "AppRuntime.h"

void AppMotionSupervisor_Init(AppRuntimeContext_t *runtime);
void AppMotionSupervisor_Start(AppRuntimeContext_t *runtime);
void AppMotionSupervisor_Stop(AppRuntimeContext_t *runtime);
void AppMotionSupervisor_EnterFault(AppRuntimeContext_t *runtime, AppFaultCode_t faultCode);
void AppMotionSupervisor_ClearFault(AppRuntimeContext_t *runtime);
void AppMotionSupervisor_UpdateFuzzy(AppRuntimeContext_t *runtime);
void AppMotionSupervisor_ControlStep(AppRuntimeContext_t *runtime);
uint8_t AppMotionSupervisor_SetRunMode(AppRuntimeContext_t *runtime, AppRunMode_t runMode);
void AppMotionSupervisor_CycleRunMode(AppRuntimeContext_t *runtime);
void AppMotionSupervisor_SetTuneMode(AppRuntimeContext_t *runtime, AppTuneMode_t tuneMode);
void AppMotionSupervisor_SetTargetSpeed(AppRuntimeContext_t *runtime, float targetSpeedRpm);
void AppMotionSupervisor_SetHeadingTrim(AppRuntimeContext_t *runtime, float trimDeg);
void AppMotionSupervisor_SetTrackHeadingGain(AppRuntimeContext_t *runtime, float gainDegPerUnit);
void AppMotionSupervisor_SetHeadingPid(AppRuntimeContext_t *runtime, const AppPidGains_t *gains);
void AppMotionSupervisor_SetSpeedPid(AppRuntimeContext_t *runtime, const AppPidGains_t *gains);
void AppMotionSupervisor_SetRuleTable(AppRuntimeContext_t *runtime, const AppFuzzyRule_t *rules, uint16_t count);
void AppMotionSupervisor_SetMembershipAxes(AppRuntimeContext_t *runtime,
                                           const AppFuzzyAxisConfig_t *errorAxis,
                                           const AppFuzzyAxisConfig_t *rateAxis);

#endif
