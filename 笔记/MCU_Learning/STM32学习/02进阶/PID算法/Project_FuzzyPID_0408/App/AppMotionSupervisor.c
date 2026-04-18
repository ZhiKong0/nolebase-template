#include "AppMotionSupervisor.h"
#include "DrvMotor.h"
#include "DrvEncoder.h"
#include "DrvTrackSensor.h"
#include "DrvImuBno08x.h"
#include <string.h>

static float app_motion_abs(float value)
{
    return (value >= 0.0f) ? value : -value;
}

static float app_motion_clamp(float value, float minValue, float maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static float app_motion_lpf(float prevValue, float sample, float alpha)
{
    if (alpha <= 0.0f) {
        return sample;
    }
    if (alpha >= 1.0f) {
        return sample;
    }
    return prevValue + alpha * (sample - prevValue);
}

static float app_motion_limit_pwm_step(float prevPwm, float targetPwm, float riseStep, float fallStep)
{
    if (targetPwm > prevPwm + riseStep) {
        return prevPwm + riseStep;
    }
    if (targetPwm < prevPwm - fallStep) {
        return prevPwm - fallStep;
    }
    return targetPwm;
}

static float app_motion_step_forward_speed_pid(AppPidController_t *pid,
                                               float targetRpm,
                                               float measuredRpm,
                                               float dtSeconds)
{
    float error;

    if (pid == 0) {
        return 0.0f;
    }

    if (targetRpm <= 0.5f) {
        AppPid_Reset(pid);
        return 0.0f;
    }

    error = targetRpm - measuredRpm;
    if (error <= 0.0f) {
        pid->integral *= 0.97f;
        if (app_motion_abs(pid->integral) < 0.02f) {
            pid->integral = 0.0f;
        }
        return AppPid_StepWithDerivative(pid, 0.0f, 0.0f, dtSeconds);
    }

    return AppPid_Step(pid, error, dtSeconds);
}

static int16_t app_motion_clamp_pwm(float value, int16_t maxPwm)
{
    if (value > (float)maxPwm) {
        value = (float)maxPwm;
    }
    if (value < (float)(-maxPwm)) {
        value = (float)(-maxPwm);
    }

    if (value >= 0.0f) {
        return (int16_t)(value + 0.5f);
    }
    return (int16_t)(value - 0.5f);
}

static void app_motion_apply_motor(const AppMotorCommand_t *command)
{
    if ((command == 0) || (command->motorEnable == 0u)) {
        DrvMotor_SetEnabled(0u);
        DrvMotor_Stop();
        return;
    }

    DrvMotor_SetEnabled(1u);
    DrvMotor_Apply(command->leftPwm, command->rightPwm);
}

static void app_motion_reset_outputs(AppRuntimeContext_t *runtime)
{
    runtime->motorCommand.targetHeadingDeg = runtime->snapshot.headingDeg;
    runtime->motorCommand.targetSpeedRpm = 0.0f;
    runtime->motorCommand.leftTargetRpm = 0.0f;
    runtime->motorCommand.rightTargetRpm = 0.0f;
    runtime->motorCommand.headingErrorDeg = 0.0f;
    runtime->motorCommand.headingRateErrorDegPerSec = 0.0f;
    runtime->motorCommand.headingControl = 0.0f;
    runtime->motorCommand.trackHeadingBiasDeg = 0.0f;
    runtime->motorCommand.basePwm = 0.0f;
    runtime->motorCommand.steeringPwm = 0.0f;
    runtime->motorCommand.rawLeftPwm = 0.0f;
    runtime->motorCommand.rawRightPwm = 0.0f;
    runtime->motorCommand.leftPwm = 0;
    runtime->motorCommand.rightPwm = 0;
    runtime->motorCommand.motorEnable = 0u;
}

static void app_motion_reset_straight_estimator(AppRuntimeContext_t *runtime)
{
    if (runtime == 0) {
        return;
    }

    runtime->forwardSpeedEstimateRpm = 0.0f;
    runtime->straightLeftCountAccum = 0;
    runtime->straightRightCountAccum = 0;
    runtime->straightSpeedWindowTicks = 0u;
}

static uint8_t app_motion_refresh_snapshot(AppRuntimeContext_t *runtime, uint16_t imuTries)
{
    uint16_t i;
    uint8_t imuOk = 0u;
    uint16_t windowMs;
    float leftWindowRpm;
    float rightWindowRpm;

    runtime->snapshot.timeMs = runtime->tickMs;
    DrvEncoder_Sample(&runtime->snapshot.encoder, APP_CONTROL_PERIOD_MS);
    DrvTrackSensor_Sample(&runtime->snapshot.track);

    runtime->snapshot.leftSpeedRpm = runtime->snapshot.encoder.leftRpm;
    runtime->snapshot.rightSpeedRpm = runtime->snapshot.encoder.rightRpm;
    runtime->snapshot.avgSpeedRpm = (runtime->snapshot.leftSpeedRpm + runtime->snapshot.rightSpeedRpm) * 0.5f;
    runtime->filteredLeftSpeedRpm = app_motion_lpf(runtime->filteredLeftSpeedRpm,
                                                   runtime->snapshot.leftSpeedRpm,
                                                   runtime->speedFilterAlpha);
    runtime->filteredRightSpeedRpm = app_motion_lpf(runtime->filteredRightSpeedRpm,
                                                    runtime->snapshot.rightSpeedRpm,
                                                    runtime->speedFilterAlpha);
    runtime->filteredAvgSpeedRpm = (runtime->filteredLeftSpeedRpm + runtime->filteredRightSpeedRpm) * 0.5f;

    if ((runtime->state == APP_STATE_RUNNING) && (runtime->runMode == APP_RUN_MODE_STRAIGHT)) {
        runtime->straightLeftCountAccum += runtime->snapshot.encoder.leftSpeed;
        runtime->straightRightCountAccum += runtime->snapshot.encoder.rightSpeed;
        if (runtime->straightSpeedWindowTicks < 0xFFu) {
            runtime->straightSpeedWindowTicks++;
        }
        if (runtime->straightSpeedWindowTicks >= 5u) {
            windowMs = (uint16_t)(runtime->straightSpeedWindowTicks * APP_CONTROL_PERIOD_MS);
            leftWindowRpm = DrvEncoder_CountToRpm(runtime->straightLeftCountAccum, windowMs);
            rightWindowRpm = DrvEncoder_CountToRpm(runtime->straightRightCountAccum, windowMs);
            runtime->forwardSpeedEstimateRpm = (leftWindowRpm + rightWindowRpm) * 0.5f;
            runtime->straightLeftCountAccum = 0;
            runtime->straightRightCountAccum = 0;
            runtime->straightSpeedWindowTicks = 0u;
        }
    } else {
        runtime->forwardSpeedEstimateRpm = runtime->snapshot.avgSpeedRpm;
        runtime->straightLeftCountAccum = 0;
        runtime->straightRightCountAccum = 0;
        runtime->straightSpeedWindowTicks = 0u;
    }

    runtime->snapshot.linePosition = runtime->snapshot.track.rawPosition;
    runtime->snapshot.lineValid = runtime->snapshot.track.hasLine;

    for (i = 0u; i < imuTries; i++) {
        if (DrvImuBno08x_Read(&runtime->snapshot.imu) != 0u) {
            DrvImuBno08x_UpdateYaw(&runtime->snapshot.imu, (float)APP_CONTROL_PERIOD_MS / 1000.0f);
            if (runtime->snapshot.imu.ahrsInited != 0u) {
                imuOk = 1u;
                break;
            }
        }
    }

    if (imuOk != 0u) {
        runtime->snapshot.imuValid = 1u;
        runtime->imuMissTicks = 0u;
    } else if (runtime->state == APP_STATE_RUNNING) {
        if (runtime->imuMissTicks < 0xFFFFu) {
            runtime->imuMissTicks++;
        }
    }

    runtime->snapshot.headingDeg = runtime->snapshot.imu.yaw;
    runtime->snapshot.headingRateDegPerSec = runtime->snapshot.imu.yawRate;
    return runtime->snapshot.imuValid;
}

static void app_motion_update_heading_target(AppRuntimeContext_t *runtime)
{
    float trackBias = 0.0f;

    if (runtime->runMode == APP_RUN_MODE_TRACK) {
        if (runtime->snapshot.lineValid != 0u) {
            trackBias = runtime->snapshot.linePosition * runtime->trackHeadingGainDegPerUnit;
        } else if (runtime->snapshot.track.lastDirection > 0) {
            trackBias = 12.0f;
        } else if (runtime->snapshot.track.lastDirection < 0) {
            trackBias = -12.0f;
        }

        trackBias = app_motion_clamp(trackBias, -30.0f, 30.0f);
        runtime->motorCommand.targetHeadingDeg = runtime->snapshot.headingDeg + trackBias + runtime->headingTrimDeg;
        runtime->motorCommand.trackHeadingBiasDeg = trackBias;
    } else {
        runtime->motorCommand.targetHeadingDeg = runtime->headingLockDeg + runtime->headingTrimDeg;
        runtime->motorCommand.trackHeadingBiasDeg = 0.0f;
    }
}

static void app_motion_configure_encoder_limit(AppRuntimeContext_t *runtime)
{
    DrvEncoderConfig_t cfg;
    float countsPerRev;
    float expectedCountsPerPeriod;
    float limitCounts;

    if (runtime == 0) {
        return;
    }

    DrvEncoder_GetConfig(&cfg);
    countsPerRev = (float)cfg.ppr * (float)cfg.ratio * (float)cfg.quadMultiplier;
    if (countsPerRev <= 0.0f) {
        return;
    }

    expectedCountsPerPeriod = (runtime->maxWheelSpeedRpm * countsPerRev * (float)APP_CONTROL_PERIOD_MS) / 60000.0f;
    /* Reject bursts that exceed the physically plausible wheel-count range by a large margin. */
    limitCounts = app_motion_clamp(expectedCountsPerPeriod * 3.0f + 6.0f, 20.0f, 120.0f);
    cfg.maxDeltaPerPeriod = (uint16_t)(limitCounts + 0.5f);
    DrvEncoder_SetConfig(&cfg);
}

void AppMotionSupervisor_Init(AppRuntimeContext_t *runtime)
{
    AppPidGains_t fixedHeadingGains;

    if (runtime == 0) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));

    fixedHeadingGains.kp = 1.20f;
    fixedHeadingGains.ki = 0.08f;
    fixedHeadingGains.kd = 0.22f;

    runtime->speedGains.kp = 1.80f;
    runtime->speedGains.ki = 0.35f;
    runtime->speedGains.kd = 0.02f;
    runtime->runMode = APP_RUN_MODE_STRAIGHT;
    runtime->state = APP_STATE_IDLE;
    runtime->baseTargetSpeedRpm = 35.0f;
    runtime->headingTrimDeg = 0.0f;
    runtime->trackHeadingGainDegPerUnit = 6.0f;
    runtime->maxWheelSpeedRpm = 70.0f;
    runtime->maxSteeringRpm = 25.0f;
    runtime->maxPwm = 100;
    runtime->speedFilterAlpha = 0.35f;
    runtime->faultCode = APP_FAULT_NONE;
    runtime->profileStored = 0u;
    runtime->profileDirty = 0u;

    DrvMotor_Init();
    DrvEncoder_Init();
    DrvTrackSensor_Init();
    app_motion_configure_encoder_limit(runtime);

    runtime->snapshot.imuValid = DrvImuBno08x_Init();
    if (runtime->snapshot.imuValid != 0u) {
        DrvImuBno08x_Calibrate(&runtime->snapshot.imu, 80u);
        DrvImuBno08x_ResetAttitude(&runtime->snapshot.imu);
    }

    AppPid_Init(&runtime->headingPid, &fixedHeadingGains,
                -runtime->maxSteeringRpm, runtime->maxSteeringRpm,
                -15.0f, 15.0f);
    AppPid_Init(&runtime->forwardSpeedPid, &runtime->speedGains,
                0.0f, (float)runtime->maxPwm,
                0.0f, 60.0f);
    AppPid_Init(&runtime->leftSpeedPid, &runtime->speedGains,
                -(float)runtime->maxPwm, (float)runtime->maxPwm,
                -60.0f, 60.0f);
    AppPid_Init(&runtime->rightSpeedPid, &runtime->speedGains,
                -(float)runtime->maxPwm, (float)runtime->maxPwm,
                -60.0f, 60.0f);
    AppFuzzyTuner_Init(&runtime->fuzzyTuner, &fixedHeadingGains);
    AppLearningManager_Init(&runtime->learning, &fixedHeadingGains);
    AppLearningManager_SetTuneMode(&runtime->learning, APP_TUNE_MODE_FUZZY);
    AppPerformanceMonitor_Init(&runtime->performance);

    runtime->appliedHeadingGains = fixedHeadingGains;
    app_motion_reset_outputs(runtime);
    app_motion_refresh_snapshot(runtime, 120u);
    runtime->filteredLeftSpeedRpm = runtime->snapshot.leftSpeedRpm;
    runtime->filteredRightSpeedRpm = runtime->snapshot.rightSpeedRpm;
    runtime->filteredAvgSpeedRpm = runtime->snapshot.avgSpeedRpm;
    runtime->forwardSpeedEstimateRpm = runtime->snapshot.avgSpeedRpm;
    runtime->prevAppliedLeftPwm = 0.0f;
    runtime->prevAppliedRightPwm = 0.0f;
    runtime->headingLockDeg = runtime->snapshot.headingDeg;
    AppMotionSupervisor_UpdateFuzzy(runtime);
}

