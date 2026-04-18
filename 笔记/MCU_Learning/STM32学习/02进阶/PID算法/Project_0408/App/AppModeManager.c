#include "AppModeManager.h"
#include "BoardConfig.h"
#include "BspControlTick.h"
#include "DrvDisplay.h"
#include "DrvTelemetry.h"
#include "DrvKey.h"
#include "DrvMotor.h"
#include "DrvEncoder.h"
#include "DrvTrackSensor.h"
#include "DrvImuBno08x.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define APP_FAULT_NONE               0u
#define APP_FAULT_IMU_NOT_READY      1u

static const char *app_mode_name(AppMode_t mode)
{
    return (mode == APP_MODE_TRACK) ? "TRACK" : "STRAIGHT";
}

static const char *app_state_name(AppState_t state)
{
    switch (state) {
        case APP_STATE_IDLE: return "IDLE";
        case APP_STATE_STARTING: return "START";
        case APP_STATE_RUNNING: return "RUN";
        case APP_STATE_STOPPING: return "STOP";
        case APP_STATE_FAULT: return "FAULT";
        default: return "UNKN";
    }
}

static float app_parse_value(const char *cmd)
{
    const char *equals;

    if (cmd == 0) {
        return 0.0f;
    }

    equals = strchr(cmd, '=');
    if (equals == 0) {
        return 0.0f;
    }

    return (float)atof(equals + 1);
}

static void app_apply_motor_command(const AppMotorCommand_t *command)
{
    if ((command == 0) || (command->motorEnable == 0u)) {
        DrvMotor_Stop();
        DrvMotor_SetEnabled(0u);
        return;
    }

    DrvMotor_SetEnabled(1u);
    DrvMotor_Apply(command->leftPwm, command->rightPwm);
}

static void app_show_status(const AppModeManager_t *manager)
{
    char line1[17];
    char line2[17];
    char line3[17];
    char line4[17];

    snprintf(line1, sizeof(line1), "MODE:%-10s", app_mode_name(manager->selectedMode));
    snprintf(line2, sizeof(line2), "STATE:%-9s", app_state_name(manager->state));

    if (manager->selectedMode == APP_MODE_STRAIGHT) {
        snprintf(line3, sizeof(line3), "Y%6.1f V%4d",
                 manager->straight.actualYaw,
                 (int)manager->straight.actualSpeed);
        snprintf(line4, sizeof(line4), "L%4d R%4d",
                 manager->motorCommand.leftPwm,
                 manager->motorCommand.rightPwm);
    } else {
        snprintf(line3, sizeof(line3), "P%6.2f C%2u",
                 manager->track.filteredLinePos,
                 (unsigned)manager->track.activeCount);
        snprintf(line4, sizeof(line4), "L%4d R%4d",
                 manager->motorCommand.leftPwm,
                 manager->motorCommand.rightPwm);
    }

    if (manager->state == APP_STATE_FAULT) {
        snprintf(line3, sizeof(line3), "FAULT:%-10u", (unsigned)manager->faultCode);
        snprintf(line4, sizeof(line4), "SHORT=CLR      ");
    }

    DrvDisplay_ShowLine(1u, line1);
    DrvDisplay_ShowLine(2u, line2);
    DrvDisplay_ShowLine(3u, line3);
    DrvDisplay_ShowLine(4u, line4);
}

static void app_send_status(const AppModeManager_t *manager)
{
    char buf[128];

    if (manager->selectedMode == APP_MODE_STRAIGHT) {
        snprintf(buf, sizeof(buf),
                 "HB mode=%s state=%s yaw=%.2f spd=%.2f lpwm=%d rpwm=%d\r\n",
                 app_mode_name(manager->selectedMode),
                 app_state_name(manager->state),
                 manager->straight.actualYaw,
                 manager->straight.actualSpeed,
                 manager->motorCommand.leftPwm,
                 manager->motorCommand.rightPwm);
    } else {
        snprintf(buf, sizeof(buf),
                 "HB mode=%s state=%s pos=%.2f cnt=%u lpwm=%d rpwm=%d\r\n",
                 app_mode_name(manager->selectedMode),
                 app_state_name(manager->state),
                 manager->track.filteredLinePos,
                 (unsigned)manager->track.activeCount,
                 manager->motorCommand.leftPwm,
                 manager->motorCommand.rightPwm);
    }

    DrvTelemetry_SendText(buf);
}

