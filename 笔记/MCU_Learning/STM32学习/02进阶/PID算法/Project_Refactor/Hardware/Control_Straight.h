#ifndef __CONTROL_STRAIGHT_H
#define __CONTROL_STRAIGHT_H

#include "Control.h"

typedef ControlSystem_t ControlStraightSystem_t;

void ControlStraight_Init(ControlStraightSystem_t *sys, uint8_t skipICM);
void ControlStraight_LoadStableDefaults(ControlStraightSystem_t *sys);
uint8_t ControlStraight_Start(ControlStraightSystem_t *sys);
void ControlStraight_Stop(ControlStraightSystem_t *sys);
void ControlStraight_LockHeading(ControlStraightSystem_t *sys);
void ControlStraight_SetTargetSpeed(ControlStraightSystem_t *sys, float speed);
void ControlStraight_TimerTickISR(ControlStraightSystem_t *sys);
void ControlStraight_Tick(ControlStraightSystem_t *sys);
void ControlStraight_Background(ControlStraightSystem_t *sys);

#endif
