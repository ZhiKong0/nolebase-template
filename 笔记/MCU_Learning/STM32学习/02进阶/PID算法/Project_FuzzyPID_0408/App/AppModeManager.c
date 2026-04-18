#include "AppModeManager.h"
#include "BspControlTick.h"
#include "AppRuntime.h"
#include "AppConfigProfile.h"
#include "AppMotionSupervisor.h"
#include "AppTelemetryService.h"
#include "AppCommandDispatcher.h"
#include "AppUiService.h"

static AppRuntimeContext_t g_runtime;

void AppModeManager_Init(void)
{
    AppTelemetryService_Init();
    AppUiService_Init();
    AppMotionSupervisor_Init(&g_runtime);
    if (AppConfigProfile_LoadStartup(&g_runtime) != 0u) {
        AppTelemetryService_SendAck("profile", "loaded");
    } else {
        AppTelemetryService_SendAck("profile", "default");
    }
    AppUiService_Render(&g_runtime);
    AppTelemetryService_SendAck("boot", (g_runtime.snapshot.imuValid != 0u) ? "ok" : "imu_warn");
}

void AppModeManager_Process(void)
{
    while (BspControlTick_ConsumeOneTick() != 0u) {
        g_runtime.tickMs++;
        AppUiService_HandleKey(&g_runtime);
        AppCommandDispatcher_Poll(&g_runtime);

        if ((g_runtime.tickMs - g_runtime.lastControlMs) >= APP_CONTROL_PERIOD_MS) {
            g_runtime.lastControlMs += APP_CONTROL_PERIOD_MS;
            AppMotionSupervisor_ControlStep(&g_runtime);
        }

        if ((g_runtime.tickMs - g_runtime.lastFuzzyMs) >= APP_FUZZY_PERIOD_MS) {
            g_runtime.lastFuzzyMs += APP_FUZZY_PERIOD_MS;
            AppMotionSupervisor_UpdateFuzzy(&g_runtime);
        }

        if ((g_runtime.tickMs - g_runtime.lastTelemetryMs) >= APP_TELEMETRY_PERIOD_MS) {
            g_runtime.lastTelemetryMs += APP_TELEMETRY_PERIOD_MS;
            AppTelemetryService_SendStatus(&g_runtime);
        }

        if ((g_runtime.tickMs - g_runtime.lastDisplayMs) >= APP_DISPLAY_PERIOD_MS) {
            g_runtime.lastDisplayMs += APP_DISPLAY_PERIOD_MS;
            AppUiService_Render(&g_runtime);
        }
    }
}
