#include "AppProtocol.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *app_protocol_skip_space(const char *text)
{
    while ((text != 0) && ((*text == ' ') || (*text == '\r') || (*text == '\n') || (*text == '\t'))) {
        text++;
    }
    return text;
}

static void app_protocol_strip_frame(const char *frame, char *out, uint16_t outSize)
{
    const char *start;
    const char *end;
    uint16_t length;

    if ((frame == 0) || (out == 0) || (outSize == 0u)) {
        return;
    }

    start = frame;
    start = app_protocol_skip_space(start);
    if (*start == '#') {
        start++;
    }
    start = app_protocol_skip_space(start);

    end = start + strlen(start);
    while (end > start) {
        if ((end[-1] == '!') || (end[-1] == '\r') || (end[-1] == '\n') || (end[-1] == ' ') || (end[-1] == '\t')) {
            end--;
        } else {
            break;
        }
    }

    length = (uint16_t)(end - start);
    if (length >= outSize) {
        length = (uint16_t)(outSize - 1u);
    }

    memcpy(out, start, length);
    out[length] = '\0';
}

static uint8_t app_protocol_extract_string(const char *json, const char *key, char *out, uint16_t outSize)
{
    const char *pos;
    const char *valueStart;
    const char *valueEnd;
    uint16_t length;

    if ((json == 0) || (key == 0) || (out == 0) || (outSize == 0u)) {
        return 0u;
    }

    pos = strstr(json, key);
    if (pos == 0) {
        return 0u;
    }

    pos = strchr(pos, ':');
    if (pos == 0) {
        return 0u;
    }
    pos++;
    pos = app_protocol_skip_space(pos);
    if (*pos != '\"') {
        return 0u;
    }

    valueStart = pos + 1;
    valueEnd = strchr(valueStart, '\"');
    if (valueEnd == 0) {
        return 0u;
    }

    length = (uint16_t)(valueEnd - valueStart);
    if (length >= outSize) {
        length = (uint16_t)(outSize - 1u);
    }
    memcpy(out, valueStart, length);
    out[length] = '\0';
    return 1u;
}

static uint8_t app_protocol_extract_float(const char *json, const char *key, float *outValue)
{
    const char *pos;
    char *endPtr;

    if ((json == 0) || (key == 0) || (outValue == 0)) {
        return 0u;
    }

    pos = strstr(json, key);
    if (pos == 0) {
        return 0u;
    }

    pos = strchr(pos, ':');
    if (pos == 0) {
        return 0u;
    }
    pos++;
    pos = app_protocol_skip_space(pos);

    *outValue = strtof(pos, &endPtr);
    if (endPtr == pos) {
        return 0u;
    }
    return 1u;
}

static uint16_t app_protocol_parse_number_sequence(const char *start, float *values, uint16_t maxCount)
{
    uint16_t count = 0u;
    const char *cursor = start;

    while ((*cursor != '\0') && (count < maxCount)) {
        if (((*cursor >= '0') && (*cursor <= '9')) || (*cursor == '-') || (*cursor == '+')) {
            char *endPtr;
            values[count] = strtof(cursor, &endPtr);
            if (endPtr == cursor) {
                cursor++;
            } else {
                count++;
                cursor = endPtr;
            }
        } else if (*cursor == ']') {
            break;
        } else {
            cursor++;
        }
    }

    return count;
}

static uint8_t app_protocol_extract_array(const char *json, const char *key, float *values, uint16_t requiredCount)
{
    const char *pos;
    const char *arrayStart;
    uint16_t count;

    pos = strstr(json, key);
    if (pos == 0) {
        return 0u;
    }

    arrayStart = strchr(pos, '[');
    if (arrayStart == 0) {
        return 0u;
    }

    count = app_protocol_parse_number_sequence(arrayStart, values, requiredCount);
    return (count >= requiredCount) ? 1u : 0u;
}

static AppRunMode_t app_protocol_decode_run_mode(const char *value)
{
    if ((value != 0) && (strcmp(value, "track") == 0)) {
        return APP_RUN_MODE_TRACK;
    }
    return APP_RUN_MODE_STRAIGHT;
}

static AppTuneMode_t app_protocol_decode_tune_mode(const char *value)
{
    if ((value != 0) && (strcmp(value, "fuzzy") == 0)) {
        return APP_TUNE_MODE_FUZZY;
    }
    if ((value != 0) && (strcmp(value, "learning") == 0)) {
        return APP_TUNE_MODE_LEARNING;
    }
    return APP_TUNE_MODE_FIXED;
}

