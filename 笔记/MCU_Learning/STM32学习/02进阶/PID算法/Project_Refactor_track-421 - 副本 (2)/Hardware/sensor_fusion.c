#include "sensor_fusion.h"
#include "config.h"
#include "bsp_oled.h"
#include "Delay.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 *  BNO085 IMU Driver (Software I2C + SHTP Protocol)
 * ================================================================ */

#define BNO_SCL_H()     GPIO_SetBits(BNO_SCL_PORT, BNO_SCL_PIN)
#define BNO_SCL_L()     GPIO_ResetBits(BNO_SCL_PORT, BNO_SCL_PIN)
#define BNO_SCL_READ()  GPIO_ReadInputDataBit(BNO_SCL_PORT, BNO_SCL_PIN)
#define BNO_SDA_H()     GPIO_SetBits(BNO_SDA_PORT, BNO_SDA_PIN)
#define BNO_SDA_L()     GPIO_ResetBits(BNO_SDA_PORT, BNO_SDA_PIN)
#define BNO_SDA_READ()  GPIO_ReadInputDataBit(BNO_SDA_PORT, BNO_SDA_PIN)

#define BNO_I2C_ACK_TIMEOUT   1200
#define BNO_I2C_SCL_TIMEOUT   2400

#define BNO_CH_EXECUTABLE      1u
#define BNO_CH_CONTROL         2u
#define BNO_CH_REPORTS         3u
#define BNO_CH_WAKE_REPORTS    4u

#define BNO_RPT_PRODUCT_ID_REQ    0xF9u
#define BNO_RPT_PRODUCT_ID_RESP   0xF8u
#define BNO_RPT_BASE_TIMESTAMP    0xFBu
#define BNO_RPT_SET_FEATURE_CMD   0xFDu

#define BNO_SENSOR_ACCEL          0x01u
#define BNO_SENSOR_GYRO           0x02u
#define BNO_SENSOR_ROT_VEC        0x05u
#define BNO_SENSOR_GAME_ROT_VEC   0x08u

#define BNO_Q_ACCEL    8u
#define BNO_Q_GYRO     9u
#define BNO_Q_ROT      14u

#define BNO_RAD2DEG              57.2957795130823208768f
#define BNO_NO_RESET_BOOT_MS     250u
#define BNO_PRESENT_STABLE_CNT   3u
#define BNO_PRESENT_STABLE_GAP   10u
#define BNO_FLUSH_ROUNDS         8u
#define BNO_FIRST_REPORT_MS      1500u
#define BNO_PRESENT_SETTLE_MS    40u
#define BNO_SOFT_RESET_DELAY_MS  100u
#define BNO_PRODUCT_ID_TIMEOUT   2000u
#define BNO_SEND_RETRY_COUNT     3u
#define BNO_RECOVER_NOT_READY    200u
#define BNO_RECOVER_STALL        40u
#define BNO_STALE_DATA_LIMIT     25u

#define BNO_INIT_IDLE     0u
#define BNO_INIT_PROBE    1u
#define BNO_INIT_PRESENT  2u
#define BNO_INIT_PID      3u
#define BNO_INIT_FEATURE  4u
#define BNO_INIT_WAIT     5u
#define BNO_INIT_READY    6u

static uint8_t  s_bnoAddr = 0;
static uint8_t  s_bnoWho = 0;
static uint8_t  s_bnoSeq[6];
static uint8_t  s_bnoHeader[4];
static uint8_t  s_bnoData[BNO_PACKET_MAX];
static float    s_bnoYawBase = 0.0f;
static float    s_bnoRawYaw = 0.0f;
static uint8_t  s_bnoReady = 0u;
static uint8_t  s_bnoInitStage = BNO_INIT_IDLE;
static uint8_t  s_bnoInitBusy = 0u;
static uint16_t s_bnoReadFailStreak = 0u;
static uint16_t s_bnoNoNewDataCount = 0u;
static uint32_t s_bnoRxAttemptCount = 0u;
static uint32_t s_bnoRxPacketCount = 0u;
static uint8_t  s_bnoLastRxFailCode = 0u;
static uint8_t  s_bnoLastChannel = 0u;
static uint8_t  s_bnoLastReportId = 0u;
static uint16_t s_bnoLastPayloadLen = 0u;

