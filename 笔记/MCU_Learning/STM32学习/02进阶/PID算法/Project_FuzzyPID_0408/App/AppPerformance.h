#ifndef __APP_PERFORMANCE_H
#define __APP_PERFORMANCE_H

#include "AppTypes.h"

typedef struct {
    AppPerformance_t stats;
    float settleThresholdDeg;
    float settleHoldSeconds;
    float withinBandSeconds;
    float elapsedSeconds;
    int8_t lastErrorSign;
    uint8_t zeroCrossed;
} AppPerformanceMonitor_t;

void AppPerformanceMonitor_Init(AppPerformanceMonitor_t *monitor);
void AppPerformanceMonitor_Reset(AppPerformanceMonitor_t *monitor);
void AppPerformanceMonitor_Update(AppPerformanceMonitor_t *monitor,
                                  float errorDeg,
                                  float controlEffort,
                                  float dtSeconds);
const AppPerformance_t *AppPerformanceMonitor_Get(const AppPerformanceMonitor_t *monitor);

#endif