static const char *app_protocol_run_mode_name(AppRunMode_t mode)
{
    return (mode == APP_RUN_MODE_TRACK) ? "track" : "straight";
}

static const char *app_protocol_tune_mode_name(AppTuneMode_t mode)
{
    if (mode == APP_TUNE_MODE_FUZZY) {
        return "fuzzy";
    }
    if (mode == APP_TUNE_MODE_LEARNING) {
        return "learning";
    }
    return "fixed";
}

static const char *app_protocol_state_name(AppState_t state)
{
    switch (state) {
        case APP_STATE_IDLE: return "idle";
        case APP_STATE_ARMING: return "arming";
        case APP_STATE_RUNNING: return "running";
        case APP_STATE_STOPPING: return "stopping";
        case APP_STATE_FAULT: return "fault";
        default: return "unknown";
    }
}

static const char *app_protocol_reset_cause_name(uint8_t cause)
{
    switch (cause) {
        case 1u: return "wwdg";
        case 2u: return "iwdg";
        case 3u: return "soft";
        case 4u: return "power";
        case 5u: return "pin";
        case 6u: return "lpwr";
        default: return "unknown";
    }
}

static const char *app_protocol_fault_trace_name(uint16_t code)
{
    switch (code) {
        case 1u: return "hard";
        case 2u: return "mem";
        case 3u: return "bus";
        case 4u: return "usage";
        default: return "none";
    }
}

static const char *app_protocol_fault_addr_source_name(uint16_t code)
{
    switch (code) {
        case 2u: return "mmfar";
        case 3u: return "bfar";
        default: return "none";
    }
}

void AppProtocol_ClearCommand(AppProtocolCommand_t *command)
{
    if (command == 0) {
        return;
    }

    memset(command, 0, sizeof(*command));
}