void AppMotionSupervisor_EnterFault(AppRuntimeContext_t *runtime, AppFaultCode_t faultCode)
{
    if (runtime == 0) {
        return;
    }

    runtime->faultCode = (uint16_t)faultCode;
    runtime->state = APP_STATE_FAULT;
    app_motion_reset_outputs(runtime);
    app_motion_reset_straight_estimator(runtime);
    runtime->prevAppliedLeftPwm = 0.0f;
    runtime->prevAppliedRightPwm = 0.0f;
    app_motion_apply_motor(&runtime->motorCommand);
}

void AppMotionSupervisor_ClearFault(AppRuntimeContext_t *runtime)
{
    if (runtime == 0) {
        return;
    }

    runtime->faultCode = APP_FAULT_NONE;
    runtime->state = APP_STATE_IDLE;
    runtime->imuMissTicks = 0u;
    AppPerformanceMonitor_Reset(&runtime->performance);
    app_motion_reset_outputs(runtime);
    app_motion_reset_straight_estimator(runtime);
    runtime->prevAppliedLeftPwm = 0.0f;
    runtime->prevAppliedRightPwm = 0.0f;
    AppPid_Reset(&runtime->forwardSpeedPid);
}

void AppMotionSupervisor_Start(AppRuntimeContext_t *runtime)
{
    if (runtime == 0) {
        return;
    }

    runtime->state = APP_STATE_ARMING;
    runtime->snapshot.imuValid = 0u;

    if (app_motion_refresh_snapshot(runtime, 200u) == 0u) {
        AppMotionSupervisor_EnterFault(runtime, APP_FAULT_IMU_INIT);
        return;
    }

    DrvEncoder_Reset();
    AppPid_Reset(&runtime->headingPid);
    AppPid_Reset(&runtime->forwardSpeedPid);
    AppPid_Reset(&runtime->leftSpeedPid);
    AppPid_Reset(&runtime->rightSpeedPid);
    AppPerformanceMonitor_Reset(&runtime->performance);
    AppLearningManager_ClearRetrainRequest(&runtime->learning);

    runtime->headingLockDeg = runtime->snapshot.headingDeg;
    runtime->prevHeadingErrorDeg = 0.0f;
    runtime->currentHeadingErrorDeg = 0.0f;
    runtime->currentHeadingRateDegPerSec = 0.0f;
    runtime->filteredLeftSpeedRpm = runtime->snapshot.leftSpeedRpm;
    runtime->filteredRightSpeedRpm = runtime->snapshot.rightSpeedRpm;
    runtime->filteredAvgSpeedRpm = runtime->snapshot.avgSpeedRpm;
    runtime->forwardSpeedEstimateRpm = runtime->snapshot.avgSpeedRpm;
    runtime->straightLeftCountAccum = 0;
    runtime->straightRightCountAccum = 0;
    runtime->straightSpeedWindowTicks = 0u;
    runtime->prevAppliedLeftPwm = 0.0f;
    runtime->prevAppliedRightPwm = 0.0f;
    app_motion_reset_outputs(runtime);
    runtime->motorCommand.motorEnable = 1u;
    app_motion_apply_motor(&runtime->motorCommand);
    runtime->state = APP_STATE_RUNNING;
}