static void bno_diag_update(void)
{
    BspOled_ShowIMUInit(s_bnoInitStage, s_bnoAddr);
}

static void bno_set_addr(uint8_t addr)
{
    s_bnoAddr = addr;
    bno_diag_update();
}

static void bno_set_stage(uint8_t stage)
{
    s_bnoInitStage = stage;
    bno_diag_update();
}

static uint8_t BNO_ReceivePacket(uint8_t *channel, uint16_t *payloadLen);

/* ---------- Software I2C ---------- */

static void bno_i2c_delay(void)
{
    uint8_t i;
    for (i = 0; i < 255; i++) __NOP();
}

static uint8_t bno_i2c_wait_scl_high(void)
{
    uint16_t t = 0;
    while (BNO_SCL_READ() == Bit_RESET) {
        if (++t >= BNO_I2C_SCL_TIMEOUT) return 0u;
        bno_i2c_delay();
    }
    return 1u;
}

static void bno_sda_out(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = BNO_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BNO_SDA_PORT, &g);
}

static void bno_sda_in(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = BNO_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(BNO_SDA_PORT, &g);
}

static uint8_t bno_i2c_wait_ack(void)
{
    uint16_t t = 0;
    bno_sda_in();
    BNO_SDA_H();
    BNO_SCL_H();
    if (!bno_i2c_wait_scl_high()) { BNO_SCL_L(); bno_sda_out(); return 1; }
    bno_i2c_delay();
    while (BNO_SDA_READ()) {
        if (++t >= BNO_I2C_ACK_TIMEOUT) { BNO_SCL_L(); bno_sda_out(); return 1; }
        bno_i2c_delay();
    }
    BNO_SCL_L(); bno_i2c_delay();
    bno_sda_out(); bno_i2c_delay();
    return 0;
}

static void bno_i2c_send_ack(void)
{
    bno_sda_out(); BNO_SDA_L();
    BNO_SCL_H(); (void)bno_i2c_wait_scl_high();
    bno_i2c_delay(); BNO_SCL_L();
}

static void bno_i2c_send_nack(void)
{
    bno_sda_out(); BNO_SDA_H();
    BNO_SCL_H(); (void)bno_i2c_wait_scl_high();
    bno_i2c_delay(); BNO_SCL_L();
}

static void bno_i2c_send_byte(uint8_t data)
{
    uint8_t i;
    bno_sda_out(); BNO_SCL_L(); bno_i2c_delay();
    for (i = 0; i < 8; i++) {
        if (data & 0x80) BNO_SDA_H(); else BNO_SDA_L();
        data <<= 1;
        bno_i2c_delay();
        BNO_SCL_H();
        if (!bno_i2c_wait_scl_high()) { BNO_SCL_L(); return; }
        bno_i2c_delay(); BNO_SCL_L(); bno_i2c_delay();
    }
}

static uint8_t bno_i2c_recv_byte(void)
{
    uint8_t i, data = 0;
    bno_sda_in();
    for (i = 0; i < 8; i++) {
        BNO_SCL_L(); bno_i2c_delay();
        BNO_SCL_H();
        if (!bno_i2c_wait_scl_high()) { BNO_SCL_L(); return data; }
        data <<= 1;
        if (BNO_SDA_READ()) data |= 0x01;
        bno_i2c_delay();
    }
    BNO_SCL_L();
    return data;
}

static void bno_i2c_start(void)
{
    bno_sda_out(); BNO_SDA_H(); BNO_SCL_H();
    (void)bno_i2c_wait_scl_high();
    bno_i2c_delay(); BNO_SDA_L(); bno_i2c_delay(); BNO_SCL_L();
}

static void bno_i2c_stop(void)
{
    bno_sda_out(); BNO_SCL_L(); BNO_SDA_L();
    bno_i2c_delay(); BNO_SCL_H();
    (void)bno_i2c_wait_scl_high();
    bno_i2c_delay(); BNO_SDA_H(); bno_i2c_delay();
}

/* ---------- BNO085 Helpers ---------- */