static void app_clear_fault(AppModeManager_t *manager)
{
    manager->faultCode = APP_FAULT_NONE;
    manager->state = APP_STATE_IDLE;
    manager->motorCommand.leftPwm = 0;
    manager->motorCommand.rightPwm = 0;
    manager->motorCommand.motorEnable = 0u;
    app_apply_motor_command(&manager->motorCommand);
}

static void app_stop_mode(AppModeManager_t *manager)
{
    manager->state = APP_STATE_STOPPING;

    AppStraightController_Stop(&manager->straight);
    AppTrackController_Stop(&manager->track);

    manager->motorCommand.leftPwm = 0;
    manager->motorCommand.rightPwm = 0;
    manager->motorCommand.motorEnable = 0u;
    app_apply_motor_command(&manager->motorCommand);

    manager->state = APP_STATE_IDLE;
}

static uint8_t app_refresh_imu(AppModeManager_t *manager, uint16_t tries)
{
    uint16_t i;

    for (i = 0u; i < tries; i++) {
        if (DrvImuBno08x_Read(&manager->snapshot.imu) != 0u) {
            DrvImuBno08x_UpdateYaw(&manager->snapshot.imu, 0.005f);
            manager->snapshot.imuValid = (manager->snapshot.imu.ahrsInited != 0u) ? 1u : 0u;
            if (manager->snapshot.imuValid != 0u) {
                return 1u;
            }
        }
    }

    return 0u;
}

static void app_start_selected_mode(AppModeManager_t *manager)
{
    manager->state = APP_STATE_STARTING;

    if (manager->selectedMode == APP_MODE_STRAIGHT) {
        if (app_refresh_imu(manager, 300u) == 0u) {
            manager->faultCode = APP_FAULT_IMU_NOT_READY;
            manager->state = APP_STATE_FAULT;
            return;
        }
        if (AppStraightController_Start(&manager->straight, &manager->snapshot) == 0u) {
            manager->faultCode = APP_FAULT_IMU_NOT_READY;
            manager->state = APP_STATE_FAULT;
            return;
        }
    } else {
        DrvTrackSensor_Sample(&manager->snapshot.track);
        (void)AppTrackController_Start(&manager->track, &manager->snapshot);
    }

    DrvEncoder_Reset();
    manager->activeMode = manager->selectedMode;
    manager->motorCommand.leftPwm = 0;
    manager->motorCommand.rightPwm = 0;
    manager->motorCommand.motorEnable = 1u;
    app_apply_motor_command(&manager->motorCommand);
    manager->faultCode = APP_FAULT_NONE;
    manager->state = APP_STATE_RUNNING;
}

static void app_control_step(AppModeManager_t *manager)
{
    DrvEncoder_Sample(&manager->snapshot.encoder, APP_CONTROL_PERIOD_MS);
    DrvTrackSensor_Sample(&manager->snapshot.track);

    if (DrvImuBno08x_Read(&manager->snapshot.imu) != 0u) {
        DrvImuBno08x_UpdateYaw(&manager->snapshot.imu, 0.010f);
        manager->snapshot.imuValid = (manager->snapshot.imu.ahrsInited != 0u) ? 1u : manager->snapshot.imuValid;
    }

    manager->motorCommand.leftPwm = 0;
    manager->motorCommand.rightPwm = 0;
    manager->motorCommand.motorEnable = 0u;

    if (manager->state != APP_STATE_RUNNING) {
        app_apply_motor_command(&manager->motorCommand);
        return;
    }

    if (manager->activeMode == APP_MODE_STRAIGHT) {
        AppStraightController_Step(&manager->straight, &manager->snapshot, &manager->motorCommand);
    } else {
        AppTrackController_Step(&manager->track, &manager->snapshot, &manager->motorCommand);
    }

    app_apply_motor_command(&manager->motorCommand);
}

