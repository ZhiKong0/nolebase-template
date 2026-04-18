#ifndef __APP_TELEMETRY_SERVICE_H
#define __APP_TELEMETRY_SERVICE_H

#include "AppRuntime.h"

void AppTelemetryService_Init(void);
void AppTelemetryService_SendAck(const char *item, const char *status);
void AppTelemetryService_SendStatus(AppRuntimeContext_t *runtime);

#endif
