#ifndef __APP_TRACK_CONTROLLER_H
#define __APP_TRACK_CONTROLLER_H

#include "AppTypes.h"

typedef struct {
    float lineKp;
    float lineKi;
    float lineKd;
    float lineILimit;
    float lineDiffLimit;
    float speedKp;
    float speedKi;
    float speedKd;
    float speedILimit;
    float speedOutLimit;
    float filtAlpha;
    float deadband;
    float targetSpeed;
    int16_t maxPwm;
} AppTrackControllerConfig_t;

typedef struct {
    AppTrackControllerConfig_t cfg;
    float filteredLinePos;
    float lineErr;
    float lineIntegral;
    float linePrevErr;
    float lineOut;
    float leftTarget;
    float leftActual;
    float leftErr;
    float leftIntegral;
    float leftPrevErr;
    float leftOut;
    float rightTarget;
    float rightActual;
    float rightErr;
    float rightIntegral;
    float rightPrevErr;
    float rightOut;
    uint16_t lostFrames;
    int8_t recoveryDirection;
    uint8_t activeMask;
    uint8_t activeCount;
    int16_t leftPwm;
    int16_t rightPwm;
    uint8_t running;
} AppTrackController_t;

void AppTrackController_Init(AppTrackController_t *controller);
uint8_t AppTrackController_Start(AppTrackController_t *controller, const AppControlSnapshot_t *snapshot);
void AppTrackController_Stop(AppTrackController_t *controller);
void AppTrackController_Step(AppTrackController_t *controller, const AppControlSnapshot_t *snapshot, AppMotorCommand_t *command);
void AppTrackController_SetTargetSpeed(AppTrackController_t *controller, float targetSpeed);
void AppTrackController_SetLinePid(AppTrackController_t *controller, float kp, float ki, float kd);
void AppTrackController_SetSpeedPid(AppTrackController_t *controller, float kp, float ki, float kd);

#endif