static float bno_wrap_deg(float deg)
{
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

static float bno_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int16_t bno_read_s16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[1] << 8) | p[0]);
}

static float bno_q_to_float(int16_t v, uint8_t q)
{
    return (float)v / (float)(1u << q);
}

static uint8_t bno_write_bytes(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    bno_i2c_start();
    bno_i2c_send_byte((uint8_t)(s_bnoAddr << 1));
    if (bno_i2c_wait_ack()) { bno_i2c_stop(); return 0u; }
    for (i = 0; i < len; i++) {
        bno_i2c_send_byte(buf[i]);
        if (bno_i2c_wait_ack()) { bno_i2c_stop(); return 0u; }
    }
    bno_i2c_stop();
    return 1u;
}

static uint8_t bno_read_block(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    if (len == 0u) return 0u;
    bno_i2c_start();
    bno_i2c_send_byte((uint8_t)((s_bnoAddr << 1) | 0x01u));
    if (bno_i2c_wait_ack()) { bno_i2c_stop(); return 0u; }
    for (i = 0; i < len; i++) {
        buf[i] = bno_i2c_recv_byte();
        if (i + 1u < len) bno_i2c_send_ack();
        else bno_i2c_send_nack();
    }
    bno_i2c_stop();
    return 1u;
}

static uint8_t bno_device_present(void)
{
    uint8_t retry;
    for (retry = 0; retry < 3u; retry++) {
        bno_i2c_start();
        bno_i2c_send_byte((uint8_t)(s_bnoAddr << 1));
        if (!bno_i2c_wait_ack()) { bno_i2c_stop(); return 1u; }
        bno_i2c_stop();
        Delay_ms(1);
    }
    return 0u;
}

static uint8_t bno_wait_present_stable(uint16_t timeoutMs, uint8_t stableCount)
{
    uint16_t waited = 0;
    uint8_t stable = 0;
    while (waited < timeoutMs) {
        if (bno_device_present()) {
            if (stable < stableCount) stable++;
            if (stable >= stableCount) return 1u;
        } else {
            stable = 0;
        }
        Delay_ms(BNO_PRESENT_STABLE_GAP);
        waited = (uint16_t)(waited + BNO_PRESENT_STABLE_GAP);
    }
    return 0u;
}

static uint8_t bno_wait_present(uint16_t timeoutMs)
{
    uint16_t waited = 0;
    while (waited < timeoutMs) {
        if (bno_device_present()) return 1u;
        Delay_ms(5);
        waited = (uint16_t)(waited + 5u);
    }
    return 0u;
}

static void bno_hardware_reset(void)
{
    GPIO_ResetBits(BNO_RST_PORT, BNO_RST_PIN);
    Delay_ms(10);
    GPIO_SetBits(BNO_RST_PORT, BNO_RST_PIN);
    Delay_ms(BNO_BOOT_DELAY_MS);
}

static void bno_flush_input(uint8_t rounds)
{
    uint8_t ch; uint16_t pl; uint8_t i;
    for (i = 0; i < rounds; i++) {
        if (!BNO_ReceivePacket(&ch, &pl)) Delay_ms(2);
    }
}

/* ---------- SHTP Send/Receive ---------- */

static uint8_t bno_send_packet(uint8_t channel, const uint8_t *payload, uint8_t payloadLen)
{
    uint8_t packet[BNO_PACKET_MAX];
    uint16_t packetLen;
    uint8_t seq, retry, i;

    packetLen = (uint16_t)payloadLen + 4u;
    if (packetLen > BNO_PACKET_MAX) return 0u;

    seq = s_bnoSeq[channel]++;
    packet[0] = (uint8_t)(packetLen & 0xFFu);
    packet[1] = (uint8_t)((packetLen >> 8) & 0xFFu);
    packet[2] = channel;
    packet[3] = seq;
    for (i = 0; i < payloadLen; i++) packet[4u + i] = payload[i];

    for (retry = 0; retry < BNO_SEND_RETRY_COUNT; retry++) {
        if (bno_write_bytes(packet, packetLen)) return 1u;
        Delay_ms(5);
    }
    return 0u;
}

