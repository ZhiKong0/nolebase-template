#include "AppCommandDispatcher.h"
#include "DrvTelemetry.h"
#include "AppConfigProfile.h"
#include "AppMotionSupervisor.h"
#include "AppTelemetryService.h"

/* Keep large command work buffers out of the main stack path. */
static char g_appCommandBuffer[1536];
static AppProtocolCommand_t g_appCommand;

static void app_command_dispatcher_handle(AppRuntimeContext_t *runtime, const AppProtocolCommand_t *command)
{
    if ((runtime == 0) || (command == 0)) {
        return;
    }

    if (command->requestClearFault != 0u) {
        AppMotionSupervisor_ClearFault(runtime);
        AppTelemetryService_SendAck("clear_fault", "ok");
    }

    if (command->requestLoadProfile != 0u) {
        if (runtime->state == APP_STATE_IDLE) {
            if (AppConfigProfile_LoadSaved(runtime) != 0u) {
                AppTelemetryService_SendAck("profile_load", "ok");
                AppTelemetryService_SendStatus(runtime);
            } else {
                AppTelemetryService_SendAck("profile_load", "missing");
            }
        } else {
            AppTelemetryService_SendAck("profile_load", "busy");
        }
    }

    if (command->hasRunMode != 0u) {
        if (AppMotionSupervisor_SetRunMode(runtime, command->runMode) != 0u) {
            AppTelemetryService_SendAck("mode", "ok");
        } else {
            AppTelemetryService_SendAck("mode", "busy");
        }
    }

    if (command->hasTuneMode != 0u) {
        AppMotionSupervisor_SetTuneMode(runtime, command->tuneMode);
        AppTelemetryService_SendAck("tune", "ok");
    }

    if (command->hasTargetSpeed != 0u) {
        AppMotionSupervisor_SetTargetSpeed(runtime, command->targetSpeedRpm);
        AppTelemetryService_SendAck("target_speed", "ok");
    }

    if (command->hasTargetHeading != 0u) {
        AppMotionSupervisor_SetHeadingTrim(runtime, command->targetHeadingDeg);
        AppTelemetryService_SendAck("target_heading", "ok");
    }

    if (command->hasHeadingPid != 0u) {
        AppMotionSupervisor_SetHeadingPid(runtime, &command->headingPid);
        AppTelemetryService_SendAck("heading_pid", "ok");
    }

    if (command->hasSpeedPid != 0u) {
        AppMotionSupervisor_SetSpeedPid(runtime, &command->speedPid);
        AppTelemetryService_SendAck("speed_pid", "ok");
    }

    if (command->hasRules != 0u) {
        AppMotionSupervisor_SetRuleTable(runtime, command->rules, APP_RULE_COUNT);
        AppTelemetryService_SendAck("rules", "ok");
    }

    if ((command->hasErrorAxis != 0u) || (command->hasRateAxis != 0u)) {
        AppMotionSupervisor_SetMembershipAxes(runtime,
                                              (command->hasErrorAxis != 0u) ? &command->errorAxis : 0,
                                              (command->hasRateAxis != 0u) ? &command->errorRateAxis : 0);
        AppTelemetryService_SendAck("membership", "ok");
    }

    if (command->requestStatus != 0u) {
        AppTelemetryService_SendStatus(runtime);
    }

    if (command->requestSaveProfile != 0u) {
        if (runtime->state == APP_STATE_IDLE) {
            if (AppConfigProfile_SaveCurrent(runtime) != 0u) {
                AppTelemetryService_SendAck("profile_save", "ok");
                AppTelemetryService_SendStatus(runtime);
            } else {
                AppTelemetryService_SendAck("profile_save", "err");
            }
        } else {
            AppTelemetryService_SendAck("profile_save", "busy");
        }
    }

    if ((command->requestStart != 0u) && (runtime->state == APP_STATE_IDLE)) {
        AppMotionSupervisor_Start(runtime);
        AppTelemetryService_SendAck("start", (runtime->state == APP_STATE_RUNNING) ? "ok" : "fault");
    }

    if (command->requestStop != 0u) {
        if (runtime->state == APP_STATE_RUNNING) {
            AppMotionSupervisor_Stop(runtime);
        }
        AppTelemetryService_SendAck("stop", "ok");
    }
}

void AppCommandDispatcher_Poll(AppRuntimeContext_t *runtime)
{
    if (runtime == 0) {
        return;
    }

    if (DrvTelemetry_TryReadCommand(g_appCommandBuffer, (uint16_t)sizeof(g_appCommandBuffer)) == 0u) {
        return;
    }

    if (AppProtocol_ParseCommand(g_appCommandBuffer, &g_appCommand) == 0u) {
        AppMotionSupervisor_EnterFault(runtime, APP_FAULT_BAD_COMMAND);
        AppTelemetryService_SendAck("parse", "err");
        return;
    }

    app_command_dispatcher_handle(runtime, &g_appCommand);
}