uint8_t AppProtocol_ParseCommand(const char *frame, AppProtocolCommand_t *command)
{
    static char payload[1536];
    char cmdName[32];
    char value[32];
    static float rules[APP_RULE_COUNT * 3u];
    static float axisValues[APP_FUZZY_SET_COUNT];
    uint8_t i;

    if ((frame == 0) || (command == 0)) {
        return 0u;
    }

    AppProtocol_ClearCommand(command);
    app_protocol_strip_frame(frame, payload, sizeof(payload));
    if (payload[0] == '\0') {
        return 0u;
    }

    if (payload[0] != '{') {
        if (strcmp(payload, "RUN") == 0) {
            command->requestStart = 1u;
            return 1u;
        }
        if (strcmp(payload, "STOP") == 0) {
            command->requestStop = 1u;
            return 1u;
        }
        if (strcmp(payload, "STATUS") == 0) {
            command->requestStatus = 1u;
            return 1u;
        }
        return 0u;
    }

    if (app_protocol_extract_string(payload, "\"cmd\"", cmdName, sizeof(cmdName)) == 0u) {
        cmdName[0] = '\0';
    }

    if (strcmp(cmdName, "start") == 0) {
        command->requestStart = 1u;
    } else if (strcmp(cmdName, "stop") == 0) {
        command->requestStop = 1u;
    } else if (strcmp(cmdName, "status") == 0) {
        command->requestStatus = 1u;
    } else if (strcmp(cmdName, "clear_fault") == 0) {
        command->requestClearFault = 1u;
    } else if (strcmp(cmdName, "save_profile") == 0) {
        command->requestSaveProfile = 1u;
    } else if (strcmp(cmdName, "load_profile") == 0) {
        command->requestLoadProfile = 1u;
    }

    if (app_protocol_extract_string(payload, "\"mode\"", value, sizeof(value)) != 0u) {
        command->hasRunMode = 1u;
        command->runMode = app_protocol_decode_run_mode(value);
    }

    if (app_protocol_extract_string(payload, "\"tune\"", value, sizeof(value)) != 0u) {
        command->hasTuneMode = 1u;
        command->tuneMode = app_protocol_decode_tune_mode(value);
    }

    if (app_protocol_extract_float(payload, "\"speed\"", &command->targetSpeedRpm) != 0u) {
        command->hasTargetSpeed = 1u;
    }
    if (app_protocol_extract_float(payload, "\"heading\"", &command->targetHeadingDeg) != 0u) {
        command->hasTargetHeading = 1u;
    } else if (app_protocol_extract_float(payload, "\"trim\"", &command->targetHeadingDeg) != 0u) {
        command->hasTargetHeading = 1u;
    }

    if ((strcmp(cmdName, "update_heading_pid") == 0) ||
        ((app_protocol_extract_float(payload, "\"kp\"", &command->headingPid.kp) != 0u) &&
         (app_protocol_extract_float(payload, "\"ki\"", &command->headingPid.ki) != 0u) &&
         (app_protocol_extract_float(payload, "\"kd\"", &command->headingPid.kd) != 0u) &&
         (strstr(payload, "\"heading_pid\"") != 0))) {
        if (app_protocol_extract_float(payload, "\"kp\"", &command->headingPid.kp) != 0u &&
            app_protocol_extract_float(payload, "\"ki\"", &command->headingPid.ki) != 0u &&
            app_protocol_extract_float(payload, "\"kd\"", &command->headingPid.kd) != 0u) {
            command->hasHeadingPid = 1u;
        }
    }

    if (strcmp(cmdName, "update_speed_pid") == 0) {
        if (app_protocol_extract_float(payload, "\"kp\"", &command->speedPid.kp) != 0u &&
            app_protocol_extract_float(payload, "\"ki\"", &command->speedPid.ki) != 0u &&
            app_protocol_extract_float(payload, "\"kd\"", &command->speedPid.kd) != 0u) {
            command->hasSpeedPid = 1u;
        }
    }

    if (strcmp(cmdName, "update_rules") == 0) {
        if (app_protocol_extract_array(payload, "\"rules\"", rules, (uint16_t)(APP_RULE_COUNT * 3u)) != 0u) {
            for (i = 0u; i < APP_RULE_COUNT; i++) {
                command->rules[i].kpMilli = (uint16_t)(rules[i * 3u + 0u] * 1000.0f + 0.5f);
                command->rules[i].kiMilli = (uint16_t)(rules[i * 3u + 1u] * 1000.0f + 0.5f);
                command->rules[i].kdMilli = (uint16_t)(rules[i * 3u + 2u] * 1000.0f + 0.5f);
            }
            command->hasRules = 1u;
        }

        if (app_protocol_extract_array(payload, "\"e_centers\"", axisValues, APP_FUZZY_SET_COUNT) != 0u) {
            for (i = 0u; i < APP_FUZZY_SET_COUNT; i++) {
                command->errorAxis.centers[i] = (int16_t)axisValues[i];
            }
            command->hasErrorAxis = 1u;
        }
        if (app_protocol_extract_array(payload, "\"e_sigmas\"", axisValues, APP_FUZZY_SET_COUNT) != 0u) {
            for (i = 0u; i < APP_FUZZY_SET_COUNT; i++) {
                command->errorAxis.sigmas[i] = (int16_t)axisValues[i];
            }
            command->hasErrorAxis = 1u;
        }
        if (app_protocol_extract_array(payload, "\"ec_centers\"", axisValues, APP_FUZZY_SET_COUNT) != 0u) {
            for (i = 0u; i < APP_FUZZY_SET_COUNT; i++) {
                command->errorRateAxis.centers[i] = (int16_t)axisValues[i];
            }
            command->hasRateAxis = 1u;
        }
        if (app_protocol_extract_array(payload, "\"ec_sigmas\"", axisValues, APP_FUZZY_SET_COUNT) != 0u) {
            for (i = 0u; i < APP_FUZZY_SET_COUNT; i++) {
                command->errorRateAxis.sigmas[i] = (int16_t)axisValues[i];
            }
            command->hasRateAxis = 1u;
        }
    }

    return 1u;
}

