#include "AppPerformance.h"

static float app_perf_abs(float value)
{
    return (value >= 0.0f) ? value : -value;
}

void AppPerformanceMonitor_Init(AppPerformanceMonitor_t *monitor)
{
    if (monitor == 0) {
        return;
    }

    monitor->settleThresholdDeg = 2.0f;
    monitor->settleHoldSeconds = 0.50f;
    AppPerformanceMonitor_Reset(monitor);
}

void AppPerformanceMonitor_Reset(AppPerformanceMonitor_t *monitor)
{
    if (monitor == 0) {
        return;
    }

    monitor->stats.ise = 0.0f;
    monitor->stats.windowIse = 0.0f;
    monitor->stats.overshoot = 0.0f;
    monitor->stats.settlingTime = 0.0f;
    monitor->stats.absPeakError = 0.0f;
    monitor->stats.settlingReached = 0u;
    monitor->stats.degraded = 0u;
    monitor->withinBandSeconds = 0.0f;
    monitor->elapsedSeconds = 0.0f;
    monitor->lastErrorSign = 0;
    monitor->zeroCrossed = 0u;
}

void AppPerformanceMonitor_Update(AppPerformanceMonitor_t *monitor,
                                  float errorDeg,
                                  float controlEffort,
                                  float dtSeconds)
{
    float absError;
    int8_t sign = 0;

    (void)controlEffort;

    if ((monitor == 0) || (dtSeconds <= 0.0f)) {
        return;
    }

    absError = app_perf_abs(errorDeg);
    monitor->elapsedSeconds += dtSeconds;
    monitor->stats.ise += errorDeg * errorDeg * dtSeconds;
    monitor->stats.windowIse = monitor->stats.windowIse * 0.92f + errorDeg * errorDeg * dtSeconds * 0.08f;

    if (absError > monitor->stats.absPeakError) {
        monitor->stats.absPeakError = absError;
    }

    if (errorDeg > 0.01f) {
        sign = 1;
    } else if (errorDeg < -0.01f) {
        sign = -1;
    }

    if ((monitor->lastErrorSign != 0) && (sign != 0) && (sign != monitor->lastErrorSign)) {
        monitor->zeroCrossed = 1u;
    }
    if (sign != 0) {
        monitor->lastErrorSign = sign;
    }

    if ((monitor->zeroCrossed != 0u) && (absError > monitor->stats.overshoot)) {
        monitor->stats.overshoot = absError;
    }

    if (absError <= monitor->settleThresholdDeg) {
        monitor->withinBandSeconds += dtSeconds;
        if ((monitor->stats.settlingReached == 0u) && (monitor->withinBandSeconds >= monitor->settleHoldSeconds)) {
            monitor->stats.settlingReached = 1u;
            monitor->stats.settlingTime = monitor->elapsedSeconds;
        }
    } else {
        monitor->withinBandSeconds = 0.0f;
    }
}

const AppPerformance_t *AppPerformanceMonitor_Get(const AppPerformanceMonitor_t *monitor)
{
    if (monitor == 0) {
        return 0;
    }
    return &monitor->stats;
}
