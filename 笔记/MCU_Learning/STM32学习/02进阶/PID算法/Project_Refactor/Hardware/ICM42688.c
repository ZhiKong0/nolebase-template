#include "stm32f10x.h"
#include "ICM42688.h"
#include "Delay.h"
#include "VOFA.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define ICM_I2C_SCL_PIN     GPIO_Pin_12
#define ICM_I2C_SDA_PIN     GPIO_Pin_13
#define ICM_I2C_PORT        GPIOB

#define ICM_SCL_H()         GPIO_SetBits(ICM_I2C_PORT, ICM_I2C_SCL_PIN)
#define ICM_SCL_L()         GPIO_ResetBits(ICM_I2C_PORT, ICM_I2C_SCL_PIN)
#define ICM_SCL_READ()      GPIO_ReadInputDataBit(ICM_I2C_PORT, ICM_I2C_SCL_PIN)
#define ICM_SDA_H()         GPIO_SetBits(ICM_I2C_PORT, ICM_I2C_SDA_PIN)
#define ICM_SDA_L()         GPIO_ResetBits(ICM_I2C_PORT, ICM_I2C_SDA_PIN)
#define ICM_SDA_READ()      GPIO_ReadInputDataBit(ICM_I2C_PORT, ICM_I2C_SDA_PIN)

#define ICM_I2C_ACK_TIMEOUT  1200
#define ICM_I2C_SCL_TIMEOUT  2400

#define ICM42688_ADDR_DEFAULT    0x68
#define ICM42688_ADDR_ALT        0x69

#define ICM42688_REG_DEVICE_CONFIG   0x11
#define ICM42688_REG_ACCEL_DATA_X1   0x1F
#define ICM42688_REG_GYRO_DATA_X1    0x25
#define ICM42688_REG_PWR_MGMT0       0x4E
#define ICM42688_REG_GYRO_CONFIG0    0x4F
#define ICM42688_REG_ACCEL_CONFIG0   0x50
#define ICM42688_REG_WHO_AM_I        0x75

#define ICM42688_ACCEL_CFG_4G_200HZ      0x47
#define ICM42688_GYRO_CFG_1000DPS_200HZ  0x27

#define ICM42688_AHRS_KP             0.5f
#define ICM42688_AHRS_KI             0.001f
#define ICM42688_GYRO_VAR_WIN        100
#define ICM42688_GYRO_VAR_THRESH     0.05f
#define ICM42688_DEG2RAD             0.01745329251994329577f

#define BNO085_RESET_PIN             GPIO_Pin_14
#define BNO085_RESET_PORT            GPIOB
#define BNO085_INT_PIN               GPIO_Pin_15
#define BNO085_INT_PORT              GPIOA

#define BNO085_ADDR_DEFAULT          0x4B
#define BNO085_ADDR_ALT              0x4A
#define BNO085_ADDR_DOC_DEFAULT      0x28
#define BNO085_ADDR_DOC_ALT          0x29

#define BNO085_CHANNEL_EXECUTABLE    1u
#define BNO085_CHANNEL_CONTROL       2u
#define BNO085_CHANNEL_REPORTS       3u
#define BNO085_CHANNEL_WAKE_REPORTS  4u

#define BNO085_REPORT_PRODUCT_ID_REQUEST   0xF9u
#define BNO085_REPORT_PRODUCT_ID_RESPONSE  0xF8u
#define BNO085_REPORT_BASE_TIMESTAMP       0xFBu
#define BNO085_REPORT_SET_FEATURE_COMMAND  0xFDu

#define BNO085_SENSOR_ACCELEROMETER        0x01u
#define BNO085_SENSOR_GYROSCOPE            0x02u
#define BNO085_SENSOR_ROTATION_VECTOR      0x05u
#define BNO085_SENSOR_GAME_ROTATION_VECTOR 0x08u

#define BNO085_Q_ACCEL                8u
#define BNO085_Q_GYRO                 9u
#define BNO085_Q_ROTATION             14u

#define BNO085_PACKET_MAX             384u
#define BNO085_REPORT_INTERVAL_US     20000UL
#define BNO085_RAD2DEG                57.2957795130823208768f
#define BNO085_BOOT_DELAY_MS          80u
#define BNO085_PRESENT_TIMEOUT_MS     1000u
#define BNO085_NO_RESET_BOOT_WAIT_MS  250u
#define BNO085_PRESENT_STABLE_COUNT   3u
#define BNO085_PRESENT_STABLE_GAP_MS  10u
#define BNO085_FLUSH_ROUNDS           8u
#define BNO085_FIRST_REPORT_TIMEOUT_MS 1500u
#define BNO085_PRESENT_SETTLE_MS      40u
#define BNO085_SOFT_RESET_DELAY_MS    100u
#define BNO085_PRODUCT_ID_TIMEOUT_MS  2000u
#define BNO085_SEND_RETRY_COUNT       3u
#define BNO085_RECOVER_NOT_READY_FAILS 200u
#define BNO085_RECOVER_STALL_FAILS    40u
#define BNO085_YAW_JUMP_REJECT_DEG    45.0f
#define BNO085_YAW_RATE_LIMIT_DPS     180.0f
#define BNO085_YAW_RATE_LPF_ALPHA     0.35f

#define BNO085_INIT_STAGE_IDLE        0u
#define BNO085_INIT_STAGE_PROBE       1u
#define BNO085_INIT_STAGE_PRESENT     2u
#define BNO085_INIT_STAGE_PRODUCT_ID  3u
#define BNO085_INIT_STAGE_FEATURE     4u
#define BNO085_INIT_STAGE_WAIT_REPORT 5u
#define BNO085_INIT_STAGE_READY       6u

