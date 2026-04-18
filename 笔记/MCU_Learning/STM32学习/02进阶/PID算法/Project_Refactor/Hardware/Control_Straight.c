#include "Control_Straight.h"

void ControlStraight_Init(ControlStraightSystem_t *sys, uint8_t skipICM)
{
    Control_Init(sys, skipICM);
}

void ControlStraight_LoadStableDefaults(ControlStraightSystem_t *sys)
{
    Control_LoadStableDefaults(sys);
}

uint8_t ControlStraight_Start(ControlStraightSystem_t *sys)
{
    return Control_Start(sys);
}

void ControlStraight_Stop(ControlStraightSystem_t *sys)
{
    Control_Stop(sys);
}

void ControlStraight_LockHeading(ControlStraightSystem_t *sys)
{
    Control_LockHeading(sys);
}

void ControlStraight_SetTargetSpeed(ControlStraightSystem_t *sys, float speed)
{
    Control_SetTargetSpeed(sys, speed);
}

void ControlStraight_TimerTickISR(ControlStraightSystem_t *sys)
{
    Control_TimerTickISR(sys);
}

void ControlStraight_Tick(ControlStraightSystem_t *sys)
{
    Control_Tick(sys);
}

void ControlStraight_Background(ControlStraightSystem_t *sys)
{
    Control_Background(sys);
}
