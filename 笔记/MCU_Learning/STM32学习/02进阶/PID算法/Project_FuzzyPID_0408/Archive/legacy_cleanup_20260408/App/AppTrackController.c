#include "AppTrackController.h"
#include <string.h>
#include <math.h>

#define APP_TRACK_DT                 0.010f

static float app_track_clamp(float value, float minValue, float maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static int16_t app_track_to_pwm(float value, int16_t limit)
{
    if (value >= 0.0f) {
        value += 0.5f;
    } else {
        value -= 0.5f;
    }
    value = app_track_clamp(value, (float)(-limit), (float)limit);
    return (int16_t)value;
}

void AppTrackController_Init(AppTrackController_t *controller)
{
    memset(controller, 0, sizeof(*controller));

    controller->cfg.lineKp = 0.4f;
    controller->cfg.lineKi = 0.004f;
    controller->cfg.lineKd = 0.0f;
    controller->cfg.lineILimit = 120.0f;
    controller->cfg.lineDiffLimit = 0.55f;
    controller->cfg.speedKp = 10.0f;
    controller->cfg.speedKi = 0.25f;
    controller->cfg.speedKd = 0.0f;
    controller->cfg.speedILimit = 240.0f;
    controller->cfg.speedOutLimit = 100.0f;
    controller->cfg.filtAlpha = 0.45f;
    controller->cfg.deadband = 0.15f;
    controller->cfg.targetSpeed = 18.0f;
    controller->cfg.maxPwm = 72;
}

uint8_t AppTrackController_Start(AppTrackController_t *controller, const AppControlSnapshot_t *snapshot)
{
    controller->running = 1u;
    controller->filteredLinePos = 0.0f;
    controller->lineIntegral = 0.0f;
    controller->linePrevErr = 0.0f;
    controller->leftIntegral = 0.0f;
    controller->leftPrevErr = 0.0f;
    controller->rightIntegral = 0.0f;
    controller->rightPrevErr = 0.0f;
    controller->lostFrames = 0u;
    controller->recoveryDirection = 0;
    controller->leftPwm = 0;
    controller->rightPwm = 0;

    if ((snapshot != 0) && (snapshot->track.hasLine != 0u)) {
        controller->filteredLinePos = snapshot->track.rawPosition;
        controller->recoveryDirection = snapshot->track.lastDirection;
    }

    return 1u;
}

void AppTrackController_Stop(AppTrackController_t *controller)
{
    controller->running = 0u;
    controller->leftPwm = 0;
    controller->rightPwm = 0;
    controller->leftOut = 0.0f;
    controller->rightOut = 0.0f;
    controller->lineOut = 0.0f;
}

void AppTrackController_Step(AppTrackController_t *controller, const AppControlSnapshot_t *snapshot, AppMotorCommand_t *command)
{
    float deriv;
    float rawErr;

    if ((controller == 0) || (snapshot == 0) || (command == 0)) {
        return;
    }

    command->leftPwm = 0;
    command->rightPwm = 0;
    command->motorEnable = 0u;

    if (controller->running == 0u) {
        return;
    }

    controller->activeMask = snapshot->track.activeMask;
    controller->activeCount = snapshot->track.activeCount;
    controller->lostFrames = snapshot->track.lostFrames;

    if (snapshot->track.hasLine != 0u) {
        controller->filteredLinePos =
            controller->cfg.filtAlpha * snapshot->track.rawPosition +
            (1.0f - controller->cfg.filtAlpha) * controller->filteredLinePos;
        if (snapshot->track.lastDirection != 0) {
            controller->recoveryDirection = snapshot->track.lastDirection;
        } else if (controller->filteredLinePos > 0.05f) {
            controller->recoveryDirection = 1;
        } else if (controller->filteredLinePos < -0.05f) {
            controller->recoveryDirection = -1;
        }
    }

    rawErr = 0.0f - controller->filteredLinePos;
    if (fabsf(rawErr) < controller->cfg.deadband) {
        rawErr = 0.0f;
    }

    controller->lineErr = rawErr;
    controller->lineIntegral += rawErr * APP_TRACK_DT;
    controller->lineIntegral = app_track_clamp(controller->lineIntegral,
                                               -controller->cfg.lineILimit,
                                               controller->cfg.lineILimit);
    deriv = (rawErr - controller->linePrevErr) / APP_TRACK_DT;
    controller->lineOut = controller->cfg.lineKp * rawErr +
                          controller->cfg.lineKi * controller->lineIntegral +
                          controller->cfg.lineKd * deriv;
    controller->lineOut = app_track_clamp(controller->lineOut,
                                          -controller->cfg.lineDiffLimit,
                                          controller->cfg.lineDiffLimit);
    controller->linePrevErr = rawErr;

    if (controller->lostFrames > 10u) {
        if (controller->recoveryDirection > 0) {
            controller->lineOut = controller->cfg.lineDiffLimit;
        } else if (controller->recoveryDirection < 0) {
            controller->lineOut = -controller->cfg.lineDiffLimit;
        } else {
            controller->lineOut = 0.0f;
        }
    }

    controller->leftActual = (float)snapshot->encoder.leftSpeed;
    controller->rightActual = (float)snapshot->encoder.rightSpeed;
    controller->leftTarget = controller->cfg.targetSpeed + controller->lineOut;
    controller->rightTarget = controller->cfg.targetSpeed - controller->lineOut;

    controller->leftErr = controller->leftTarget - controller->leftActual;
    controller->leftIntegral += controller->leftErr * APP_TRACK_DT;
    controller->leftIntegral = app_track_clamp(controller->leftIntegral,
                                               -controller->cfg.speedILimit,
                                               controller->cfg.speedILimit);
    deriv = (controller->leftErr - controller->leftPrevErr) / APP_TRACK_DT;
    controller->leftOut = controller->cfg.speedKp * controller->leftErr +
                          controller->cfg.speedKi * controller->leftIntegral +
                          controller->cfg.speedKd * deriv;
    controller->leftOut = app_track_clamp(controller->leftOut,
                                          -controller->cfg.speedOutLimit,
                                          controller->cfg.speedOutLimit);
    controller->leftPrevErr = controller->leftErr;

    controller->rightErr = controller->rightTarget - controller->rightActual;
    controller->rightIntegral += controller->rightErr * APP_TRACK_DT;
    controller->rightIntegral = app_track_clamp(controller->rightIntegral,
                                                -controller->cfg.speedILimit,
                                                controller->cfg.speedILimit);
    deriv = (controller->rightErr - controller->rightPrevErr) / APP_TRACK_DT;
    controller->rightOut = controller->cfg.speedKp * controller->rightErr +
                           controller->cfg.speedKi * controller->rightIntegral +
                           controller->cfg.speedKd * deriv;
    controller->rightOut = app_track_clamp(controller->rightOut,
                                           -controller->cfg.speedOutLimit,
                                           controller->cfg.speedOutLimit);
    controller->rightPrevErr = controller->rightErr;

    controller->leftPwm = app_track_to_pwm(controller->leftOut, controller->cfg.maxPwm);
    controller->rightPwm = app_track_to_pwm(controller->rightOut, controller->cfg.maxPwm);

    command->leftPwm = controller->leftPwm;
    command->rightPwm = controller->rightPwm;
    command->motorEnable = 1u;
}

void AppTrackController_SetTargetSpeed(AppTrackController_t *controller, float targetSpeed)
{
    if (controller == 0) {
        return;
    }
    controller->cfg.targetSpeed = targetSpeed;
}

void AppTrackController_SetLinePid(AppTrackController_t *controller, float kp, float ki, float kd)
{
    if (controller == 0) {
        return;
    }
    controller->cfg.lineKp = kp;
    controller->cfg.lineKi = ki;
    controller->cfg.lineKd = kd;
    controller->lineIntegral = 0.0f;
}

void AppTrackController_SetSpeedPid(AppTrackController_t *controller, float kp, float ki, float kd)
{
    if (controller == 0) {
        return;
    }
    controller->cfg.speedKp = kp;
    controller->cfg.speedKi = ki;
    controller->cfg.speedKd = kd;
    controller->leftIntegral = 0.0f;
    controller->rightIntegral = 0.0f;
}