static uint8_t g_icmAddr = ICM42688_ADDR_DEFAULT;
static uint8_t g_icmWho = 0;
static float g_accSensitivity = 4000.0f / 32768.0f;
static float g_gyroSensitivity = 1000.0f / 32768.0f;
static float g_gyroFill[3][ICM42688_GYRO_VAR_WIN];
static float g_gyroTotal[3];
static float g_gyroSqrTotal[3];
static uint16_t g_gyroVarCount = 0;
static uint8_t g_gyroVarInited = 0;
static uint8_t g_biasTrackEnabled = 1u;
static uint8_t g_bnoSeq[6];
static uint8_t g_bnoHeader[4];
static uint8_t g_bnoData[BNO085_PACKET_MAX];
static float g_bnoYawBase = 0.0f;
static float g_bnoRawYaw = 0.0f;
static uint8_t g_bnoReady = 0u;
static uint8_t g_bnoLastProbeAddr = 0u;
static uint8_t g_bnoInitStage = BNO085_INIT_STAGE_IDLE;
static uint8_t g_bnoProbeWho = 0u;
static uint8_t g_bnoScanFirstAddr = 0u;
static uint8_t g_bnoScanLastAddr = 0u;
static uint8_t g_bnoScanHitCount = 0u;
static uint8_t g_bnoLastChannel = 0u;
static uint8_t g_bnoLastReportId = 0u;
static uint16_t g_bnoLastPayloadLen = 0u;
static uint8_t g_bnoLastTxChannel = 0u;
static uint16_t g_bnoLastTxPacketLen = 0u;
static uint16_t g_bnoLastWriteFailIndex = 0u;
static uint8_t g_bnoInitBusy = 0u;
static uint16_t g_bnoReadFailStreak = 0u;
static uint32_t g_bnoRxAttemptCount = 0u;
static uint32_t g_bnoRxPacketCount = 0u;
static uint8_t g_bnoLastRxFailCode = 0u;

static uint8_t BNO085_ReceivePacket(uint8_t *channel, uint16_t *payloadLen);

static void BNO085_Trace(const char *s) {
    VOFA_SendString(s);
}

static float ICM42688_InvSqrt(float x) {
    float halfx = 0.5f * x;
    float y = x;
    long i;

    if (x <= 0.0f) return 0.0f;

    i = *(long*)&y;
    i = 0x5f3759df - (i >> 1);
    y = *(float*)&i;
    y = y * (1.5f - (halfx * y * y));
    return y;
}

static float ICM42688_ClampFloat(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void ICM42688_ResetBiasTracker(void) {
    memset(g_gyroFill, 0, sizeof(g_gyroFill));
    memset(g_gyroTotal, 0, sizeof(g_gyroTotal));
    memset(g_gyroSqrTotal, 0, sizeof(g_gyroSqrTotal));
    g_gyroVarCount = 0;
    g_gyroVarInited = 0;
}

static void ICM42688_UpdateGyroBias(ICM42688_Data_t *data) {
    float samples[3];
    float avg[3];
    float var[3];
    uint16_t i;
    float lenf;

    samples[0] = data->gyroXf;
    samples[1] = data->gyroYf;
    samples[2] = data->gyroZf;

    if (!g_gyroVarInited) {
        for (i = 0; i < 3; i++) {
            g_gyroFill[i][g_gyroVarCount] = samples[i];
            g_gyroTotal[i] += samples[i];
            g_gyroSqrTotal[i] += samples[i] * samples[i];
        }
    } else {
        for (i = 0; i < 3; i++) {
            g_gyroTotal[i] -= g_gyroFill[i][g_gyroVarCount];
            g_gyroSqrTotal[i] -= g_gyroFill[i][g_gyroVarCount] * g_gyroFill[i][g_gyroVarCount];
            g_gyroFill[i][g_gyroVarCount] = samples[i];
            g_gyroTotal[i] += samples[i];
            g_gyroSqrTotal[i] += samples[i] * samples[i];
        }
    }

    g_gyroVarCount++;
    if (g_gyroVarCount >= ICM42688_GYRO_VAR_WIN) {
        g_gyroVarCount = 0;
        g_gyroVarInited = 1;
    }

    if (!g_gyroVarInited) return;

    lenf = (float)ICM42688_GYRO_VAR_WIN;
    for (i = 0; i < 3; i++) {
        avg[i] = g_gyroTotal[i] / lenf;
        var[i] = (g_gyroSqrTotal[i] - g_gyroTotal[i] * g_gyroTotal[i] / lenf) / lenf;
    }

    if (var[0] < ICM42688_GYRO_VAR_THRESH &&
        var[1] < ICM42688_GYRO_VAR_THRESH &&
        var[2] < ICM42688_GYRO_VAR_THRESH) {
        data->gyroXOffset = avg[0];
        data->gyroYOffset = avg[1];
        data->gyroZOffset = avg[2];
        data->exInt = 0.0f;
        data->eyInt = 0.0f;
        data->ezInt = 0.0f;
    }
}

static void ICM_I2C_Delay(void) {
    uint8_t i;
    for (i = 0; i < 255; i++) {
        __NOP();
    }
}

static uint8_t ICM_I2C_WaitSclHigh(void) {
    uint16_t timeout = 0u;
    while (ICM_SCL_READ() == Bit_RESET) {
        if (++timeout >= ICM_I2C_SCL_TIMEOUT) {
            return 0u;
        }
        ICM_I2C_Delay();
    }
    return 1u;
}

static void ICM_SDA_OUT(void) {
    GPIO_InitTypeDef g;
    g.GPIO_Pin = ICM_I2C_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ICM_I2C_PORT, &g);
}

static void ICM_SDA_IN(void) {
    GPIO_InitTypeDef g;
    g.GPIO_Pin = ICM_I2C_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(ICM_I2C_PORT, &g);
}