void AppMotionSupervisor_Stop(AppRuntimeContext_t *runtime)
{
    if (runtime == 0) {
        return;
    }

    runtime->state = APP_STATE_STOPPING;
    app_motion_reset_outputs(runtime);
    app_motion_reset_straight_estimator(runtime);
    runtime->prevAppliedLeftPwm = 0.0f;
    runtime->prevAppliedRightPwm = 0.0f;
    AppPid_Reset(&runtime->forwardSpeedPid);
    app_motion_apply_motor(&runtime->motorCommand);
    runtime->state = APP_STATE_IDLE;
}

void AppMotionSupervisor_UpdateFuzzy(AppRuntimeContext_t *runtime)
{
    AppPidGains_t fuzzyGains;

    if (runtime == 0) {
        return;
    }

    AppFuzzyTuner_Evaluate(&runtime->fuzzyTuner,
                           runtime->currentHeadingErrorDeg,
                           runtime->currentHeadingRateDegPerSec,
                           &fuzzyGains);
    AppLearningManager_Update(&runtime->learning,
                              &fuzzyGains,
                              AppPerformanceMonitor_Get(&runtime->performance),
                              (float)APP_FUZZY_PERIOD_MS / 1000.0f,
                              &runtime->appliedHeadingGains);
    AppPid_SetGains(&runtime->headingPid, &runtime->appliedHeadingGains);
}

