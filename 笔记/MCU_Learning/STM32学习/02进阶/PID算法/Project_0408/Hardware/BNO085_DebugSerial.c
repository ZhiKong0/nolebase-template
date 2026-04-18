#include "BNO085_DebugSerial.h"
#include "VOFA.h"
#include <stdio.h>

#define BNO_DEBUG_SEND_PERIOD_RUN_MS   400u
#define BNO_DEBUG_SEND_PERIOD_IDLE_MS 1000u

extern volatile uint32_t g_icmReadOkCount;
extern volatile uint32_t g_icmReadFailCount;

void BNO085_DebugSerial_Send(uint32_t tickCount, uint8_t isRunning, const BNO085_Data_t *data)
{
    static char out[384];
    static uint32_t s_lastSendTick = 0u;
    const BNO085_Data_t *p = data;
    uint32_t periodMs;

    if (p == 0) {
        return;
    }

    periodMs = isRunning ? BNO_DEBUG_SEND_PERIOD_RUN_MS : BNO_DEBUG_SEND_PERIOD_IDLE_MS;
    if (s_lastSendTick != 0u && tickCount >= s_lastSendTick) {
        if ((tickCount - s_lastSendTick) < periodMs) {
            return;
        }
    }
    s_lastSendTick = tickCount;

    snprintf(out, sizeof(out),
             "BNO tick=%lu run=%u y=%.3f yr=%.3f q0=%.5f q1=%.5f q2=%.5f q3=%.5f who=0x%02X addr=0x%02X paddr=0x%02X st=%u pwho=0x%02X sfa=0x%02X sla=0x%02X shc=%u ch=%u rid=0x%02X len=%u txch=%u txlen=%u wfail=%u scl=%u sda=%u rst=%u int=%u iok=%lu if=%lu rx=%lu txd=%lu\r\n",
             (unsigned long)tickCount,
             (unsigned)isRunning,
             (double)p->yaw,
             (double)p->yawRate,
             (double)p->q0,
             (double)p->q1,
             (double)p->q2,
             (double)p->q3,
             (unsigned)BNO085_GetWhoAmI(),
             (unsigned)BNO085_GetI2CAddr(),
             (unsigned)BNO085_GetLastProbeAddr(),
             (unsigned)BNO085_GetInitStage(),
             (unsigned)BNO085_GetProbeWhoAmI(),
             (unsigned)BNO085_GetScanFirstAddr(),
             (unsigned)BNO085_GetScanLastAddr(),
             (unsigned)BNO085_GetScanHitCount(),
             (unsigned)BNO085_GetLastChannel(),
             (unsigned)BNO085_GetLastReportId(),
             (unsigned)BNO085_GetLastPayloadLen(),
             (unsigned)BNO085_GetLastTxChannel(),
             (unsigned)BNO085_GetLastTxPacketLen(),
             (unsigned)BNO085_GetLastWriteFailIndex(),
             (unsigned)BNO085_GetSclLevel(),
             (unsigned)BNO085_GetSdaLevel(),
             (unsigned)BNO085_GetResetLevel(),
             (unsigned)BNO085_GetIntLevel(),
             (unsigned long)g_icmReadOkCount,
             (unsigned long)g_icmReadFailCount,
             (unsigned long)VOFA_GetRxByteCount(),
             (unsigned long)VOFA_GetTxDropByteCount());
    VOFA_SendString(out);
}
