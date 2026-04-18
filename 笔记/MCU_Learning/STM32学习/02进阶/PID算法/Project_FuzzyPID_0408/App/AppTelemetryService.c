#include "AppTelemetryService.h"
#include "DrvTelemetry.h"
#include "BspFaultTrace.h"
#include <stdio.h>

static char g_appAckBuffer[APP_PROTOCOL_ACK_TEXT_MAX];
static char g_appStatusBuffer[APP_PROTOCOL_TELEMETRY_TEXT_MAX];

static void app_telemetry_fill_frame(AppRuntimeContext_t *runtime)
{
    runtime->telemetry.runMode = runtime->runMode;
    runtime->telemetry.tuneMode = runtime->learning.tuneMode;
    runtime->telemetry.state = runtime->state;
    runtime->telemetry.bootCount = BspBootTrace_GetCount();
    runtime->telemetry.faultTraceCode = BspBootTrace_GetFaultCode();
    runtime->telemetry.resetCauseCode = BspBootTrace_GetResetCauseCode();
    runtime->telemetry.faultCfsr = BspBootTrace_GetFaultCfsr();
    runtime->telemetry.faultHfsr = BspBootTrace_GetFaultHfsr();
    runtime->telemetry.faultAddr = BspBootTrace_GetFaultAddr();
    runtime->telemetry.snapshot = runtime->snapshot;
    runtime->telemetry.command = runtime->motorCommand;
    runtime->telemetry.performance = *AppPerformanceMonitor_Get(&runtime->performance);
    runtime->telemetry.headingGains = runtime->appliedHeadingGains;
    runtime->telemetry.speedGains = runtime->speedGains;
    runtime->telemetry.filteredLeftSpeedRpm = runtime->filteredLeftSpeedRpm;
    runtime->telemetry.filteredRightSpeedRpm = runtime->filteredRightSpeedRpm;
    runtime->telemetry.filteredAvgSpeedRpm = runtime->filteredAvgSpeedRpm;
    runtime->telemetry.forwardSpeedEstimateRpm = runtime->forwardSpeedEstimateRpm;
    runtime->telemetry.configuredTargetSpeedRpm = runtime->baseTargetSpeedRpm;
    runtime->telemetry.headingTrimDeg = runtime->headingTrimDeg;
    runtime->telemetry.headingIntegral = runtime->headingPid.integral;
    runtime->telemetry.retrainRequested = runtime->learning.retrainRequested;
    runtime->telemetry.profileStored = runtime->profileStored;
    runtime->telemetry.profileDirty = runtime->profileDirty;
}

void AppTelemetryService_Init(void)
{
    DrvTelemetry_Init();
}

void AppTelemetryService_SendAck(const char *item, const char *status)
{
    snprintf(g_appAckBuffer, sizeof(g_appAckBuffer), "{\"ack\":\"%s\",\"item\":\"%s\"}\r\n", status, item);
    DrvTelemetry_SendText(g_appAckBuffer);
}

void AppTelemetryService_SendStatus(AppRuntimeContext_t *runtime)
{
    if (runtime == 0) {
        return;
    }

    app_telemetry_fill_frame(runtime);
    AppProtocol_BuildTelemetry(&runtime->telemetry, g_appStatusBuffer, sizeof(g_appStatusBuffer));
    DrvTelemetry_SendText(g_appStatusBuffer);
}