static uint8_t ICM_I2C_WaitAck(void) {
    uint16_t timeout = 0;
    ICM_SDA_IN();
    ICM_SDA_H();
    ICM_SCL_H();
    if (!ICM_I2C_WaitSclHigh()) {
        ICM_SCL_L();
        ICM_SDA_OUT();
        return 1;
    }
    ICM_I2C_Delay();
    while (ICM_SDA_READ()) {
        if (++timeout >= ICM_I2C_ACK_TIMEOUT) {
            ICM_SCL_L();
            ICM_SDA_OUT();
            return 1;
        }
        ICM_I2C_Delay();
    }
    ICM_SCL_L();
    ICM_I2C_Delay();
    ICM_SDA_OUT();
    ICM_I2C_Delay();
    return 0;
}

static void ICM_I2C_SendAck(void) {
    ICM_SDA_OUT();
    ICM_SDA_L();
    ICM_SCL_H();
    (void)ICM_I2C_WaitSclHigh();
    ICM_I2C_Delay();
    ICM_SCL_L();
}

static void ICM_I2C_SendNack(void) {
    ICM_SDA_OUT();
    ICM_SDA_H();
    ICM_SCL_H();
    (void)ICM_I2C_WaitSclHigh();
    ICM_I2C_Delay();
    ICM_SCL_L();
}

static void ICM_I2C_SendByte(uint8_t data) {
    uint8_t i;
    ICM_SDA_OUT();
    ICM_SCL_L();
    ICM_I2C_Delay();
    for (i = 0; i < 8; i++) {
        if (data & 0x80) ICM_SDA_H();
        else ICM_SDA_L();
        data <<= 1;
        ICM_I2C_Delay();
        ICM_SCL_H();
        if (!ICM_I2C_WaitSclHigh()) {
            ICM_SCL_L();
            return;
        }
        ICM_I2C_Delay();
        ICM_SCL_L();
        ICM_I2C_Delay();
    }
}

static uint8_t ICM_I2C_RecvByte(void) {
    uint8_t i, data = 0;
    ICM_SDA_IN();
    for (i = 0; i < 8; i++) {
        ICM_SCL_L();
        ICM_I2C_Delay();
        ICM_SCL_H();
        if (!ICM_I2C_WaitSclHigh()) {
            ICM_SCL_L();
            return data;
        }
        data <<= 1;
        if (ICM_SDA_READ()) data |= 0x01;
        ICM_I2C_Delay();
    }
    ICM_SCL_L();
    return data;
}

static void ICM_I2C_Start(void) {
    ICM_SDA_OUT();
    ICM_SDA_H();
    ICM_SCL_H();
    (void)ICM_I2C_WaitSclHigh();
    ICM_I2C_Delay();
    ICM_SDA_L();
    ICM_I2C_Delay();
    ICM_SCL_L();
}

static void ICM_I2C_Stop(void) {
    ICM_SDA_OUT();
    ICM_SCL_L();
    ICM_SDA_L();
    ICM_I2C_Delay();
    ICM_SCL_H();
    (void)ICM_I2C_WaitSclHigh();
    ICM_I2C_Delay();
    ICM_SDA_H();
    ICM_I2C_Delay();
}