void AppMotionSupervisor_ControlStep(AppRuntimeContext_t *runtime)
{
    float dtSeconds;
    float speedTarget;
    float headingControl;
    float steeringLimit;
    float minWheelTarget;
    float steerRatio;
    float basePwm;
    float leftPwm;
    float rightPwm;

    if (runtime == 0) {
        return;
    }

    dtSeconds = (float)APP_CONTROL_PERIOD_MS / 1000.0f;
    speedTarget = runtime->baseTargetSpeedRpm;
    app_motion_refresh_snapshot(runtime, 1u);
    app_motion_reset_outputs(runtime);

    if (runtime->state != APP_STATE_RUNNING) {
        app_motion_apply_motor(&runtime->motorCommand);
        return;
    }

    if ((runtime->snapshot.imuValid == 0u) || (runtime->imuMissTicks > 20u)) {
        AppMotionSupervisor_EnterFault(runtime, APP_FAULT_IMU_LOST);
        return;
    }

    if (runtime->runMode == APP_RUN_MODE_TRACK) {
        if ((runtime->snapshot.lineValid == 0u) && (runtime->snapshot.track.lostFrames > 20u)) {
            speedTarget *= 0.50f;
        }
        if ((runtime->snapshot.lineValid == 0u) && (runtime->snapshot.track.lostFrames > 80u)) {
            speedTarget = 0.0f;
        }
    }

    app_motion_update_heading_target(runtime);
    runtime->currentHeadingErrorDeg =
        DrvImuBno08x_GetYawError(runtime->motorCommand.targetHeadingDeg,
                                 runtime->snapshot.headingDeg);
    runtime->currentHeadingRateDegPerSec =
        (runtime->currentHeadingErrorDeg - runtime->prevHeadingErrorDeg) /
        dtSeconds;
    runtime->prevHeadingErrorDeg = runtime->currentHeadingErrorDeg;

    headingControl = AppPid_StepWithDerivative(&runtime->headingPid,
                                               runtime->currentHeadingErrorDeg,
                                               runtime->currentHeadingRateDegPerSec,
                                               dtSeconds);
    steeringLimit = runtime->maxSteeringRpm;
    if ((runtime->runMode == APP_RUN_MODE_STRAIGHT) && (speedTarget > 0.0f)) {
        /* Keep straight-mode heading correction smaller than the forward demand. */
        steeringLimit = app_motion_clamp(speedTarget * 0.35f, 3.0f, runtime->maxSteeringRpm);
    }
    headingControl = app_motion_clamp(headingControl, -steeringLimit, steeringLimit);

    minWheelTarget = (runtime->runMode == APP_RUN_MODE_STRAIGHT)
        ? ((speedTarget > 0.0f) ? (speedTarget * 0.35f) : 0.0f)
        : -runtime->maxWheelSpeedRpm;
    runtime->motorCommand.targetSpeedRpm = speedTarget;
    runtime->motorCommand.leftTargetRpm = app_motion_clamp(speedTarget - headingControl,
                                                           minWheelTarget,
                                                           runtime->maxWheelSpeedRpm);
    runtime->motorCommand.rightTargetRpm = app_motion_clamp(speedTarget + headingControl,
                                                            minWheelTarget,
                                                            runtime->maxWheelSpeedRpm);
    runtime->motorCommand.headingErrorDeg = runtime->currentHeadingErrorDeg;
    runtime->motorCommand.headingRateErrorDegPerSec = runtime->currentHeadingRateDegPerSec;
    runtime->motorCommand.headingControl = headingControl;

    if (runtime->runMode == APP_RUN_MODE_STRAIGHT) {
        basePwm = app_motion_step_forward_speed_pid(&runtime->forwardSpeedPid,
                                                    speedTarget,
                                                    runtime->forwardSpeedEstimateRpm,
                                                    dtSeconds);
        steerRatio = 0.0f;
        if (speedTarget > 0.5f) {
            steerRatio = headingControl / speedTarget;
        }
        steerRatio = app_motion_clamp(steerRatio, -0.60f, 0.60f);
        runtime->motorCommand.basePwm = basePwm;
        runtime->motorCommand.steeringPwm = basePwm * steerRatio;
        leftPwm = basePwm - runtime->motorCommand.steeringPwm;
        rightPwm = basePwm + runtime->motorCommand.steeringPwm;
        leftPwm = app_motion_limit_pwm_step(runtime->prevAppliedLeftPwm, leftPwm, 30.0f, 12.0f);
        rightPwm = app_motion_limit_pwm_step(runtime->prevAppliedRightPwm, rightPwm, 30.0f, 12.0f);
    } else {
        runtime->motorCommand.basePwm = 0.0f;
        runtime->motorCommand.steeringPwm = 0.0f;
        leftPwm = AppPid_Step(&runtime->leftSpeedPid,
                              runtime->motorCommand.leftTargetRpm - runtime->filteredLeftSpeedRpm,
                              dtSeconds);
        rightPwm = AppPid_Step(&runtime->rightSpeedPid,
                               runtime->motorCommand.rightTargetRpm - runtime->filteredRightSpeedRpm,
                               dtSeconds);
    }

    runtime->motorCommand.rawLeftPwm = leftPwm;
    runtime->motorCommand.rawRightPwm = rightPwm;
    leftPwm = app_motion_clamp(leftPwm,
                               (runtime->runMode == APP_RUN_MODE_STRAIGHT) ? 0.0f : -(float)runtime->maxPwm,
                               (float)runtime->maxPwm);
    rightPwm = app_motion_clamp(rightPwm,
                                (runtime->runMode == APP_RUN_MODE_STRAIGHT) ? 0.0f : -(float)runtime->maxPwm,
                                (float)runtime->maxPwm);
    runtime->motorCommand.leftPwm = app_motion_clamp_pwm(leftPwm, runtime->maxPwm);
    runtime->motorCommand.rightPwm = app_motion_clamp_pwm(rightPwm, runtime->maxPwm);
    runtime->motorCommand.motorEnable = 1u;
    runtime->prevAppliedLeftPwm = (float)runtime->motorCommand.leftPwm;
    runtime->prevAppliedRightPwm = (float)runtime->motorCommand.rightPwm;

    AppPerformanceMonitor_Update(&runtime->performance,
                                 runtime->currentHeadingErrorDeg,
                                 app_motion_abs(headingControl),
                                 dtSeconds);
    app_motion_apply_motor(&runtime->motorCommand);
}

