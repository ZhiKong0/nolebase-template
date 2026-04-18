#ifndef __APP_STRAIGHT_CONTROLLER_H
#define __APP_STRAIGHT_CONTROLLER_H

#include "AppTypes.h"

typedef struct {
    float headingKp;
    float headingKi;
    float headingKd;
    float headingILimit;
    float headingOutLimit;
    float speedKp;
    float speedKi;
    float speedKd;
    float speedILimit;
    float speedOutLimit;
    float targetSpeed;
    int16_t maxPwm;
} AppStraightControllerConfig_t;

typedef struct {
    AppStraightControllerConfig_t cfg;
    float targetYaw;
    float actualYaw;
    float actualSpeed;
    float yawError;
    float yawIntegral;
    float yawPrevError;
    float headingOut;
    float speedError;
    float speedIntegral;
    float speedPrevError;
    float speedOut;
    int16_t leftPwm;
    int16_t rightPwm;
    uint8_t running;
    uint8_t imuOk;
} AppStraightController_t;

void AppStraightController_Init(AppStraightController_t *controller);
uint8_t AppStraightController_Start(AppStraightController_t *controller, const AppControlSnapshot_t *snapshot);
void AppStraightController_Stop(AppStraightController_t *controller);
void AppStraightController_LockHeading(AppStraightController_t *controller, const AppControlSnapshot_t *snapshot);
void AppStraightController_Step(AppStraightController_t *controller, const AppControlSnapshot_t *snapshot, AppMotorCommand_t *command);
void AppStraightController_SetTargetSpeed(AppStraightController_t *controller, float targetSpeed);
void AppStraightController_SetHeadingPid(AppStraightController_t *controller, float kp, float ki, float kd);
void AppStraightController_SetSpeedPid(AppStraightController_t *controller, float kp, float ki, float kd);

#endif