void AppProtocol_BuildTelemetry(const AppTelemetryFrame_t *telemetry, char *out, uint16_t outSize)
{
    float timeSeconds;
    float pwmLeftNorm;
    float pwmRightNorm;
    float rawLeftNorm;
    float rawRightNorm;
    float baseNorm;
    float steerNorm;

    if ((telemetry == 0) || (out == 0) || (outSize == 0u)) {
        return;
    }

    timeSeconds = (float)telemetry->snapshot.timeMs / 1000.0f;
    pwmLeftNorm = (float)telemetry->command.leftPwm / 100.0f;
    pwmRightNorm = (float)telemetry->command.rightPwm / 100.0f;
    rawLeftNorm = telemetry->command.rawLeftPwm / 100.0f;
    rawRightNorm = telemetry->command.rawRightPwm / 100.0f;
    baseNorm = telemetry->command.basePwm / 100.0f;
    steerNorm = telemetry->command.steeringPwm / 100.0f;

    snprintf(out,
             outSize,
             "{\"time\":%.3f,"
             "\"mode\":\"%s\","
             "\"tune\":\"%s\","
             "\"state\":\"%s\","
             "\"boot\":{\"count\":%u,\"reset\":\"%s\",\"fault\":\"%s\",\"fault_code\":%u,"
             "\"fault_addr_src\":\"%s\",\"fault_addr\":\"0x%08lX\","
             "\"cfsr\":\"0x%08lX\",\"hfsr\":\"0x%08lX\"},"
             "\"sensors\":{\"heading\":%.2f,\"v_left\":%.2f,\"v_right\":%.2f,"
             "\"vf_left\":%.2f,\"vf_right\":%.2f,\"vf_avg\":%.2f,\"v_forward\":%.2f},"
             "\"encoder\":{\"dl\":%d,\"dr\":%d,\"cl_l\":%u,\"cl_r\":%u},"
             "\"control\":{\"pwm_l\":%.2f,\"pwm_r\":%.2f,\"raw_l\":%.2f,\"raw_r\":%.2f,"
             "\"base\":%.2f,\"steer\":%.2f},"
             "\"errors\":{\"e\":%.2f,\"ec\":%.2f,\"ei\":%.2f},"
             "\"gains\":{\"heading\":{\"kp\":%.4f,\"ki\":%.4f,\"kd\":%.4f},"
             "\"speed\":{\"kp\":%.4f,\"ki\":%.4f,\"kd\":%.4f}},"
             "\"config\":{\"target_speed\":%.2f,\"trim\":%.2f},"
             "\"performance\":{\"ise\":%.3f,\"window_ise\":%.3f,\"overshoot\":%.2f,\"settling\":%.2f},"
             "\"flags\":{\"retrain\":%u,\"profile_stored\":%u,\"profile_dirty\":%u}}\r\n",
             timeSeconds,
             app_protocol_run_mode_name(telemetry->runMode),
             app_protocol_tune_mode_name(telemetry->tuneMode),
             app_protocol_state_name(telemetry->state),
             (unsigned)telemetry->bootCount,
             app_protocol_reset_cause_name(telemetry->resetCauseCode),
             app_protocol_fault_trace_name(telemetry->faultTraceCode),
             (unsigned)telemetry->faultTraceCode,
             app_protocol_fault_addr_source_name(telemetry->faultTraceCode),
             (unsigned long)telemetry->faultAddr,
             (unsigned long)telemetry->faultCfsr,
             (unsigned long)telemetry->faultHfsr,
             telemetry->snapshot.headingDeg,
             telemetry->snapshot.leftSpeedRpm,
             telemetry->snapshot.rightSpeedRpm,
             telemetry->filteredLeftSpeedRpm,
             telemetry->filteredRightSpeedRpm,
             telemetry->filteredAvgSpeedRpm,
             telemetry->forwardSpeedEstimateRpm,
             (int)telemetry->snapshot.encoder.rawLeftDelta,
             (int)telemetry->snapshot.encoder.rawRightDelta,
             (unsigned)telemetry->snapshot.encoder.leftDeltaClamped,
             (unsigned)telemetry->snapshot.encoder.rightDeltaClamped,
             pwmLeftNorm,
             pwmRightNorm,
             rawLeftNorm,
             rawRightNorm,
             baseNorm,
             steerNorm,
             telemetry->command.headingErrorDeg,
             telemetry->command.headingRateErrorDegPerSec,
             telemetry->headingIntegral,
             telemetry->headingGains.kp,
             telemetry->headingGains.ki,
             telemetry->headingGains.kd,
             telemetry->speedGains.kp,
             telemetry->speedGains.ki,
             telemetry->speedGains.kd,
             telemetry->configuredTargetSpeedRpm,
             telemetry->headingTrimDeg,
             telemetry->performance.ise,
             telemetry->performance.windowIse,
             telemetry->performance.overshoot,
             telemetry->performance.settlingTime,
             (unsigned)telemetry->retrainRequested,
             (unsigned)telemetry->profileStored,
             (unsigned)telemetry->profileDirty);
}