static uint8_t BNO_ReceivePacket(uint8_t *channel, uint16_t *payloadLen)
{
    uint16_t totalLen, len, i;
    uint8_t raw[BNO_PACKET_MAX + 4u];

    if (GPIO_ReadInputDataBit(BNO_INT_PORT, BNO_INT_PIN) != Bit_RESET) return 0u;

    s_bnoRxAttemptCount++;
    s_bnoLastRxFailCode = 0u;
    memset(s_bnoHeader, 0, sizeof(s_bnoHeader));

    if (!bno_read_block(s_bnoHeader, 4u)) { s_bnoLastRxFailCode = 1u; return 0u; }

    totalLen = (uint16_t)(((uint16_t)s_bnoHeader[1] << 8) | s_bnoHeader[0]);
    totalLen &= (uint16_t)~0x8000u;
    if (totalLen < 4u) { s_bnoLastRxFailCode = 2u; return 0u; }

    len = (uint16_t)(totalLen - 4u);
    if (len == 0u || len > BNO_PACKET_MAX) { s_bnoLastRxFailCode = 3u; return 0u; }

    if (!bno_read_block(raw, (uint16_t)(len + 4u))) { s_bnoLastRxFailCode = 4u; return 0u; }

    memset(s_bnoData, 0, sizeof(s_bnoData));
    for (i = 0; i < len; i++) s_bnoData[i] = raw[i + 4u];

    s_bnoLastRxFailCode = 0u;
    s_bnoRxPacketCount++;
    if (channel) *channel = s_bnoHeader[2];
    if (payloadLen) *payloadLen = len;
    return 1u;
}

/* ---------- Report Parsing ---------- */

static void bno_update_euler(IMU_Data_t *data)
{
    float q0 = data->q0, q1 = data->q1, q2 = data->q2, q3 = data->q3;

    s_bnoRawYaw = -atan2f(2.0f * q1 * q2 + 2.0f * q0 * q3,
                           -2.0f * q2 * q2 - 2.0f * q3 * q3 + 1.0f) * BNO_RAD2DEG;
    data->yaw = bno_wrap_deg(s_bnoRawYaw - s_bnoYawBase);
    data->pitch = -asinf(bno_clampf(-2.0f * q1 * q3 + 2.0f * q0 * q2, -1.0f, 1.0f)) * BNO_RAD2DEG;
    data->roll = atan2f(2.0f * q2 * q3 + 2.0f * q0 * q1,
                         -2.0f * q1 * q1 - 2.0f * q2 * q2 + 1.0f) * BNO_RAD2DEG;
}

