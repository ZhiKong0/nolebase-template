#ifndef __APP_CONFIG_PROFILE_H
#define __APP_CONFIG_PROFILE_H

#include "AppRuntime.h"

uint8_t AppConfigProfile_LoadStartup(AppRuntimeContext_t *runtime);
uint8_t AppConfigProfile_LoadSaved(AppRuntimeContext_t *runtime);
uint8_t AppConfigProfile_SaveCurrent(AppRuntimeContext_t *runtime);

#endif