uint8_t AppMotionSupervisor_SetRunMode(AppRuntimeContext_t *runtime, AppRunMode_t runMode)
{
    if ((runtime == 0) || (runMode >= APP_RUN_MODE_COUNT) || (runtime->state != APP_STATE_IDLE)) {
        return 0u;
    }

    runtime->runMode = runMode;
    runtime->profileDirty = 1u;
    return 1u;
}

void AppMotionSupervisor_CycleRunMode(AppRuntimeContext_t *runtime)
{
    if ((runtime == 0) || (runtime->state != APP_STATE_IDLE)) {
        return;
    }

    runtime->runMode = (AppRunMode_t)(((uint8_t)runtime->runMode + 1u) % APP_RUN_MODE_COUNT);
    runtime->profileDirty = 1u;
}

void AppMotionSupervisor_SetTuneMode(AppRuntimeContext_t *runtime, AppTuneMode_t tuneMode)
{
    if ((runtime == 0) || (tuneMode >= APP_TUNE_MODE_COUNT)) {
        return;
    }

    AppLearningManager_SetTuneMode(&runtime->learning, tuneMode);
    AppMotionSupervisor_UpdateFuzzy(runtime);
    runtime->profileDirty = 1u;
}

void AppMotionSupervisor_SetTargetSpeed(AppRuntimeContext_t *runtime, float targetSpeedRpm)
{
    if (runtime == 0) {
        return;
    }

    runtime->baseTargetSpeedRpm = app_motion_clamp(targetSpeedRpm,
                                                   -runtime->maxWheelSpeedRpm,
                                                   runtime->maxWheelSpeedRpm);
    runtime->profileDirty = 1u;
}