static uint8_t bno_parse_sensor_report(IMU_Data_t *data, uint16_t len)
{
    uint16_t off;
    uint8_t rid, parsed = 0u;

    if (len < 9u) return 0u;
    if (s_bnoData[0] != BNO_RPT_BASE_TIMESTAMP) return 0u;

    /* BNO085 batches multiple sensor reports into one SHTP packet when
       they share the same report interval.  Iterate through all reports.
       Report sizes: ACCEL/GYRO = 10 bytes, GAME_ROT_VEC = 12, ROT_VEC = 14. */
    off = 5u; /* skip 0xFB + 4-byte timestamp reference */
    while (off + 4u <= len) {
        rid = s_bnoData[off];
        s_bnoLastReportId = rid;

        if (rid == BNO_SENSOR_ACCEL && (off + 10u) <= len) {
            data->accelXf = bno_q_to_float(bno_read_s16(&s_bnoData[off + 4]), BNO_Q_ACCEL);
            data->accelYf = bno_q_to_float(bno_read_s16(&s_bnoData[off + 6]), BNO_Q_ACCEL);
            data->accelZf = bno_q_to_float(bno_read_s16(&s_bnoData[off + 8]), BNO_Q_ACCEL);
            parsed = 1u;
            off += 10u;
        } else if (rid == BNO_SENSOR_GYRO && (off + 10u) <= len) {
            data->gyroXf = bno_q_to_float(bno_read_s16(&s_bnoData[off + 4]), BNO_Q_GYRO) * BNO_RAD2DEG;
            data->gyroYf = bno_q_to_float(bno_read_s16(&s_bnoData[off + 6]), BNO_Q_GYRO) * BNO_RAD2DEG;
            data->gyroZf = bno_q_to_float(bno_read_s16(&s_bnoData[off + 8]), BNO_Q_GYRO) * BNO_RAD2DEG;
            parsed = 1u;
            off += 10u;
        } else if (rid == BNO_SENSOR_GAME_ROT_VEC && (off + 12u) <= len) {
            data->q1 = bno_q_to_float(bno_read_s16(&s_bnoData[off + 4]), BNO_Q_ROT);
            data->q2 = bno_q_to_float(bno_read_s16(&s_bnoData[off + 6]), BNO_Q_ROT);
            data->q3 = bno_q_to_float(bno_read_s16(&s_bnoData[off + 8]), BNO_Q_ROT);
            data->q0 = bno_q_to_float(bno_read_s16(&s_bnoData[off + 10]), BNO_Q_ROT);
            data->ahrsInited = 1u;
            data->yawSampleUpdated = 1u;
            bno_update_euler(data);
            parsed = 1u;
            off += 12u;
        } else if (rid == BNO_SENSOR_ROT_VEC && (off + 14u) <= len) {
            data->q1 = bno_q_to_float(bno_read_s16(&s_bnoData[off + 4]), BNO_Q_ROT);
            data->q2 = bno_q_to_float(bno_read_s16(&s_bnoData[off + 6]), BNO_Q_ROT);
            data->q3 = bno_q_to_float(bno_read_s16(&s_bnoData[off + 8]), BNO_Q_ROT);
            data->q0 = bno_q_to_float(bno_read_s16(&s_bnoData[off + 10]), BNO_Q_ROT);
            data->ahrsInited = 1u;
            data->yawSampleUpdated = 1u;
            bno_update_euler(data);
            parsed = 1u;
            off += 14u;
        } else {
            break; /* unknown report ID or insufficient data */
        }
    }
    return parsed;
}

/* ---------- Init Sequence ---------- */

static uint8_t bno_request_product_id(void)
{
    uint8_t req[2], ch;
    uint16_t len, retry;

    req[0] = BNO_RPT_PRODUCT_ID_REQ;
    req[1] = 0;
    bno_flush_input(BNO_FLUSH_ROUNDS);
    Delay_ms(20);
    if (!bno_send_packet(BNO_CH_CONTROL, req, 2u)) return 0u;

    for (retry = 0; retry < BNO_PRODUCT_ID_TIMEOUT; retry++) {
        if (BNO_ReceivePacket(&ch, &len)) {
            if (ch == BNO_CH_CONTROL && len >= 2u && s_bnoData[0] == BNO_RPT_PRODUCT_ID_RESP) {
                s_bnoWho = 0x85u;
                return 1u;
            }
        }
        Delay_ms(1);
    }
    return 0u;
}

static uint8_t bno_enable_report(uint8_t reportId, uint32_t intervalUs)
{
    uint8_t payload[17];
    memset(payload, 0, sizeof(payload));
    payload[0] = BNO_RPT_SET_FEATURE_CMD;
    payload[1] = reportId;
    payload[5] = (uint8_t)(intervalUs & 0xFFu);
    payload[6] = (uint8_t)((intervalUs >> 8) & 0xFFu);
    payload[7] = (uint8_t)((intervalUs >> 16) & 0xFFu);
    payload[8] = (uint8_t)((intervalUs >> 24) & 0xFFu);
    return bno_send_packet(BNO_CH_CONTROL, payload, 17u);
}

static uint8_t bno_wait_first_report(IMU_Data_t *data, uint16_t timeoutMs)
{
    uint16_t t;
    for (t = 0; t < timeoutMs; t++) {
        if (BNO085_ReadAll(data) && data->ahrsInited) return 1u;
        Delay_ms(1);
    }
    return 0u;
}