static void app_handle_key(AppModeManager_t *manager)
{
    DrvKeyEvent_t keyEvent = DrvKey_Poll(manager->tickMs);

    if (keyEvent == DRV_KEY_EVENT_NONE) {
        return;
    }

    if (keyEvent == DRV_KEY_EVENT_LONG) {
        if (manager->state == APP_STATE_IDLE) {
            manager->selectedMode = (AppMode_t)(((uint8_t)manager->selectedMode + 1u) % APP_MODE_COUNT);
        }
        return;
    }

    if (manager->state == APP_STATE_RUNNING) {
        app_stop_mode(manager);
    } else if (manager->state == APP_STATE_FAULT) {
        app_clear_fault(manager);
    } else if (manager->state == APP_STATE_IDLE) {
        app_start_selected_mode(manager);
    }
}

static void app_handle_command(AppModeManager_t *manager, char *cmd)
{
    if ((manager == 0) || (cmd == 0)) {
        return;
    }

    if (strcmp(cmd, "#RUN") == 0) {
        if (manager->state == APP_STATE_IDLE) {
            app_start_selected_mode(manager);
        }
        DrvTelemetry_SendText("OK RUN\r\n");
        return;
    }

    if (strcmp(cmd, "#STOP") == 0) {
        if (manager->state == APP_STATE_RUNNING) {
            app_stop_mode(manager);
        } else if (manager->state == APP_STATE_FAULT) {
            app_clear_fault(manager);
        }
        DrvTelemetry_SendText("OK STOP\r\n");
        return;
    }

    if (strcmp(cmd, "#STATUS") == 0) {
        app_send_status(manager);
        return;
    }

    if (strcmp(cmd, "#RSTYAW") == 0) {
        AppStraightController_LockHeading(&manager->straight, &manager->snapshot);
        DrvImuBno08x_ResetAttitude(&manager->snapshot.imu);
        DrvTelemetry_SendText("OK YAW\r\n");
        return;
    }

    if (strncmp(cmd, "#MODE=", 6) == 0) {
        if (manager->state == APP_STATE_IDLE) {
            if (strstr(cmd, "TRACK") != 0) {
                manager->selectedMode = APP_MODE_TRACK;
            } else {
                manager->selectedMode = APP_MODE_STRAIGHT;
            }
            DrvTelemetry_SendText("OK MODE\r\n");
        } else {
            DrvTelemetry_SendText("ERR BUSY\r\n");
        }
        return;
    }

    if (strncmp(cmd, "#TS=", 4) == 0) {
        float value = app_parse_value(cmd);
        if (manager->selectedMode == APP_MODE_TRACK) {
            AppTrackController_SetTargetSpeed(&manager->track, value);
        } else {
            AppStraightController_SetTargetSpeed(&manager->straight, value);
        }
        DrvTelemetry_SendText("OK TS\r\n");
        return;
    }

    if (strncmp(cmd, "#HKP=", 5) == 0) {
        AppStraightController_SetHeadingPid(&manager->straight, app_parse_value(cmd), manager->straight.cfg.headingKi, manager->straight.cfg.headingKd);
        DrvTelemetry_SendText("OK HKP\r\n");
        return;
    }
    if (strncmp(cmd, "#HKI=", 5) == 0) {
        AppStraightController_SetHeadingPid(&manager->straight, manager->straight.cfg.headingKp, app_parse_value(cmd), manager->straight.cfg.headingKd);
        DrvTelemetry_SendText("OK HKI\r\n");
        return;
    }
    if (strncmp(cmd, "#HKD=", 5) == 0) {
        AppStraightController_SetHeadingPid(&manager->straight, manager->straight.cfg.headingKp, manager->straight.cfg.headingKi, app_parse_value(cmd));
        DrvTelemetry_SendText("OK HKD\r\n");
        return;
    }

    if (strncmp(cmd, "#LKP=", 5) == 0) {
        AppTrackController_SetLinePid(&manager->track, app_parse_value(cmd), manager->track.cfg.lineKi, manager->track.cfg.lineKd);
        DrvTelemetry_SendText("OK LKP\r\n");
        return;
    }
    if (strncmp(cmd, "#LKI=", 5) == 0) {
        AppTrackController_SetLinePid(&manager->track, manager->track.cfg.lineKp, app_parse_value(cmd), manager->track.cfg.lineKd);
        DrvTelemetry_SendText("OK LKI\r\n");
        return;
    }
    if (strncmp(cmd, "#LKD=", 5) == 0) {
        AppTrackController_SetLinePid(&manager->track, manager->track.cfg.lineKp, manager->track.cfg.lineKi, app_parse_value(cmd));
        DrvTelemetry_SendText("OK LKD\r\n");
        return;
    }

    if (strncmp(cmd, "#SKP=", 5) == 0) {
        float value = app_parse_value(cmd);
        AppStraightController_SetSpeedPid(&manager->straight, value, manager->straight.cfg.speedKi, manager->straight.cfg.speedKd);
        AppTrackController_SetSpeedPid(&manager->track, value, manager->track.cfg.speedKi, manager->track.cfg.speedKd);
        DrvTelemetry_SendText("OK SKP\r\n");
        return;
    }
    if (strncmp(cmd, "#SKI=", 5) == 0) {
        float value = app_parse_value(cmd);
        AppStraightController_SetSpeedPid(&manager->straight, manager->straight.cfg.speedKp, value, manager->straight.cfg.speedKd);
        AppTrackController_SetSpeedPid(&manager->track, manager->track.cfg.speedKp, value, manager->track.cfg.speedKd);
        DrvTelemetry_SendText("OK SKI\r\n");
        return;
    }
    if (strncmp(cmd, "#SKD=", 5) == 0) {
        float value = app_parse_value(cmd);
        AppStraightController_SetSpeedPid(&manager->straight, manager->straight.cfg.speedKp, manager->straight.cfg.speedKi, value);
        AppTrackController_SetSpeedPid(&manager->track, manager->track.cfg.speedKp, manager->track.cfg.speedKi, value);
        DrvTelemetry_SendText("OK SKD\r\n");
        return;
    }

    DrvTelemetry_SendText("ERR CMD\r\n");
}

