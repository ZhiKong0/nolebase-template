#ifndef __APP_PROTOCOL_H
#define __APP_PROTOCOL_H

#include "AppTypes.h"

#define APP_PROTOCOL_ACK_TEXT_MAX        96u
#define APP_PROTOCOL_TELEMETRY_TEXT_MAX  1152u

typedef struct {
    uint8_t requestStart;
    uint8_t requestStop;
    uint8_t requestStatus;
    uint8_t requestClearFault;
    uint8_t requestSaveProfile;
    uint8_t requestLoadProfile;
    uint8_t hasRunMode;
    AppRunMode_t runMode;
    uint8_t hasTuneMode;
    AppTuneMode_t tuneMode;
    uint8_t hasTargetSpeed;
    float targetSpeedRpm;
    uint8_t hasTargetHeading;
    float targetHeadingDeg;
    uint8_t hasHeadingPid;
    AppPidGains_t headingPid;
    uint8_t hasSpeedPid;
    AppPidGains_t speedPid;
    uint8_t hasRules;
    AppFuzzyRule_t rules[APP_RULE_COUNT];
    uint8_t hasErrorAxis;
    AppFuzzyAxisConfig_t errorAxis;
    uint8_t hasRateAxis;
    AppFuzzyAxisConfig_t errorRateAxis;
} AppProtocolCommand_t;

void AppProtocol_ClearCommand(AppProtocolCommand_t *command);
uint8_t AppProtocol_ParseCommand(const char *frame, AppProtocolCommand_t *command);
void AppProtocol_BuildTelemetry(const AppTelemetryFrame_t *telemetry, char *out, uint16_t outSize);

#endif