static float BNO085_WrapDeg(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

static int16_t BNO085_ReadS16(const uint8_t *p) {
    return (int16_t)(((uint16_t)p[1] << 8) | p[0]);
}

static float BNO085_QToFloat(int16_t v, uint8_t q) {
    return (float)v / (float)(1u << q);
}

static uint8_t BNO085_WriteBytes(const uint8_t *buf, uint16_t len) {
    uint16_t i;

    g_bnoLastWriteFailIndex = 0u;
    ICM_I2C_Start();
    ICM_I2C_SendByte((uint8_t)(g_icmAddr << 1));
    if (ICM_I2C_WaitAck()) {
        g_bnoLastWriteFailIndex = 0u;
        ICM_I2C_Stop();
        return 0u;
    }

    for (i = 0; i < len; i++) {
        ICM_I2C_SendByte(buf[i]);
        if (ICM_I2C_WaitAck()) {
            g_bnoLastWriteFailIndex = (uint16_t)(i + 1u);
            ICM_I2C_Stop();
            return 0u;
        }
    }

    ICM_I2C_Stop();
    return 1u;
}

static uint8_t BNO085_DevicePresent(void) {
    uint8_t retry;

    for (retry = 0u; retry < 3u; retry++) {
        ICM_I2C_Start();
        ICM_I2C_SendByte((uint8_t)(g_icmAddr << 1));
        if (!ICM_I2C_WaitAck()) {
            ICM_I2C_Stop();
            return 1u;
        }
        ICM_I2C_Stop();
        Delay_ms(1);
    }

    return 0u;
}

static uint8_t BNO085_WaitPresentStable(uint16_t timeoutMs, uint8_t stableCount) {
    uint16_t waited = 0u;
    uint8_t stable = 0u;

    while (waited < timeoutMs) {
        if (BNO085_DevicePresent()) {
            if (stable < stableCount) stable++;
            if (stable >= stableCount) return 1u;
        } else {
            stable = 0u;
        }
        Delay_ms(BNO085_PRESENT_STABLE_GAP_MS);
        waited = (uint16_t)(waited + BNO085_PRESENT_STABLE_GAP_MS);
    }

    return 0u;
}

static uint8_t BNO085_WaitPresent(uint16_t timeoutMs) {
    uint16_t waited = 0u;

    while (waited < timeoutMs) {
        if (BNO085_DevicePresent()) return 1u;
        Delay_ms(5);
        waited = (uint16_t)(waited + 5u);
    }

    return 0u;
}

static void BNO085_ScanBus(void) {
    uint8_t savedAddr = g_icmAddr;
    uint8_t addr;

    g_bnoScanFirstAddr = 0u;
    g_bnoScanLastAddr = 0u;
    g_bnoScanHitCount = 0u;

    for (addr = 0x08u; addr < 0x78u; addr++) {
        g_icmAddr = addr;
        if (!BNO085_DevicePresent()) continue;
        if (g_bnoScanHitCount == 0u) g_bnoScanFirstAddr = addr;
        g_bnoScanLastAddr = addr;
        if (g_bnoScanHitCount < 0xFFu) g_bnoScanHitCount++;
        Delay_ms(1);
    }

    g_icmAddr = savedAddr;
}

static void BNO085_HardwareReset(void) {
    GPIO_ResetBits(BNO085_RESET_PORT, BNO085_RESET_PIN);
    Delay_ms(10);
    GPIO_SetBits(BNO085_RESET_PORT, BNO085_RESET_PIN);
    Delay_ms(80);
}

static void BNO085_FlushInput(uint8_t rounds) {
    uint8_t channel;
    uint16_t payloadLen;
    uint8_t i;

    for (i = 0; i < rounds; i++) {
        if (!BNO085_ReceivePacket(&channel, &payloadLen)) {
            Delay_ms(2);
            continue;
        }
    }
}

static uint8_t BNO085_ReadBlock(uint8_t *buf, uint16_t len) {
    uint16_t i;

    if (len == 0u) {
        return 0u;
    }

    ICM_I2C_Start();
    ICM_I2C_SendByte((uint8_t)((g_icmAddr << 1) | 0x01u));
    if (ICM_I2C_WaitAck()) {
        ICM_I2C_Stop();
        return 0u;
    }

    for (i = 0; i < len; i++) {
        buf[i] = ICM_I2C_RecvByte();
        if (i + 1u < len) {
            ICM_I2C_SendAck();
        } else {
            ICM_I2C_SendNack();
        }
    }

    ICM_I2C_Stop();
    return 1u;
}

static uint8_t BNO085_SendPacket(uint8_t channel, const uint8_t *payload, uint8_t payloadLen) {
    uint8_t packet[BNO085_PACKET_MAX];
    uint16_t packetLen;
    uint8_t attempt;
    uint8_t seq;
    uint8_t retry;
    uint8_t i;

    packetLen = (uint16_t)payloadLen + 4u;
    if (packetLen > BNO085_PACKET_MAX) return 0u;

    g_bnoLastTxChannel = channel;
    g_bnoLastTxPacketLen = packetLen;
    seq = g_bnoSeq[channel]++;

    packet[0] = (uint8_t)(packetLen & 0xFFu);
    packet[1] = (uint8_t)((packetLen >> 8) & 0xFFu);
    packet[2] = channel;
    packet[3] = seq;

    for (i = 0; i < payloadLen; i++) {
        packet[4u + i] = payload[i];
    }

    for (retry = 0u; retry < BNO085_SEND_RETRY_COUNT; retry++) {
        if (BNO085_WriteBytes(packet, packetLen)) {
            return 1u;
        }
        Delay_ms(5);
    }

    return 0u;
}

static uint8_t BNO085_ReceivePacket(uint8_t *channel, uint16_t *payloadLen) {
    uint16_t totalLen;
    uint16_t len;
    uint16_t i;
    uint8_t raw[BNO085_PACKET_MAX + 4u];

    if (GPIO_ReadInputDataBit(BNO085_INT_PORT, BNO085_INT_PIN) != Bit_RESET) {
        return 0u;
    }

    g_bnoRxAttemptCount++;
    g_bnoLastRxFailCode = 0u;
    memset(g_bnoHeader, 0, sizeof(g_bnoHeader));

    if (!BNO085_ReadBlock(g_bnoHeader, 4u)) {
        g_bnoLastRxFailCode = 1u;
        return 0u;
    }

    totalLen = (uint16_t)(((uint16_t)g_bnoHeader[1] << 8) | g_bnoHeader[0]);
    totalLen &= (uint16_t)~0x8000u;
    if (totalLen < 4u) {
        g_bnoLastRxFailCode = 2u;
        return 0u;
    }

    len = (uint16_t)(totalLen - 4u);
    if (len == 0u || len > BNO085_PACKET_MAX) {
        g_bnoLastRxFailCode = 3u;
        return 0u;
    }

    if (!BNO085_ReadBlock(raw, (uint16_t)(len + 4u))) {
        g_bnoLastRxFailCode = 4u;
        return 0u;
    }

    memset(g_bnoData, 0, sizeof(g_bnoData));
    for (i = 0; i < len; i++) {
        g_bnoData[i] = raw[i + 4u];
    }
    g_bnoLastRxFailCode = 0u;
    g_bnoRxPacketCount++;

    if (channel) *channel = g_bnoHeader[2];
    if (payloadLen) *payloadLen = len;
    return 1u;
}

static void BNO085_UpdateEuler(ICM42688_Data_t *data) {
    float q0 = data->q0;
    float q1 = data->q1;
    float q2 = data->q2;
    float q3 = data->q3;

    g_bnoRawYaw = -atan2f(2.0f * q1 * q2 + 2.0f * q0 * q3,
                          -2.0f * q2 * q2 - 2.0f * q3 * q3 + 1.0f) * BNO085_RAD2DEG;
    data->yaw = BNO085_WrapDeg(g_bnoRawYaw - g_bnoYawBase);
    data->pitch = -asinf(ICM42688_ClampFloat(-2.0f * q1 * q3 + 2.0f * q0 * q2, -1.0f, 1.0f)) * BNO085_RAD2DEG;
    data->roll = atan2f(2.0f * q2 * q3 + 2.0f * q0 * q1,
                        -2.0f * q1 * q1 - 2.0f * q2 * q2 + 1.0f) * BNO085_RAD2DEG;
}

static uint8_t BNO085_ParseSensorReport(ICM42688_Data_t *data, uint16_t len) {
    uint8_t reportId;

    if (len < 9u) return 0u;
    if (g_bnoData[0] != BNO085_REPORT_BASE_TIMESTAMP) return 0u;

    reportId = g_bnoData[5];
    g_bnoLastReportId = reportId;

    if (reportId == BNO085_SENSOR_ACCELEROMETER && len >= 15u) {
        data->accelXf = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[9]), BNO085_Q_ACCEL);
        data->accelYf = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[11]), BNO085_Q_ACCEL);
        data->accelZf = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[13]), BNO085_Q_ACCEL);
        data->accelX = (int16_t)(data->accelXf * 1000.0f);
        data->accelY = (int16_t)(data->accelYf * 1000.0f);
        data->accelZ = (int16_t)(data->accelZf * 1000.0f);
        return 1u;
    } else if (reportId == BNO085_SENSOR_GYROSCOPE && len >= 15u) {
        data->gyroXf = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[9]), BNO085_Q_GYRO) * BNO085_RAD2DEG;
        data->gyroYf = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[11]), BNO085_Q_GYRO) * BNO085_RAD2DEG;
        data->gyroZf = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[13]), BNO085_Q_GYRO) * BNO085_RAD2DEG;
        data->gyroX = (int16_t)(data->gyroXf * 100.0f);
        data->gyroY = (int16_t)(data->gyroYf * 100.0f);
        data->gyroZ = (int16_t)(data->gyroZf * 100.0f);
        return 1u;
    } else if ((reportId == BNO085_SENSOR_ROTATION_VECTOR || reportId == BNO085_SENSOR_GAME_ROTATION_VECTOR) && len >= 17u) {
        data->q1 = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[9]), BNO085_Q_ROTATION);
        data->q2 = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[11]), BNO085_Q_ROTATION);
        data->q3 = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[13]), BNO085_Q_ROTATION);
        data->q0 = BNO085_QToFloat(BNO085_ReadS16(&g_bnoData[15]), BNO085_Q_ROTATION);
        data->ahrsInited = 1u;
        data->yawSampleUpdated = 1u;
        BNO085_UpdateEuler(data);
        return 1u;
    }

    return 0u;
}

