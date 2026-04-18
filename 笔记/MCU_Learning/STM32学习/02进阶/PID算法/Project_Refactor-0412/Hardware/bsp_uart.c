#include "bsp_uart.h"
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

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(COMM_USART_RCC, ENABLE);

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

void BspUart_SendTelemetryStraight(uint32_t tMs, uint8_t run,
                                   int16_t encL, int16_t encR,
                                   float yaw, float yawRate,
                                   int16_t pwmCore, int16_t headingDiff,
                                   int16_t dPostDZ,
                                   int16_t pwmL, int16_t pwmR,
                                   float hi)
{
    char buf[180];
    sprintf(buf,
        "HB:t=%lu,m=S,run=%u,el=%d,er=%d,yaw=%.1f,yr=%.1f,"
        "pc=%d,hd=%d,dp=%d,OL=%d,OR=%d,hi=%.2f\r\n",
        (unsigned long)tMs, (unsigned)run,
        (int)encL, (int)encR,
        (double)yaw, (double)yawRate,
        (int)pwmCore, (int)headingDiff, (int)dPostDZ,
        (int)pwmL, (int)pwmR, (double)hi);
    BspUart_SendString(buf);
}

void BspUart_SendTelemetryTrack(uint32_t tMs, uint8_t run,
                                int16_t encL, int16_t encR,
                                float yaw, float yawRate,
                                int16_t pwmCore, int16_t headingDiff,
                                int16_t pwmL, int16_t pwmR,
                                uint8_t sensorBits, float linePos,
                                uint8_t acuteState, float acuteYawDelta,
                                uint32_t acuteRearmTick,
                                uint8_t dbgTrackState, uint8_t dbgCornerDir,
                                float dbgCornerYawDelta, uint8_t dbgCornerBits,
                                uint8_t dbgCornerAcceptMask,
                                uint8_t dbgCornerYawReady,
                                uint8_t dbgCornerAcceptHit)
{
    const char *stateStr = "TRK";
    char dirChar = '-';
    char buf[360];

    if (dbgTrackState == 1u) stateStr = "AWN";
    else if (dbgTrackState == 2u) stateStr = "ATN";
    else if (dbgTrackState == 3u) stateStr = "ARC";
    else if (dbgTrackState == 4u) stateStr = "CSR";
    else if (dbgTrackState == 5u) stateStr = "R90";
    else if (dbgTrackState == 6u) stateStr = "CRS";

    if (dbgCornerDir == 1u) dirChar = 'L';
    else if (dbgCornerDir == 2u) dirChar = 'R';

    sprintf(buf,
        "HB:t=%lu,m=T,run=%u,el=%d,er=%d,yaw=%.1f,yr=%.1f,"
        "pc=%d,hd=%d,OL=%d,OR=%d,sb=%u,lp=%.2f,"
        "acuteState=%u,acuteYawDelta=%.1f,acuteRearmTick=%lu,"
        "st=%s,cd=%c,cy=%.1f,cb=%u,cm=%u,cg=%u,ch=%u\r\n",
        (unsigned long)tMs, (unsigned)run,
        (int)encL, (int)encR,
        (double)yaw, (double)yawRate,
        (int)pwmCore, (int)headingDiff,
        (int)pwmL, (int)pwmR,
        (unsigned)sensorBits, (double)linePos,
        (unsigned)acuteState, (double)acuteYawDelta,
        (unsigned long)acuteRearmTick,
        stateStr, dirChar,
        (double)dbgCornerYawDelta,
        (unsigned)dbgCornerBits,
        (unsigned)dbgCornerAcceptMask,
        (unsigned)dbgCornerYawReady,
        (unsigned)dbgCornerAcceptHit);
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

    sprintf(buf,
        "STAT:state=%s,mode=%s,spd=%.2f,"
        "skp=%.4f,ski=%.5f,skd=%.4f,"
        "akp=%.4f,aki=%.5f,akd=%.4f,"
        "lkp=%.4f,lki=%.5f,lkd=%.4f\r\n",
        stateStr,
        (mode == MODE_TRACK) ? "TRACK" : "STRAIGHT",
        (double)targetSpeed,
        (double)skp, (double)ski, (double)skd,
        (double)akp, (double)aki, (double)akd,
        (double)lkp, (double)lki, (double)lkd);
    BspUart_SendString(buf);
}