static uint8_t bno_attempt_init_at_addr(uint8_t addr, uint8_t doReset, uint16_t presentTimeoutMs,
                                         IMU_Data_t *tempData)
{
    bno_set_addr(addr);
    bno_set_stage(BNO_INIT_PROBE);
    memset(s_bnoSeq, 0, sizeof(s_bnoSeq));
    s_bnoReady = 0u;
    s_bnoWho = 0u;

    if (doReset) {
        bno_hardware_reset();
        Delay_ms(BNO_BOOT_DELAY_MS);
    } else {
        Delay_ms(BNO_NO_RESET_BOOT_MS);
    }

    if (!bno_wait_present(presentTimeoutMs)) { bno_set_addr(0u); return 0u; }
    if (!bno_wait_present_stable(
            (uint16_t)(BNO_PRESENT_STABLE_CNT * BNO_PRESENT_STABLE_GAP * 6u),
            BNO_PRESENT_STABLE_CNT)) {
        bno_set_addr(0u); return 0u;
    }

    bno_set_stage(BNO_INIT_PID);
    (void)bno_request_product_id();

    bno_set_stage(BNO_INIT_FEATURE);
    if (!bno_enable_report(BNO_SENSOR_GAME_ROT_VEC, BNO_REPORT_INTERVAL_US)) {
        bno_set_addr(0u); return 0u;
    }
    /* Enable calibrated gyroscope for direct angular velocity (D-term).
       Gyro output has ~5ms latency vs ~25ms for differentiated rot-vec,
       Use 20ms interval (50Hz) — same as GAME_ROT_VEC.
       Was incorrectly set to 40ms/25Hz (comment said 100Hz).
       50Hz matches rotation vector so both arrive at similar cadence. */
    (void)bno_enable_report(BNO_SENSOR_GYRO, 20000UL);

    s_bnoReady = 1u;
    bno_set_stage(BNO_INIT_WAIT);
    if (!bno_wait_first_report(tempData, BNO_FIRST_REPORT_MS)) {
        s_bnoReady = 0u; s_bnoWho = 0u; bno_set_addr(0u); return 0u;
    }

    s_bnoWho = 0x85u;
    bno_set_stage(BNO_INIT_READY);
    return 1u;
}

static void bno_try_recover(IMU_Data_t *data)
{
    if (s_bnoInitBusy) return;
    if (data) {
        data->ahrsInited = 0u;
        data->yaw = 0.0f;
        data->yawRate = 0.0f;
        data->prevYaw = 0.0f;
        data->yawRateValid = 0u;
        data->yawSampleUpdated = 0u;
    }
    BNO085_Init();
}

/* ---------- Public API ---------- */

void BNO085_Init(void)
{
    static const uint8_t addrList[] = {
        BNO_ADDR_ALT, BNO_ADDR_DEFAULT, BNO_ADDR_DOC_ALT, BNO_ADDR_DOC_DEF
    };
    GPIO_InitTypeDef g;
    uint8_t i;
    IMU_Data_t tempData;

    s_bnoInitBusy = 1u;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    g.GPIO_Pin = BNO_SCL_PIN | BNO_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BNO_SCL_PORT, &g);

    g.GPIO_Pin = BNO_RST_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(BNO_RST_PORT, &g);

    g.GPIO_Pin = BNO_INT_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(BNO_INT_PORT, &g);

    GPIO_SetBits(BNO_RST_PORT, BNO_RST_PIN);
    BNO_SCL_H(); BNO_SDA_H();

    memset(s_bnoSeq, 0, sizeof(s_bnoSeq));
    memset(s_bnoHeader, 0, sizeof(s_bnoHeader));
    memset(s_bnoData, 0, sizeof(s_bnoData));
    s_bnoYawBase = 0.0f;
    s_bnoRawYaw = 0.0f;
    s_bnoReady = 0u;
    bno_set_stage(BNO_INIT_IDLE);
    s_bnoWho = 0u;
    bno_set_addr(0u);
    s_bnoReadFailStreak = 0u;
    s_bnoNoNewDataCount = 0u;
    memset(&tempData, 0, sizeof(tempData));

    for (i = 0; i < (uint8_t)(sizeof(addrList) / sizeof(addrList[0])); i++) {
        if (bno_attempt_init_at_addr(addrList[i], 0u, BNO_PRESENT_TIMEOUT_MS, &tempData)) break;
    }

    if (s_bnoWho != 0x85u) {
        for (i = 0; i < (uint8_t)(sizeof(addrList) / sizeof(addrList[0])); i++) {
            if (bno_attempt_init_at_addr(addrList[i], 1u, BNO_PRESENT_TIMEOUT_MS, &tempData)) break;
        }
    }

    s_bnoInitBusy = 0u;
}