static uint8_t BNO085_RequestProductId(void) {
    uint8_t req[2];
    uint8_t channel;
    uint16_t len;
    uint16_t retry;
    uint8_t sawPacket = 0u;

    req[0] = BNO085_REPORT_PRODUCT_ID_REQUEST;
    req[1] = 0u;
    BNO085_FlushInput(BNO085_FLUSH_ROUNDS);
    Delay_ms(20);
    BNO085_Trace("BI:PIDS\r\n");
    if (!BNO085_SendPacket(BNO085_CHANNEL_CONTROL, req, 2u)) {
        BNO085_Trace("BI:FPIDS\r\n");
        return 0u;
    }

    for (retry = 0u; retry < BNO085_PRODUCT_ID_TIMEOUT_MS; retry++) {
        if (BNO085_ReceivePacket(&channel, &len)) {
            if (!sawPacket) {
                sawPacket = 1u;
                BNO085_Trace("BI:PIDPK\r\n");
            }
            g_bnoProbeWho = g_bnoData[0];
            if (channel == BNO085_CHANNEL_CONTROL && len >= 2u && g_bnoData[0] == BNO085_REPORT_PRODUCT_ID_RESPONSE) {
                BNO085_Trace("BI:PIDR\r\n");
                g_icmWho = 0x85u;
                return 1u;
            }
        }
        Delay_ms(1);
    }

    BNO085_Trace("BI:TPID\r\n");
    return 0u;
}

static uint8_t BNO085_EnableReport(uint8_t reportId, uint32_t intervalUs) {
    uint8_t payload[17];

    memset(payload, 0, sizeof(payload));
    payload[0] = BNO085_REPORT_SET_FEATURE_COMMAND;
    payload[1] = reportId;
    payload[5] = (uint8_t)(intervalUs & 0xFFu);
    payload[6] = (uint8_t)((intervalUs >> 8) & 0xFFu);
    payload[7] = (uint8_t)((intervalUs >> 16) & 0xFFu);
    payload[8] = (uint8_t)((intervalUs >> 24) & 0xFFu);

    return BNO085_SendPacket(BNO085_CHANNEL_CONTROL, payload, 17u);
}

static uint8_t BNO085_WaitForFirstReport(ICM42688_Data_t *data, uint16_t timeoutMs) {
    uint16_t t;
    uint32_t startAttemptCount = g_bnoRxAttemptCount;
    uint32_t startRxCount = g_bnoRxPacketCount;
    uint8_t sawPacket = 0u;
    uint8_t sawAttempt = 0u;
    char traceBuf[48];

    for (t = 0u; t < timeoutMs; t++) {
        if (ICM42688_ReadAll(data) && data->ahrsInited) {
            BNO085_Trace("BI:RPT\r\n");
            return 1u;
        }
        if (!sawAttempt && g_bnoRxAttemptCount != startAttemptCount) {
            sawAttempt = 1u;
        }
        if (!sawPacket && g_bnoRxPacketCount != startRxCount) {
            sawPacket = 1u;
            BNO085_Trace("BI:WFPK\r\n");
        }
        Delay_ms(1);
    }

    if (sawPacket) {
        snprintf(traceBuf, sizeof(traceBuf), "BI:WFNP ch=%u rid=0x%02X len=%u\r\n",
                 (unsigned)g_bnoLastChannel,
                 (unsigned)g_bnoLastReportId,
                 (unsigned)g_bnoLastPayloadLen);
        BNO085_Trace(traceBuf);
    } else if (sawAttempt) {
        snprintf(traceBuf, sizeof(traceBuf), "BI:WFRA c=%u h=%02X%02X%02X%02X\r\n",
                 (unsigned)g_bnoLastRxFailCode,
                 (unsigned)g_bnoHeader[0],
                 (unsigned)g_bnoHeader[1],
                 (unsigned)g_bnoHeader[2],
                 (unsigned)g_bnoHeader[3]);
        BNO085_Trace(traceBuf);
    } else {
        BNO085_Trace("BI:WFNO\r\n");
    }
    return 0u;
}