static void app_poll_commands(AppModeManager_t *manager)
{
    uint8_t i;
    char cmd[64];

    for (i = 0u; i < 8u; i++) {
        if (DrvTelemetry_TryReadCommand(cmd, sizeof(cmd)) == 0u) {
            break;
        }
        app_handle_command(manager, cmd);
    }
}

void AppModeManager_Init(AppModeManager_t *manager)
{
    memset(manager, 0, sizeof(*manager));

    manager->selectedMode = APP_MODE_STRAIGHT;
    manager->activeMode = APP_MODE_STRAIGHT;
    manager->state = APP_STATE_IDLE;
    manager->faultCode = APP_FAULT_NONE;

    DrvDisplay_ShowLine(4u, "BOOT=TEL");
    DrvTelemetry_Init();

    DrvDisplay_ShowLine(4u, "BOOT=KEY");
    DrvKey_Init();

    DrvDisplay_ShowLine(4u, "BOOT=MOTOR");
    DrvMotor_Init();

    DrvDisplay_ShowLine(4u, "BOOT=ENC");
    DrvEncoder_Init();

    DrvDisplay_ShowLine(4u, "BOOT=TRACK");
    DrvTrackSensor_Init();

    DrvDisplay_ShowLine(4u, "BOOT=IMU");
    manager->imuReady = DrvImuBno08x_Init();

    AppStraightController_Init(&manager->straight);
    AppTrackController_Init(&manager->track);

    DrvMotor_Stop();
    DrvMotor_SetEnabled(0u);
    app_show_status(manager);
}

void AppModeManager_Process(AppModeManager_t *manager)
{
    while (BspControlTick_ConsumeOneTick() != 0u) {
        manager->tickMs++;

        if ((manager->tickMs % APP_CONTROL_PERIOD_MS) == 0u) {
            app_control_step(manager);
        }
        if ((manager->tickMs % APP_TELEMETRY_PERIOD_MS) == 0u) {
            app_send_status(manager);
        }
        if ((manager->tickMs % APP_DISPLAY_PERIOD_MS) == 0u) {
            app_show_status(manager);
        }
    }

    app_poll_commands(manager);
    app_handle_key(manager);
}
