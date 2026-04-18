#include "AppStraightController.h"
#include <string.h>

#define APP_STRAIGHT_DT              0.010f

static float app_straight_clamp(float value, float minValue, float maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static int16_t app_straight_to_pwm(float value, int16_t limit)
{
    if (value >= 0.0f) {
        value += 0.5f;
    } else {
        value -= 0.5f;
    }
    value = app_straight_clamp(value, (float)(-limit), (float)limit);
    return (int16_t)value;
}

static void app_straight_reset_integrators(AppStraightController_t *controller)
{
    controller->yawIntegral = 0.0f;
    controller->yawPrevError = 0.0f;
    controller->headingOut = 0.0f;
    controller->speedIntegral = 0.0f;
    controller->speedPrevError = 0.0f;
    controller->speedOut = 0.0f;
}

void AppStraightController_Init(AppStraightController_t *controller)
{
    memset(controller, 0, sizeof(*controller));

    controller->cfg.headingKp = 1.5f;
    controller->cfg.headingKi = 0.0f;
    controller->cfg.headingKd = 0.12f;
    controller->cfg.headingILimit = 200.0f;
    controller->cfg.headingOutLimit = 20.0f;
    controller->cfg.speedKp = 0.6f;
    controller->cfg.speedKi = 0.012f;
    controller->cfg.speedKd = 0.0f;
    controller->cfg.speedILimit = 400.0f;
    controller->cfg.speedOutLimit = 60.0f;
    controller->cfg.targetSpeed = 35.0f;
    controller->cfg.maxPwm = 60;
}

uint8_t AppStraightController_Start(AppStraightController_t *controller, const AppControlSnapshot_t *snapshot)
{
    if ((snapshot == 0) || (snapshot->imu.ahrsInited == 0u)) {
        return 0u;
    }

    controller->running = 1u;
    controller->imuOk = snapshot->imu.ahrsInited;
    controller->targetYaw = snapshot->imu.yaw;
    controller->actualYaw = snapshot->imu.yaw;
    controller->leftPwm = 0;
    controller->rightPwm = 0;
    app_straight_reset_integrators(controller);
    return 1u;
}

void AppStraightController_Stop(AppStraightController_t *controller)
{
    controller->running = 0u;
    controller->leftPwm = 0;
    controller->rightPwm = 0;
    app_straight_reset_integrators(controller);
}

void AppStraightController_LockHeading(AppStraightController_t *controller, const AppControlSnapshot_t *snapshot)
{
    if ((snapshot != 0) && (snapshot->imu.ahrsInited != 0u)) {
        controller->targetYaw = snapshot->imu.yaw;
    }
}

void AppStraightController_Step(AppStraightController_t *controller, const AppControlSnapshot_t *snapshot, AppMotorCommand_t *command)
{
    float yawDeriv;
    float speedDeriv;
    int16_t basePwm;
    int16_t diffPwm;

    if ((controller == 0) || (snapshot == 0) || (command == 0)) {
        return;
    }

    command->leftPwm = 0;
    command->rightPwm = 0;
    command->motorEnable = 0u;

    if (controller->running == 0u) {
        return;
    }

    if ((snapshot->imuValid != 0u) && (snapshot->imu.ahrsInited != 0u)) {
        controller->imuOk = 1u;
        controller->actualYaw = snapshot->imu.yaw;
    }

    if (controller->imuOk != 0u) {
        controller->yawError = DrvImuBno08x_GetYawError(controller->targetYaw, controller->actualYaw);
        controller->yawIntegral += controller->yawError * APP_STRAIGHT_DT;
        controller->yawIntegral = app_straight_clamp(controller->yawIntegral,
                                                     -controller->cfg.headingILimit,
                                                     controller->cfg.headingILimit);
        yawDeriv = (controller->yawError - controller->yawPrevError) / APP_STRAIGHT_DT;
        controller->headingOut = controller->cfg.headingKp * controller->yawError +
                                 controller->cfg.headingKi * controller->yawIntegral +
                                 controller->cfg.headingKd * yawDeriv;
        controller->headingOut = app_straight_clamp(controller->headingOut,
                                                    -controller->cfg.headingOutLimit,
                                                    controller->cfg.headingOutLimit);
        controller->yawPrevError = controller->yawError;
    } else {
        controller->yawError = 0.0f;
        controller->headingOut = 0.0f;
    }

    controller->actualSpeed = ((float)snapshot->encoder.leftSpeed + (float)snapshot->encoder.rightSpeed) * 0.5f;
    controller->speedError = controller->cfg.targetSpeed - controller->actualSpeed;
    controller->speedIntegral += controller->speedError * APP_STRAIGHT_DT;
    controller->speedIntegral = app_straight_clamp(controller->speedIntegral,
                                                   -controller->cfg.speedILimit,
                                                   controller->cfg.speedILimit);
    speedDeriv = (controller->speedError - controller->speedPrevError) / APP_STRAIGHT_DT;
    controller->speedOut = controller->cfg.speedKp * controller->speedError +
                           controller->cfg.speedKi * controller->speedIntegral +
                           controller->cfg.speedKd * speedDeriv;
    controller->speedOut = app_straight_clamp(controller->speedOut,
                                              -controller->cfg.speedOutLimit,
                                              controller->cfg.speedOutLimit);
    controller->speedPrevError = controller->speedError;

    basePwm = app_straight_to_pwm(controller->speedOut, controller->cfg.maxPwm);
    diffPwm = app_straight_to_pwm(controller->headingOut, controller->cfg.maxPwm);

    controller->leftPwm = app_straight_to_pwm((float)(basePwm + diffPwm), controller->cfg.maxPwm);
    controller->rightPwm = app_straight_to_pwm((float)(basePwm - diffPwm), controller->cfg.maxPwm);

    command->leftPwm = controller->leftPwm;
    command->rightPwm = controller->rightPwm;
    command->motorEnable = 1u;
}

void AppStraightController_SetTargetSpeed(AppStraightController_t *controller, float targetSpeed)
{
    if (controller == 0) {
        return;
    }
    controller->cfg.targetSpeed = targetSpeed;
}

void AppStraightController_SetHeadingPid(AppStraightController_t *controller, float kp, float ki, float kd)
{
    if (controller == 0) {
        return;
    }
    controller->cfg.headingKp = kp;
    controller->cfg.headingKi = ki;
    controller->cfg.headingKd = kd;
    controller->yawIntegral = 0.0f;
}

void AppStraightController_SetSpeedPid(AppStraightController_t *controller, float kp, float ki, float kd)
{
    if (controller == 0) {
        return;
    }
    controller->cfg.speedKp = kp;
    controller->cfg.speedKi = ki;
    controller->cfg.speedKd = kd;
    controller->speedIntegral = 0.0f;
}