static void BNO085_TryRecover(ICM42688_Data_t *data) {
    if (g_bnoInitBusy) return;

    if (data) {
        data->ahrsInited = 0u;
        data->yaw = 0.0f;
        data->yawRate = 0.0f;
        data->prevYaw = 0.0f;
        data->yawRateValid = 0u;
        data->yawSampleUpdated = 0u;
    }

    ICM42688_Init();
}

static uint8_t BNO085_AttemptInitAtAddr(uint8_t addr, uint8_t doReset, uint16_t presentTimeoutMs, ICM42688_Data_t *tempData) {
    g_icmAddr = addr;
    g_bnoLastProbeAddr = addr;
    g_bnoInitStage = BNO085_INIT_STAGE_PROBE;
    memset(g_bnoSeq, 0, sizeof(g_bnoSeq));
    g_bnoReady = 0u;
    g_icmWho = 0u;

    if (doReset) {
        BNO085_Trace("BI:HR\r\n");
        BNO085_HardwareReset();
        Delay_ms(BNO085_BOOT_DELAY_MS);
    } else {
        BNO085_Trace("BI:WTN\r\n");
        Delay_ms(BNO085_NO_RESET_BOOT_WAIT_MS);
    }

    BNO085_Trace("BI:PRE\r\n");
    if (!BNO085_WaitPresent(presentTimeoutMs)) {
        BNO085_Trace("BI:FPRE\r\n");
        g_icmAddr = 0u;
        return 0u;
    }

    if (!BNO085_WaitPresentStable((uint16_t)(BNO085_PRESENT_STABLE_COUNT * BNO085_PRESENT_STABLE_GAP_MS * 6u),
                                   BNO085_PRESENT_STABLE_COUNT)) {
        BNO085_Trace("BI:FPRS\r\n");
        g_icmAddr = 0u;
        return 0u;
    }

    g_bnoInitStage = BNO085_INIT_STAGE_PRESENT;
    g_bnoProbeWho = 0u;
    g_bnoInitStage = BNO085_INIT_STAGE_PRODUCT_ID;
    BNO085_Trace("BI:PID\r\n");
    if (!BNO085_RequestProductId()) {
        BNO085_Trace("BI:FPID\r\n");
    }

    g_bnoInitStage = BNO085_INIT_STAGE_FEATURE;
    BNO085_Trace("BI:FTR\r\n");
    if (!BNO085_EnableReport(BNO085_SENSOR_GAME_ROTATION_VECTOR, BNO085_REPORT_INTERVAL_US)) {
        BNO085_Trace("BI:FFTR\r\n");
        g_icmAddr = 0u;
        return 0u;
    }

    g_bnoReady = 1u;
    g_bnoInitStage = BNO085_INIT_STAGE_WAIT_REPORT;
    BNO085_Trace("BI:WFR\r\n");
    if (!BNO085_WaitForFirstReport(tempData, BNO085_FIRST_REPORT_TIMEOUT_MS)) {
        BNO085_Trace("BI:FWFR\r\n");
        g_bnoReady = 0u;
        g_icmWho = 0u;
        g_icmAddr = 0u;
        return 0u;
    }

    g_icmWho = 0x85u;
    g_bnoInitStage = BNO085_INIT_STAGE_READY;
    BNO085_Trace("BI:RDY\r\n");
    return 1u;
}

static uint8_t ICM42688_WriteReg(uint8_t reg, uint8_t data) {
    ICM_I2C_Start();
    ICM_I2C_SendByte(g_icmAddr << 1);
    if (ICM_I2C_WaitAck()) { ICM_I2C_Stop(); return 0; }
    ICM_I2C_SendByte(reg);
    if (ICM_I2C_WaitAck()) { ICM_I2C_Stop(); return 0; }
    ICM_I2C_SendByte(data);
    if (ICM_I2C_WaitAck()) { ICM_I2C_Stop(); return 0; }
    ICM_I2C_Stop();
    return 1;
}

static uint8_t ICM42688_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
    uint8_t i;
    if (len == 0) return 0;

    ICM_I2C_Start();
    ICM_I2C_SendByte(g_icmAddr << 1);
    if (ICM_I2C_WaitAck()) { ICM_I2C_Stop(); return 0; }
    ICM_I2C_SendByte(reg);
    if (ICM_I2C_WaitAck()) { ICM_I2C_Stop(); return 0; }

    ICM_I2C_Start();
    ICM_I2C_SendByte((g_icmAddr << 1) | 0x01);
    if (ICM_I2C_WaitAck()) { ICM_I2C_Stop(); return 0; }

    for (i = 0; i < (uint8_t)(len - 1); i++) {
        buf[i] = ICM_I2C_RecvByte();
        ICM_I2C_SendAck();
    }
    buf[len - 1] = ICM_I2C_RecvByte();
    ICM_I2C_SendNack();
    ICM_I2C_Stop();
    return 1;
}

