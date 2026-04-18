#ifndef __DRV_TRACK_SENSOR_H
#define __DRV_TRACK_SENSOR_H

#include "stm32f10x.h"

typedef struct {
    uint8_t activeMask;
    uint8_t activeCount;
    uint8_t hasLine;
    uint16_t lostFrames;
    int8_t lastDirection;
    float rawPosition;
} DrvTrackSensorSample_t;

void DrvTrackSensor_Init(void);
void DrvTrackSensor_Sample(DrvTrackSensorSample_t *sample);

#endif
