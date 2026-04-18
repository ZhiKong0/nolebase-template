#include "AppUiService.h"
#include "DrvDisplay.h"
#include "DrvKey.h"
#include "AppMotionSupervisor.h"
#include <stdio.h>

static const char *app_ui_run_mode_name(AppRunMode_t mode)
{
    return (mode == APP_RUN_MODE_TRACK) ? "TRACK" : "STRAI";
}

static const char *app_ui_tune_mode_name(AppTuneMode_t mode)
{
    if (mode == APP_TUNE_MODE_FUZZY) {
        return "FUZ";
    }
    if (mode == APP_TUNE_MODE_LEARNING) {
        return "LRN";
    }
    return "FIX";
}

static const char *app_ui_state_name(AppState_t state)
{
    switch (state) {
        case APP_STATE_IDLE: return "IDLE";
        case APP_STATE_ARMING: return "ARM ";
        case APP_STATE_RUNNING: return "RUN ";
        case APP_STATE_STOPPING: return "STOP";
        case APP_STATE_FAULT: return "FLT ";
        default: return "UNKN";
    }
}

void AppUiService_Init(void)
{
    DrvKey_Init();
}

void AppUiService_HandleKey(AppRuntimeContext_t *runtime)
{
    DrvKeyEvent_t keyEvent;

    if (runtime == 0) {
        return;
    }

    keyEvent = DrvKey_Poll(runtime->tickMs);
    if (keyEvent == DRV_KEY_EVENT_NONE) {
        return;
    }

    if (keyEvent == DRV_KEY_EVENT_LONG) {
        if (runtime->state == APP_STATE_IDLE) {
            AppMotionSupervisor_CycleRunMode(runtime);
        } else if (runtime->state == APP_STATE_FAULT) {
            AppMotionSupervisor_ClearFault(runtime);
        }
        return;
    }

    if (runtime->state == APP_STATE_RUNNING) {
        AppMotionSupervisor_Stop(runtime);
    } else if (runtime->state == APP_STATE_FAULT) {
        AppMotionSupervisor_ClearFault(runtime);
    } else if (runtime->state == APP_STATE_IDLE) {
        AppMotionSupervisor_Start(runtime);
    }
}

void AppUiService_Render(const AppRuntimeContext_t *runtime)
{
    char line1[17];
    char line2[17];
    char line3[17];
    char line4[17];

    if (runtime == 0) {
        return;
    }

    snprintf(line1, sizeof(line1), "%s %s %s",
             app_ui_run_mode_name(runtime->runMode),
             app_ui_tune_mode_name(runtime->learning.tuneMode),
             app_ui_state_name(runtime->state));
    snprintf(line2, sizeof(line2), "V%5.1f Y%5.1f",
             runtime->snapshot.avgSpeedRpm,
             runtime->snapshot.headingDeg);

    if (runtime->state == APP_STATE_FAULT) {
        snprintf(line3, sizeof(line3), "FAULT=%4u", (unsigned)runtime->faultCode);
        snprintf(line4, sizeof(line4), "CLR:SHORT KEY ");
    } else {
        snprintf(line3, sizeof(line3), "E%5.1f C%5.1f",
                 runtime->currentHeadingErrorDeg,
                 runtime->motorCommand.headingControl);
        snprintf(line4, sizeof(line4), "L%3d R%3d %c",
                 runtime->motorCommand.leftPwm,
                 runtime->motorCommand.rightPwm,
                 (runtime->learning.retrainRequested != 0u) ? 'R' : '-');
    }

    DrvDisplay_ShowLine(1u, line1);
    DrvDisplay_ShowLine(2u, line2);
    DrvDisplay_ShowLine(3u, line3);
    DrvDisplay_ShowLine(4u, line4);
}