uint8_t BNO085_ReadAll(IMU_Data_t *data)
{
    uint8_t ch;
    uint16_t len;
    uint8_t got = 0u, parsed = 0u, i;

    if (data) data->yawSampleUpdated = 0u;

    if (!s_bnoReady) {
        if (!s_bnoInitBusy) {
            if (s_bnoReadFailStreak < 0xFFFFu) s_bnoReadFailStreak++;
            if (s_bnoReadFailStreak >= BNO_RECOVER_NOT_READY) {
                s_bnoReadFailStreak = 0u;
                bno_try_recover(data);
            }
        }
        return 0u;
    }

    for (i = 0; i < 3u; i++) {
        if (!BNO_ReceivePacket(&ch, &len)) break;
        got = 1u;
        s_bnoLastChannel = ch;
        s_bnoLastPayloadLen = len;
        if (ch == BNO_CH_REPORTS || ch == BNO_CH_WAKE_REPORTS) {
            parsed |= bno_parse_sensor_report(data, len);
        }
    }

    if (parsed) {
        s_bnoReadFailStreak = 0u;
        s_bnoNoNewDataCount = 0u;
        return 1u;
    }
    if (data->ahrsInited && !got) {
        s_bnoNoNewDataCount++;
        if (s_bnoNoNewDataCount >= BNO_STALE_DATA_LIMIT) {
            s_bnoNoNewDataCount = 0u;
            s_bnoReady = 0u;
            s_bnoWho = 0u;
            bno_set_stage(BNO_INIT_PROBE);
            bno_try_recover(data);
            return 0u;
        }
        s_bnoReadFailStreak = 0u;
        return 1u;
    }

    if (!s_bnoInitBusy) {
        if (s_bnoReadFailStreak < 0xFFFFu) s_bnoReadFailStreak++;
        if (s_bnoReadFailStreak >= BNO_RECOVER_STALL) {
            s_bnoReadFailStreak = 0u;
            s_bnoReady = 0u;
            s_bnoWho = 0u;
            bno_set_stage(BNO_INIT_PROBE);
            bno_try_recover(data);
        }
    }
    return parsed ? 1u : 0u;
}

void BNO085_ResetAttitude(IMU_Data_t *data)
{
    if (!data) return;
    s_bnoYawBase = s_bnoRawYaw;
    data->yaw = 0.0f;
    data->prevYaw = 0.0f;
    data->yawRate = 0.0f;
    data->yawRateValid = 0u;
    data->yawSampleUpdated = 0u;
}

void BNO085_UpdateYaw(IMU_Data_t *data, float dt)
{
    float newYaw, dy, yawRate, maxStep;

    if (!data || !data->ahrsInited) return;

    newYaw = bno_wrap_deg(s_bnoRawYaw - s_bnoYawBase);
    if (data->yawRateValid && dt > 0.0f) {
        dy = newYaw - data->prevYaw;
        while (dy > 180.0f) dy -= 360.0f;
        while (dy < -180.0f) dy += 360.0f;
        yawRate = dy / dt;
        if (fabsf(dy) > BNO_YAW_JUMP_REJECT_DEG || fabsf(yawRate) > BNO_YAW_RATE_LIMIT_DPS) {
            maxStep = BNO_YAW_RATE_LIMIT_DPS * dt;
            if (maxStep <= 0.0f) maxStep = BNO_YAW_JUMP_REJECT_DEG;
            if (maxStep > BNO_YAW_JUMP_REJECT_DEG) maxStep = BNO_YAW_JUMP_REJECT_DEG;
            if (dy > maxStep) dy = maxStep;
            if (dy < -maxStep) dy = -maxStep;
            newYaw = bno_wrap_deg(data->prevYaw + dy);
            yawRate = dy / dt;
        }
        if (yawRate > BNO_YAW_RATE_LIMIT_DPS) yawRate = BNO_YAW_RATE_LIMIT_DPS;
        if (yawRate < -BNO_YAW_RATE_LIMIT_DPS) yawRate = -BNO_YAW_RATE_LIMIT_DPS;
        data->yawRate += (yawRate - data->yawRate) * BNO_YAW_RATE_LPF_ALPHA;
    } else {
        data->yawRate = 0.0f;
        data->yawRateValid = 1u;
    }
    data->yaw = newYaw;
    data->prevYaw = newYaw;
}