void AppMotionSupervisor_SetHeadingTrim(AppRuntimeContext_t *runtime, float trimDeg)
{
    if (runtime == 0) {
        return;
    }

    runtime->headingTrimDeg = app_motion_clamp(trimDeg, -45.0f, 45.0f);
    runtime->profileDirty = 1u;
}

void AppMotionSupervisor_SetTrackHeadingGain(AppRuntimeContext_t *runtime, float gainDegPerUnit)
{
    if (runtime == 0) {
        return;
    }

    runtime->trackHeadingGainDegPerUnit = app_motion_clamp(gainDegPerUnit, 0.5f, 20.0f);
    runtime->profileDirty = 1u;
}

void AppMotionSupervisor_SetHeadingPid(AppRuntimeContext_t *runtime, const AppPidGains_t *gains)
{
    if ((runtime == 0) || (gains == 0)) {
        return;
    }

    AppLearningManager_SetFixedHeadingGains(&runtime->learning, gains);
    AppFuzzyTuner_SetBaseGains(&runtime->fuzzyTuner, gains);
    if (runtime->learning.tuneMode == APP_TUNE_MODE_LEARNING) {
        AppLearningManager_RequestHeadingGains(&runtime->learning, gains);
    }
    AppMotionSupervisor_UpdateFuzzy(runtime);
    runtime->profileDirty = 1u;
}

