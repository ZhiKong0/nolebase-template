#ifndef __APP_UI_SERVICE_H
#define __APP_UI_SERVICE_H

#include "AppRuntime.h"

void AppUiService_Init(void);
void AppUiService_HandleKey(AppRuntimeContext_t *runtime);
void AppUiService_Render(const AppRuntimeContext_t *runtime);

#endif
