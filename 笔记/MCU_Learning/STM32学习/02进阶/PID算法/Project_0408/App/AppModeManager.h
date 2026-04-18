#ifndef __APP_MODE_MANAGER_H
#define __APP_MODE_MANAGER_H

#include "AppStraightController.h"
#include "AppTrackController.h"

typedef struct {
    AppMode_t selectedMode;
    AppMode_t activeMode;
    AppState_t state;
    uint32_t tickMs;
    uint16_t faultCode;
    AppControlSnapshot_t snapshot;
    AppMotorCommand_t motorCommand;
    AppStraightController_t straight;
    AppTrackController_t track;
    uint8_t imuReady;
} AppModeManager_t;

void AppModeManager_Init(AppModeManager_t *manager);
void AppModeManager_Process(AppModeManager_t *manager);

#endif