float BNO085_GetYawError(float target, float current)
{
    float e = target - current;
    while (e > 180.0f) e -= 360.0f;
    while (e < -180.0f) e += 360.0f;
    return e;
}

uint8_t BNO085_IsReady(void)      { return s_bnoReady; }
uint8_t BNO085_GetInitStage(void) { return s_bnoInitStage; }
uint8_t BNO085_GetI2CAddr(void)   { return s_bnoAddr; }

uint8_t BNO085_GetLastRxFailCode(void)  { return s_bnoLastRxFailCode; }
uint8_t BNO085_GetLastChannel(void)     { return s_bnoLastChannel; }
uint8_t BNO085_GetLastReportId(void)    { return s_bnoLastReportId; }
uint16_t BNO085_GetLastPayloadLen(void) { return s_bnoLastPayloadLen; }

/* ================================================================
 *  Line Tracking Sensors (8-channel)
 * ================================================================ */

static const int16_t s_lineWeights[LINE_SENSOR_COUNT] = {
    -350, -250, -150, -50, 50, 150, 250, 350
};

void LineSensor_Init(void)
{
    GPIO_InitTypeDef g;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);

    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_50MHz;

    g.GPIO_Pin = LINE_S1_PIN | LINE_S2_PIN | LINE_S3_PIN;
    GPIO_Init(GPIOA, &g);

    g.GPIO_Pin = LINE_S4_PIN | LINE_S5_PIN | LINE_S6_PIN | LINE_S7_PIN;
    GPIO_Init(GPIOB, &g);

    g.GPIO_Pin = LINE_S8_PIN;
    GPIO_Init(GPIOC, &g);
}

static uint8_t line_is_active(GPIO_TypeDef *port, uint16_t pin)
{
    uint8_t level = (GPIO_ReadInputDataBit(port, pin) == Bit_SET) ? 1u : 0u;
#if LINE_ACTIVE_LOW
    return level ? 0u : 1u;
#else
    return level;
#endif
}

static uint8_t line_bit_count(uint8_t bits)
{
    uint8_t c = 0, i;
    for (i = 0; i < 8; i++) {
        if (bits & (1u << i)) c++;
    }
    return c;
}

void LineSensor_Read(LineSensor_Data_t *data)
{
    uint8_t bits = 0, count, i;
    int32_t sum = 0;

    if (!data) return;

    if (line_is_active(LINE_S1_PORT, LINE_S1_PIN)) bits |= (1u << 0);
    if (line_is_active(LINE_S2_PORT, LINE_S2_PIN)) bits |= (1u << 1);
    if (line_is_active(LINE_S3_PORT, LINE_S3_PIN)) bits |= (1u << 2);
    if (line_is_active(LINE_S4_PORT, LINE_S4_PIN)) bits |= (1u << 3);
    if (line_is_active(LINE_S5_PORT, LINE_S5_PIN)) bits |= (1u << 4);
    if (line_is_active(LINE_S6_PORT, LINE_S6_PIN)) bits |= (1u << 5);
    if (line_is_active(LINE_S7_PORT, LINE_S7_PIN)) bits |= (1u << 6);
    if (line_is_active(LINE_S8_PORT, LINE_S8_PIN)) bits |= (1u << 7);

    count = line_bit_count(bits);
    data->bits = bits;
    data->count = count;

    if (count == 0u) {
        data->lineDetected = 0u;
        data->position = 0.0f;
        return;
    }

    data->lineDetected = 1u;
    for (i = 0; i < LINE_SENSOR_COUNT; i++) {
        if (bits & (1u << i)) sum += s_lineWeights[i];
    }
    data->position = (float)sum / ((float)count * 100.0f);
}