void ICM42688_Init(void) {
    static const uint8_t addrList[] = {
        BNO085_ADDR_ALT,
        BNO085_ADDR_DEFAULT,
        BNO085_ADDR_DOC_ALT,
        BNO085_ADDR_DOC_DEFAULT
    };
    GPIO_InitTypeDef g;
    uint8_t i;
    ICM42688_Data_t tempData;

    g_bnoInitBusy = 1u;
    BNO085_Trace("BI:INI\r\n");

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    g.GPIO_Pin = ICM_I2C_SCL_PIN | ICM_I2C_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ICM_I2C_PORT, &g);

    g.GPIO_Pin = BNO085_RESET_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BNO085_RESET_PORT, &g);

    g.GPIO_Pin = BNO085_INT_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BNO085_INT_PORT, &g);

    GPIO_SetBits(BNO085_RESET_PORT, BNO085_RESET_PIN);
    ICM_SCL_H();
    ICM_SDA_H();

    memset(g_bnoSeq, 0, sizeof(g_bnoSeq));
    memset(g_bnoHeader, 0, sizeof(g_bnoHeader));
    memset(g_bnoData, 0, sizeof(g_bnoData));
    g_bnoYawBase = 0.0f;
    g_bnoRawYaw = 0.0f;
    g_bnoReady = 0u;
    g_bnoLastProbeAddr = 0u;
    g_bnoInitStage = BNO085_INIT_STAGE_IDLE;
    g_bnoProbeWho = 0u;
    g_bnoScanFirstAddr = 0u;
    g_bnoScanLastAddr = 0u;
    g_bnoScanHitCount = 0u;
    g_icmWho = 0u;
    g_icmAddr = 0u;
    g_bnoLastChannel = 0u;
    g_bnoLastReportId = 0u;
    g_bnoLastPayloadLen = 0u;
    g_bnoReadFailStreak = 0u;
    memset(&tempData, 0, sizeof(tempData));

    for (i = 0; i < (uint8_t)(sizeof(addrList) / sizeof(addrList[0])); i++) {
        if (BNO085_AttemptInitAtAddr(addrList[i], 0u, BNO085_PRESENT_TIMEOUT_MS, &tempData)) {
            break;
        }
    }

    if (g_icmWho != 0x85u) {
        BNO085_Trace("BI:SCN\r\n");
        BNO085_ScanBus();
        if (g_bnoScanHitCount > 0u) {
            BNO085_Trace("BI:SHIT\r\n");
            if (!BNO085_AttemptInitAtAddr(g_bnoScanFirstAddr, 0u, 150u, &tempData)) {
                if (g_bnoScanLastAddr != g_bnoScanFirstAddr) {
                    (void)BNO085_AttemptInitAtAddr(g_bnoScanLastAddr, 0u, 150u, &tempData);
                }
            }
        }
    }

    if (g_icmWho != 0x85u) {
        for (i = 0; i < (uint8_t)(sizeof(addrList) / sizeof(addrList[0])); i++) {
            if (BNO085_AttemptInitAtAddr(addrList[i], 1u, BNO085_PRESENT_TIMEOUT_MS, &tempData)) {
                break;
            }
        }
    }

    g_bnoInitBusy = 0u;
}

uint8_t ICM42688_GetWhoAmI(void) {
    return g_icmWho;
}

uint8_t ICM42688_GetI2CAddr(void) {
    return g_icmAddr;
}

uint8_t ICM42688_GetLastProbeAddr(void) {
    return g_bnoLastProbeAddr;
}

uint8_t ICM42688_GetInitStage(void) {
    return g_bnoInitStage;
}

uint8_t ICM42688_GetProbeWhoAmI(void) {
    return g_bnoProbeWho;
}

uint8_t ICM42688_GetScanFirstAddr(void) {
    return g_bnoScanFirstAddr;
}

uint8_t ICM42688_GetScanLastAddr(void) {
    return g_bnoScanLastAddr;
}

uint8_t ICM42688_GetScanHitCount(void) {
    return g_bnoScanHitCount;
}

uint8_t ICM42688_GetLastTxChannel(void) {
    return g_bnoLastTxChannel;
}

uint16_t ICM42688_GetLastTxPacketLen(void) {
    return g_bnoLastTxPacketLen;
}

uint16_t ICM42688_GetLastWriteFailIndex(void) {
    return g_bnoLastWriteFailIndex;
}

uint8_t ICM42688_ReadAll(ICM42688_Data_t *data) {
    uint8_t channel;
    uint16_t len;
    uint8_t got = 0u;
    uint8_t parsed = 0u;
    uint8_t i;

    if (data) {
        data->yawSampleUpdated = 0u;
    }

    if (!g_bnoReady) {
        if (!g_bnoInitBusy) {
            if (g_bnoReadFailStreak < 0xFFFFu) g_bnoReadFailStreak++;
            if (g_bnoReadFailStreak >= BNO085_RECOVER_NOT_READY_FAILS) {
                g_bnoReadFailStreak = 0u;
                BNO085_TryRecover(data);
            }
        }
        return 0u;
    }

    for (i = 0; i < 6u; i++) {
        if (!BNO085_ReceivePacket(&channel, &len)) break;
        got = 1u;
        g_bnoLastChannel = channel;
        g_bnoLastPayloadLen = len;
        if (channel == BNO085_CHANNEL_REPORTS || channel == BNO085_CHANNEL_WAKE_REPORTS) {
            parsed |= BNO085_ParseSensorReport(data, len);
        }
    }

    if (parsed || (data->ahrsInited && !got)) {
        g_bnoReadFailStreak = 0u;
        return 1u;
    }

    if (!g_bnoInitBusy) {
        if (g_bnoReadFailStreak < 0xFFFFu) g_bnoReadFailStreak++;
        if (g_bnoReadFailStreak >= BNO085_RECOVER_STALL_FAILS) {
            g_bnoReadFailStreak = 0u;
            g_bnoReady = 0u;
            g_icmWho = 0u;
            g_bnoInitStage = BNO085_INIT_STAGE_PROBE;
            BNO085_TryRecover(data);
        }
    }

    return parsed ? 1u : 0u;
}

