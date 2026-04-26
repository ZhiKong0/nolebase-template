#include "bsp_uart.h"
#include "line_track.h"
#include <string.h>
#include <stdio.h>

/* ========== TX Ring Buffer ========== */

static volatile uint16_t txHead = 0;
static volatile uint16_t txTail = 0;
static uint8_t txBuffer[COMM_TX_BUF_SIZE];

/* ========== RX Command Parser ========== */

static char rxBuffer[COMM_RX_BUF_SIZE];
static volatile uint8_t rxIndex = 0;
static volatile uint8_t rxInFrame = 0;

static char cmdBuffer[COMM_RX_BUF_SIZE];
static volatile uint8_t cmdLength = 0;
static volatile uint8_t cmdReady = 0;

/* ---------- TX helpers ---------- */

static uint16_t tx_next(uint16_t v)
{
    v++;
    if (v >= (uint16_t)COMM_TX_BUF_SIZE) v = 0;
    return v;
}

static void tx_kick(void)
{
    USART_ITConfig(COMM_USART, USART_IT_TXE, ENABLE);
}

static void tx_enqueue(uint8_t b)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    {
        uint16_t next = tx_next(txHead);
        if (next != txTail) {
            txBuffer[txHead] = b;
            txHead = next;
            tx_kick();
        }
    }
    if (primask == 0u) __enable_irq();
}

/* ---------- RX helpers ---------- */

static void rx_publish(void)
{
    uint8_t len;
    if (rxIndex < 2u || rxBuffer[0] != '#' || rxBuffer[rxIndex - 1u] != '!') {
        rxIndex = 0;
        rxInFrame = 0;
        return;
    }
    if (cmdReady != 0u) {
        rxIndex = 0;
        rxInFrame = 0;
        return;
    }
    len = rxIndex;
    if (len >= COMM_RX_BUF_SIZE) len = (uint8_t)(COMM_RX_BUF_SIZE - 1u);
    memcpy(cmdBuffer, rxBuffer, len);
    cmdBuffer[len] = '\0';
    cmdLength = len;
    cmdReady = 1u;
    rxIndex = 0;
    rxInFrame = 0;
}

static void rx_feed_byte(uint8_t d)
{
    if (cmdReady != 0u) return;

    if (!rxInFrame) {
        if (d == '#') {
            rxIndex = 0;
            rxBuffer[rxIndex++] = (char)d;
            rxInFrame = 1;
        }
        return;
    }

    if (d == '!') {
        if (rxIndex < (COMM_RX_BUF_SIZE - 1u)) {
            rxBuffer[rxIndex++] = (char)d;
        }
        rx_publish();
    } else if (d == '#') {
        rxIndex = 0;
        rxBuffer[rxIndex++] = (char)d;
    } else {
        if (rxIndex < (COMM_RX_BUF_SIZE - 1u)) {
            rxBuffer[rxIndex++] = (char)d;
        } else {
            rxIndex = 0;
            rxInFrame = 0;
        }
    }
}