void AppMotionSupervisor_SetSpeedPid(AppRuntimeContext_t *runtime, const AppPidGains_t *gains)
{
    if ((runtime == 0) || (gains == 0)) {
        return;
    }

    runtime->speedGains = *gains;
    AppPid_SetGains(&runtime->forwardSpeedPid, &runtime->speedGains);
    AppPid_SetGains(&runtime->leftSpeedPid, &runtime->speedGains);
    AppPid_SetGains(&runtime->rightSpeedPid, &runtime->speedGains);
    runtime->profileDirty = 1u;
}

void AppMotionSupervisor_SetRuleTable(AppRuntimeContext_t *runtime, const AppFuzzyRule_t *rules, uint16_t count)
{
    if ((runtime == 0) || (rules == 0)) {
        return;
    }

    if (AppFuzzyTuner_UpdateRules(&runtime->fuzzyTuner, rules, count) != 0u) {
        AppMotionSupervisor_UpdateFuzzy(runtime);
        runtime->profileDirty = 1u;
    }
}

void AppMotionSupervisor_SetMembershipAxes(AppRuntimeContext_t *runtime,
                                           const AppFuzzyAxisConfig_t *errorAxis,
                                           const AppFuzzyAxisConfig_t *rateAxis)
{
    if (runtime == 0) {
        return;
    }

    AppFuzzyTuner_SetAxes(&runtime->fuzzyTuner, errorAxis, rateAxis);
    AppMotionSupervisor_UpdateFuzzy(runtime);
    runtime->profileDirty = 1u;
}