uint8_t ICM42688_GetLastChannel(void) {
    return g_bnoLastChannel;
}

uint8_t ICM42688_GetLastReportId(void) {
    return g_bnoLastReportId;
}

uint16_t ICM42688_GetLastPayloadLen(void) {
    return g_bnoLastPayloadLen;
}

uint8_t ICM42688_DiagProbeAddr(uint8_t addr) {
    uint8_t savedAddr = g_icmAddr;
    uint8_t present;

    g_icmAddr = addr;
    present = BNO085_DevicePresent();
    g_icmAddr = savedAddr;
    return present;
}

void ICM42688_DiagPinsInit(void) {
    GPIO_InitTypeDef g;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    g.GPIO_Pin = BNO085_INT_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BNO085_INT_PORT, &g);
}

uint8_t ICM42688_GetSclLevel(void) {
    return (GPIO_ReadInputDataBit(ICM_I2C_PORT, ICM_I2C_SCL_PIN) == Bit_SET) ? 1u : 0u;
}

uint8_t ICM42688_GetSdaLevel(void) {
    return (GPIO_ReadInputDataBit(ICM_I2C_PORT, ICM_I2C_SDA_PIN) == Bit_SET) ? 1u : 0u;
}

uint8_t ICM42688_GetResetLevel(void) {
    return (GPIO_ReadInputDataBit(BNO085_RESET_PORT, BNO085_RESET_PIN) == Bit_SET) ? 1u : 0u;
}

uint8_t ICM42688_GetIntLevel(void) {
    return (GPIO_ReadInputDataBit(BNO085_INT_PORT, BNO085_INT_PIN) == Bit_SET) ? 1u : 0u;
}

void ICM42688_Calibrate(ICM42688_Data_t *data, uint16_t samples) {
    float sumX = 0.0f;
    float sumY = 0.0f;
    float sumZ = 0.0f;
    uint16_t ok = 0;
    uint16_t i;

    for (i = 0; i < samples; i++) {
        if (!ICM42688_ReadAll(data)) {
            Delay_ms(5);
            continue;
        }
        sumX += data->gyroXf;
        sumY += data->gyroYf;
        sumZ += data->gyroZf;
        ok++;
        Delay_ms(5);
    }

    if (ok > 0u) {
        data->gyroXOffset = (float)sumX / ok;
        data->gyroYOffset = (float)sumY / ok;
        data->gyroZOffset = (float)sumZ / ok;
    } else {
        data->gyroXOffset = 0.0f;
        data->gyroYOffset = 0.0f;
        data->gyroZOffset = 0.0f;
    }
    ICM42688_ResetAttitude(data);
}

void ICM42688_ResetAttitude(ICM42688_Data_t *data) {
    g_bnoYawBase = g_bnoRawYaw;
    data->exInt = 0.0f;
    data->eyInt = 0.0f;
    data->ezInt = 0.0f;
    data->yaw = 0.0f;
    data->prevYaw = 0.0f;
    data->yawRate = 0.0f;
    data->yawRateValid = 0u;
    data->yawSampleUpdated = 0u;
}

void ICM42688_SetBiasTrackEnabled(uint8_t enabled) {
    g_biasTrackEnabled = enabled ? 1u : 0u;
}

void ICM42688_UpdateYaw(ICM42688_Data_t *data, float dt) {
    (void)g_biasTrackEnabled;

    float newYaw;
    float dy;
    float yawRate;

    if (!data->ahrsInited) return;

    newYaw = BNO085_WrapDeg(g_bnoRawYaw - g_bnoYawBase);
    if (data->yawRateValid && dt > 0.0f) {
        dy = newYaw - data->prevYaw;
        while (dy > 180.0f) dy -= 360.0f;
        while (dy < -180.0f) dy += 360.0f;
        yawRate = dy / dt;
        if (fabsf(dy) > BNO085_YAW_JUMP_REJECT_DEG || fabsf(yawRate) > BNO085_YAW_RATE_LIMIT_DPS) {
            float maxStep = BNO085_YAW_RATE_LIMIT_DPS * dt;
            if (maxStep <= 0.0f) {
                maxStep = BNO085_YAW_JUMP_REJECT_DEG;
            }
            if (maxStep > BNO085_YAW_JUMP_REJECT_DEG) {
                maxStep = BNO085_YAW_JUMP_REJECT_DEG;
            }
            if (dy > maxStep) dy = maxStep;
            if (dy < -maxStep) dy = -maxStep;
            newYaw = BNO085_WrapDeg(data->prevYaw + dy);
            yawRate = dy / dt;
        }
        if (yawRate > BNO085_YAW_RATE_LIMIT_DPS) yawRate = BNO085_YAW_RATE_LIMIT_DPS;
        if (yawRate < -BNO085_YAW_RATE_LIMIT_DPS) yawRate = -BNO085_YAW_RATE_LIMIT_DPS;
        data->yawRate += (yawRate - data->yawRate) * BNO085_YAW_RATE_LPF_ALPHA;
    } else {
        data->yawRate = 0.0f;
        data->yawRateValid = 1u;
    }
    data->yaw = newYaw;
    data->prevYaw = newYaw;
}

float ICM42688_GetYawError(float targetYaw, float currentYaw) {
    float e = targetYaw - currentYaw;
    while (e > 180.0f) e -= 360.0f;
    while (e < -180.0f) e += 360.0f;
    return e;
}