static void rx_poll(void)
{
    while (USART_GetFlagStatus(COMM_USART, USART_FLAG_RXNE) != RESET) {
        uint8_t d = (uint8_t)USART_ReceiveData(COMM_USART);
        rx_feed_byte(d);
    }
    if (USART_GetFlagStatus(COMM_USART, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(COMM_USART);
    }
}

/* ========== Public API ========== */

void BspUart_Init(void)
{
    GPIO_InitTypeDef g;
    USART_InitTypeDef u;
    NVIC_InitTypeDef n;

    RCC_APB2PeriphClockCmd(COMM_USART_GPIO_RCC, ENABLE);
#if COMM_USART_ON_APB2
    RCC_APB2PeriphClockCmd(COMM_USART_RCC, ENABLE);
#else
    RCC_APB1PeriphClockCmd(COMM_USART_RCC, ENABLE);
#endif
    GPIO_PinRemapConfig(COMM_USART_REMAP, ENABLE);

    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Pin = COMM_TX_PIN;
    g.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(COMM_TX_PORT, &g);

    g.GPIO_Pin = COMM_RX_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(COMM_RX_PORT, &g);

    u.USART_BaudRate = COMM_BAUDRATE;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    u.USART_Parity = USART_Parity_No;
    u.USART_StopBits = USART_StopBits_1;
    u.USART_WordLength = USART_WordLength_8b;
    USART_Init(COMM_USART, &u);

    USART_ITConfig(COMM_USART, USART_IT_RXNE, ENABLE);
    USART_ITConfig(COMM_USART, USART_IT_TXE, DISABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    n.NVIC_IRQChannel = COMM_USART_IRQn;
    n.NVIC_IRQChannelCmd = ENABLE;
    n.NVIC_IRQChannelPreemptionPriority = 1;
    n.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&n);

    USART_Cmd(COMM_USART, ENABLE);

    txHead = 0;
    txTail = 0;
    rxIndex = 0;
    rxInFrame = 0;
    cmdReady = 0;
    cmdLength = 0;
}

void BspUart_SendString(const char *s)
{
    if (!s) return;
    while (*s) {
        tx_enqueue((uint8_t)(*s++));
    }
}

void BspUart_SendBytes(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    if (!data || len == 0u) return;
    for (i = 0; i < len; i++) {
        tx_enqueue(data[i]);
    }
}

uint8_t BspUart_TakeCommand(char *out, uint8_t outSize)
{
    uint8_t i;
    uint32_t primask;

    if (!out || outSize == 0) return 0;

    rx_poll();

    primask = __get_PRIMASK();
    __disable_irq();

    if (cmdReady == 0u) {
        if (primask == 0u) __enable_irq();
        return 0;
    }

    if (cmdLength >= outSize) cmdLength = (uint8_t)(outSize - 1u);
    for (i = 0; i < cmdLength; i++) out[i] = cmdBuffer[i];
    out[cmdLength] = '\0';

    cmdReady = 0u;
    cmdLength = 0u;

    if (primask == 0u) __enable_irq();
    return 1;
}

void BspUart_USART_IRQHandler(void)
{
    if (USART_GetITStatus(COMM_USART, USART_IT_RXNE) != RESET) {
        uint8_t d = (uint8_t)USART_ReceiveData(COMM_USART);
        rx_feed_byte(d);
    }

    if (USART_GetFlagStatus(COMM_USART, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(COMM_USART);
    }

    if (USART_GetITStatus(COMM_USART, USART_IT_TXE) != RESET) {
        if (txTail != txHead) {
            uint8_t b = txBuffer[txTail];
            txTail = tx_next(txTail);
            USART_SendData(COMM_USART, b);
        } else {
            USART_ITConfig(COMM_USART, USART_IT_TXE, DISABLE);
        }
    }
}

/* ========== Mode-Aware Telemetry ========== */

static const char *track_telem_state_str(uint8_t dbgTelemState, uint8_t dbgTrackState)
{
    switch (dbgTelemState)
    {
    case LT_TLM_STATE_STRAIGHT:     return "STRA";
    case LT_TLM_STATE_SCURVE:       return "SCRV";
    case LT_TLM_STATE_EDGE:         return "EDGE";
    case LT_TLM_STATE_SEARCH_LEFT:  return "FNDL";
    case LT_TLM_STATE_SEARCH_RIGHT: return "FNDR";
    case LT_TLM_STATE_TRIM_LEFT:    return "TRML";
    case LT_TLM_STATE_TRIM_RIGHT:   return "TRMR";
    case LT_TLM_STATE_CROSS:        return "CROSS";
    default:
        break;
    }

    if (dbgTrackState == LT_TRACK_SEARCH_LEFT)  return "FNDL";
    if (dbgTrackState == LT_TRACK_SEARCH_RIGHT) return "FNDR";
    if (dbgTrackState == LT_TRACK_CROSS)        return "CROSS";
    return "TRK";
}

static const char *track_state_str(uint8_t trackState)
{
    switch (trackState)
    {
    case LT_TRACK_FOLLOW:       return "FOLLOW";
    case LT_TRACK_SEARCH_LEFT:  return "SEARCH_LEFT";
    case LT_TRACK_SEARCH_RIGHT: return "SEARCH_RIGHT";
    case LT_TRACK_CROSS:        return "CROSS";
    default:                    return "UNKNOWN";
    }
}

static const char *track_dir_str(uint8_t dir)
{
    switch (dir)
    {
    case LT_DIR_LEFT:  return "LEFT";
    case LT_DIR_RIGHT: return "RIGHT";
    default:           return "NONE";
    }
}

static const char *track_search_phase_str(uint8_t searchPhase)
{
    switch (searchPhase)
    {
    case LT_SEARCH_PHASE_ARC:   return "ARC";
    case LT_SEARCH_PHASE_PIVOT: return "PIVOT";
    default:                    return "NONE";
    }
}

static const char *track_cross_state_str(uint8_t crossState)
{
    switch (crossState)
    {
    case LT_CROSS_READY: return "READY";
    case LT_CROSS_SEEN:  return "SEEN";
    default:             return "NONE";
    }
}

static const char *track_resolve_source_str(uint8_t resolveSource)
{
    switch (resolveSource)
    {
    case 1u: return "LATCH";
    case 2u: return "EDGE";
    case 3u: return "TURN";
    case 4u: return "POS";
    case 5u: return "DEF";
    case 6u: return "FLIP";
    default: return "NONE";
    }
}

static void track_bits_to_bin(uint16_t bits, char *out, uint8_t width)
{
    uint8_t i;

    if (!out || width == 0u) {
        return;
    }

    for (i = 0u; i < width; i++) {
        uint8_t shift = (uint8_t)(width - 1u - i);
        out[i] = ((bits >> shift) & 0x1u) ? '1' : '0';
    }
    out[width] = '\0';
}

void BspUart_SendTelemetryStraight(uint32_t tMs, uint32_t experimentId, uint8_t run,
                                   int16_t encL, int16_t encR,
                                   int32_t encCountL, int32_t encCountR,
                                   float yaw, float yawRate,
                                   int16_t pwmCore, int16_t headingDiff,
                                   int16_t dPostDZ,
                                   int16_t pwmL, int16_t pwmR,
                                   float hi)
{
    char buf[200];
    sprintf(buf,
        "HB:t=%lu,m=S,run=%u,exp=%lu,el=%d,er=%d,ecl=%ld,ecr=%ld,yaw=%.1f,yr=%.1f,"
        "pc=%d,hd=%d,dp=%d,OL=%d,OR=%d,hi=%.2f\r\n",
        (unsigned long)tMs, (unsigned)run, (unsigned long)experimentId,
        (int)encL, (int)encR,
        (long)encCountL, (long)encCountR,
        (double)yaw, (double)yawRate,
        (int)pwmCore, (int)headingDiff, (int)dPostDZ,
        (int)pwmL, (int)pwmR, (double)hi);
    BspUart_SendString(buf);
}

void BspUart_SendTelemetryTrack(uint32_t tMs, uint32_t experimentId, uint8_t run,
                                int16_t encL, int16_t encR,
                                int32_t encCountL, int32_t encCountR,
                                float yaw, float yawRate,
                                int16_t pwmCore, int16_t headingDiff,
                                int16_t pwmL, int16_t pwmR,
                                uint16_t sensorBits, int16_t linePos,
                                int8_t bearingDev, uint8_t crossCount,
                                uint8_t dbgTrackState, uint8_t dbgTurnDir,
                                uint8_t dbgCrossActive, uint8_t crossState,
                                uint8_t dbgTelemState, uint8_t dbgScoreEnabled,
                                uint16_t dbgTelemFlags,
                                uint8_t gainStage, uint8_t searchPhase,
                                uint8_t searchDir, uint8_t cornerLatchDir,
                                uint8_t cornerLatchTicks, uint8_t recoverTicks,
                                uint8_t activeCount, uint8_t searchLost,
                                uint8_t searchReacquired, uint8_t cornerCandidateDir,
                                uint16_t lastData, uint8_t lastTurnDir,
                                uint8_t resolvedSearchDir, uint8_t resolvedSource)
{
    const char *stateStr;
    const char *trackStateStr;
    const char *turnDirStr;
    const char *searchDirStr;
    const char *searchPhaseStr;
    const char *cornerDirStr;
    const char *cornerCandStr;
    const char *crossStateStr;
    const char *lastTurnDirStr;
    const char *resolvedDirStr;
    const char *resolvedSourceStr;
    char dirChar = '-';
    char phaseChar = '-';
    char bitsBin[LINE_SENSOR_COUNT + 1u];
    char lastBitsBin[LINE_SENSOR_COUNT + 1u];
    char buf[560];

    stateStr = track_telem_state_str(dbgTelemState, dbgTrackState);
    trackStateStr = track_state_str(dbgTrackState);
    turnDirStr = track_dir_str(dbgTurnDir);
    searchDirStr = track_dir_str(searchDir);
    searchPhaseStr = track_search_phase_str(searchPhase);
    cornerDirStr = track_dir_str(cornerLatchDir);
    cornerCandStr = track_dir_str(cornerCandidateDir);
    crossStateStr = track_cross_state_str(crossState);
    lastTurnDirStr = track_dir_str(lastTurnDir);
    resolvedDirStr = track_dir_str(resolvedSearchDir);
    resolvedSourceStr = track_resolve_source_str(resolvedSource);
    track_bits_to_bin(sensorBits, bitsBin, LINE_SENSOR_COUNT);
    track_bits_to_bin(lastData, lastBitsBin, LINE_SENSOR_COUNT);

    if (dbgTurnDir == LT_DIR_LEFT) dirChar = 'L';
    else if (dbgTurnDir == LT_DIR_RIGHT) dirChar = 'R';
    if (searchPhase == LT_SEARCH_PHASE_ARC) phaseChar = 'A';
    else if (searchPhase == LT_SEARCH_PHASE_PIVOT) phaseChar = 'P';

    sprintf(buf,
        "HB:t=%lu,m=T,run=%u,exp=%lu,el=%d,er=%d,ecl=%ld,ecr=%ld,yaw=%.1f,yr=%.1f,"
        "pc=%d,hd=%d,OL=%d,OR=%d,sb=%u,sb2=%s,lp=%d,bd=%d,cc=%u,"
        "st=%s,sc=%u,td=%c,xa=%u,tf=%u,gs=%u,sp=%c,rt=%u,"
        "trk=%s,turn=%s,sdir=%s,sphase=%s,corner=%s,clt=%u,xst=%s,"
        "ac=%u,ls=%u,rq=%u,cand=%s,last=%s,ltd=%s,rdir=%s,rsrc=%s\r\n",
        (unsigned long)tMs, (unsigned)run, (unsigned long)experimentId,
        (int)encL, (int)encR,
        (long)encCountL, (long)encCountR,
        (double)yaw, (double)yawRate,
        (int)pwmCore, (int)headingDiff,
        (int)pwmL, (int)pwmR,
        (unsigned)sensorBits, bitsBin, (int)linePos, (int)bearingDev, (unsigned)crossCount,
        stateStr, (unsigned)dbgScoreEnabled, dirChar,
        (unsigned)dbgCrossActive,
        (unsigned)dbgTelemFlags, (unsigned)gainStage, phaseChar, (unsigned)recoverTicks,
        trackStateStr, turnDirStr, searchDirStr, searchPhaseStr, cornerDirStr,
        (unsigned)cornerLatchTicks, crossStateStr,
        (unsigned)activeCount, (unsigned)searchLost, (unsigned)searchReacquired,
        cornerCandStr, lastBitsBin, lastTurnDirStr, resolvedDirStr, resolvedSourceStr);
    BspUart_SendString(buf);
}

void BspUart_SendTelemetrySpin(uint32_t tMs, uint32_t experimentId, uint8_t run,
                               int16_t encL, int16_t encR,
                               int32_t encCountL, int32_t encCountR,
                               float yaw, float yawRate,
                               int16_t pwmL, int16_t pwmR)
{
    char buf[160];
    sprintf(buf,
        "HB:t=%lu,m=P,run=%u,exp=%lu,el=%d,er=%d,ecl=%ld,ecr=%ld,yaw=%.1f,yr=%.1f,"
        "OL=%d,OR=%d,st=SPIN\r\n",
        (unsigned long)tMs, (unsigned)run, (unsigned long)experimentId,
        (int)encL, (int)encR,
        (long)encCountL, (long)encCountR,
        (double)yaw, (double)yawRate,
        (int)pwmL, (int)pwmR);
    BspUart_SendString(buf);
}

void BspUart_SendStat(SystemState_t state, ControlMode_t mode,
                      float skp, float ski, float skd,
                      float akp, float aki, float akd,
                      float lkp, float lki, float lkd,
                      float targetSpeed)
{
    char buf[200];
    const char *stateStr = "STOP";
    if (state == SYS_STRAIGHT) stateStr = "STRAIGHT";
    else if (state == SYS_TRACKING) stateStr = "TRACKING";
    else if (state == SYS_SPINNING) stateStr = "SPINNING";

    sprintf(buf,
        "STAT:state=%s,mode=%s,spd=%.2f,"
        "skp=%.4f,ski=%.5f,skd=%.4f,"
        "akp=%.4f,aki=%.5f,akd=%.4f,"
        "lkp=%.4f,lki=%.5f,lkd=%.4f\r\n",
        stateStr,
        (mode == MODE_TRACK) ? "TRACK" : ((mode == MODE_TRACK3) ? "TRACK3" : "STRAIGHT"),
        (double)targetSpeed,
        (double)skp, (double)ski, (double)skd,
        (double)akp, (double)aki, (double)akd,
        (double)lkp, (double)lki, (double)lkd);
    BspUart_SendString(buf);
}
