#include "DrvImuBno08x.h"
#include "BoardConfig.h"
#include "Delay.h"
#include <string.h>
#include <math.h>

#define DRV_IMU_ADDR_4A                     0x4Au
#define DRV_IMU_ADDR_4B                     0x4Bu
#define DRV_IMU_ADDR_28                     0x28u
#define DRV_IMU_ADDR_29                     0x29u

#define DRV_IMU_CHANNEL_CONTROL             2u
#define DRV_IMU_CHANNEL_REPORTS             3u
#define DRV_IMU_CHANNEL_WAKE_REPORTS        4u

#define DRV_IMU_REPORT_PRODUCT_ID_REQUEST   0xF9u
#define DRV_IMU_REPORT_PRODUCT_ID_RESPONSE  0xF8u
#define DRV_IMU_REPORT_BASE_TIMESTAMP       0xFBu
#define DRV_IMU_REPORT_SET_FEATURE          0xFDu

#define DRV_IMU_SENSOR_ACCEL                0x01u
#define DRV_IMU_SENSOR_GYRO                 0x02u
#define DRV_IMU_SENSOR_ROTATION             0x05u
#define DRV_IMU_SENSOR_GAME_ROTATION        0x08u

#define DRV_IMU_Q_ACCEL                     8u
#define DRV_IMU_Q_GYRO                      9u
#define DRV_IMU_Q_ROTATION                  14u

#define DRV_IMU_PACKET_MAX                  384u
#define DRV_IMU_REPORT_INTERVAL_US          20000UL
#define DRV_IMU_ACK_TIMEOUT                 1200u
#define DRV_IMU_SCL_TIMEOUT                 2400u
#define DRV_IMU_BOOT_DELAY_MS               80u
#define DRV_IMU_NO_RESET_WAIT_MS            250u
#define DRV_IMU_PRESENT_TIMEOUT_MS          1000u
#define DRV_IMU_PRODUCT_ID_TIMEOUT_MS       1000u
#define DRV_IMU_FIRST_REPORT_TIMEOUT_MS     1500u
#define DRV_IMU_RECOVER_FAILS               40u

#define DRV_IMU_STAGE_IDLE                  0u
#define DRV_IMU_STAGE_PROBE                 1u
#define DRV_IMU_STAGE_PRESENT               2u
#define DRV_IMU_STAGE_PRODUCT_ID            3u
#define DRV_IMU_STAGE_FEATURE               4u
#define DRV_IMU_STAGE_WAIT_REPORT           5u
#define DRV_IMU_STAGE_READY                 6u

#define DRV_IMU_RAD2DEG                     57.2957795130823208768f
#define DRV_IMU_YAW_JUMP_REJECT_DEG         45.0f
#define DRV_IMU_YAW_RATE_LIMIT_DPS          180.0f
#define DRV_IMU_YAW_RATE_LPF_ALPHA          0.35f

#define IMU_SCL_H()                         GPIO_SetBits(BOARD_IMU_SCL_PORT, BOARD_IMU_SCL_PIN)
#define IMU_SCL_L()                         GPIO_ResetBits(BOARD_IMU_SCL_PORT, BOARD_IMU_SCL_PIN)
#define IMU_SCL_READ()                      GPIO_ReadInputDataBit(BOARD_IMU_SCL_PORT, BOARD_IMU_SCL_PIN)
#define IMU_SDA_H()                         GPIO_SetBits(BOARD_IMU_SDA_PORT, BOARD_IMU_SDA_PIN)
#define IMU_SDA_L()                         GPIO_ResetBits(BOARD_IMU_SDA_PORT, BOARD_IMU_SDA_PIN)
#define IMU_SDA_READ()                      GPIO_ReadInputDataBit(BOARD_IMU_SDA_PORT, BOARD_IMU_SDA_PIN)

static uint8_t s_addr = 0u;
static uint8_t s_ready = 0u;
static uint8_t s_initBusy = 0u;
static uint8_t s_initStage = DRV_IMU_STAGE_IDLE;
static uint8_t s_seq[6];
static uint8_t s_header[4];
static uint8_t s_data[DRV_IMU_PACKET_MAX];
static float s_yawBase = 0.0f;
static float s_rawYaw = 0.0f;
static uint16_t s_readFailStreak = 0u;
static uint8_t s_biasTrackEnabled = 1u;

static void drv_imu_delay_cycles(void)
{
    uint16_t i;
    for (i = 0u; i < 255u; i++) {
        __NOP();
    }
}

static uint8_t drv_imu_wait_scl_high(void)
{
    uint16_t timeout = 0u;

    while (IMU_SCL_READ() == Bit_RESET) {
        if (++timeout >= DRV_IMU_SCL_TIMEOUT) {
            return 0u;
        }
        drv_imu_delay_cycles();
    }

    return 1u;
}

static void drv_imu_sda_out(void)
{
    GPIO_InitTypeDef gpio;

    gpio.GPIO_Pin = BOARD_IMU_SDA_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_IMU_SDA_PORT, &gpio);
}

static void drv_imu_sda_in(void)
{
    GPIO_InitTypeDef gpio;

    gpio.GPIO_Pin = BOARD_IMU_SDA_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_IMU_SDA_PORT, &gpio);
}

static void drv_imu_start(void)
{
    drv_imu_sda_out();
    IMU_SDA_H();
    IMU_SCL_H();
    (void)drv_imu_wait_scl_high();
    drv_imu_delay_cycles();
    IMU_SDA_L();
    drv_imu_delay_cycles();
    IMU_SCL_L();
}

static void drv_imu_stop(void)
{
    drv_imu_sda_out();
    IMU_SCL_L();
    IMU_SDA_L();
    drv_imu_delay_cycles();
    IMU_SCL_H();
    (void)drv_imu_wait_scl_high();
    drv_imu_delay_cycles();
    IMU_SDA_H();
    drv_imu_delay_cycles();
}

static void drv_imu_send_byte(uint8_t data)
{
    uint8_t i;

    drv_imu_sda_out();
    IMU_SCL_L();

    for (i = 0u; i < 8u; i++) {
        if ((data & 0x80u) != 0u) {
            IMU_SDA_H();
        } else {
            IMU_SDA_L();
        }
        data <<= 1;
        drv_imu_delay_cycles();
        IMU_SCL_H();
        if (drv_imu_wait_scl_high() == 0u) {
            IMU_SCL_L();
            return;
        }
        drv_imu_delay_cycles();
        IMU_SCL_L();
        drv_imu_delay_cycles();
    }
}

static uint8_t drv_imu_wait_ack(void)
{
    uint16_t timeout = 0u;

    drv_imu_sda_in();
    IMU_SDA_H();
    IMU_SCL_H();
    if (drv_imu_wait_scl_high() == 0u) {
        IMU_SCL_L();
        drv_imu_sda_out();
        return 1u;
    }
    drv_imu_delay_cycles();

    while (IMU_SDA_READ() != Bit_RESET) {
        if (++timeout >= DRV_IMU_ACK_TIMEOUT) {
            IMU_SCL_L();
            drv_imu_sda_out();
            return 1u;
        }
        drv_imu_delay_cycles();
    }

    IMU_SCL_L();
    drv_imu_delay_cycles();
    drv_imu_sda_out();
    return 0u;
}

static void drv_imu_send_ack(void)
{
    drv_imu_sda_out();
    IMU_SDA_L();
    IMU_SCL_H();
    (void)drv_imu_wait_scl_high();
    drv_imu_delay_cycles();
    IMU_SCL_L();
}

static void drv_imu_send_nack(void)
{
    drv_imu_sda_out();
    IMU_SDA_H();
    IMU_SCL_H();
    (void)drv_imu_wait_scl_high();
    drv_imu_delay_cycles();
    IMU_SCL_L();
}

static uint8_t drv_imu_recv_byte(void)
{
    uint8_t i;
    uint8_t data = 0u;

    drv_imu_sda_in();
    for (i = 0u; i < 8u; i++) {
        IMU_SCL_L();
        drv_imu_delay_cycles();
        IMU_SCL_H();
        if (drv_imu_wait_scl_high() == 0u) {
            IMU_SCL_L();
            return data;
        }
        data <<= 1;
        if (IMU_SDA_READ() != Bit_RESET) {
            data |= 0x01u;
        }
        drv_imu_delay_cycles();
    }
    IMU_SCL_L();
    return data;
}

static float drv_imu_wrap_deg(float deg)
{
    while (deg > 180.0f) {
        deg -= 360.0f;
    }
    while (deg < -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

static int16_t drv_imu_read_s16(const uint8_t *ptr)
{
    return (int16_t)(((uint16_t)ptr[1] << 8) | ptr[0]);
}

static float drv_imu_q_to_float(int16_t value, uint8_t q)
{
    return (float)value / (float)(1u << q);
}

static void drv_imu_update_euler(DrvImuBno08xData_t *data)
{
    float q0 = data->q0;
    float q1 = data->q1;
    float q2 = data->q2;
    float q3 = data->q3;
    float pitchTerm;

    /* Keep yaw positive in the same steering direction as the chassis model. */
    s_rawYaw = atan2f(2.0f * q1 * q2 + 2.0f * q0 * q3,
                      -2.0f * q2 * q2 - 2.0f * q3 * q3 + 1.0f) * DRV_IMU_RAD2DEG;
    data->yaw = drv_imu_wrap_deg(s_rawYaw - s_yawBase);

    pitchTerm = -2.0f * q1 * q3 + 2.0f * q0 * q2;
    if (pitchTerm > 1.0f) {
        pitchTerm = 1.0f;
    }
    if (pitchTerm < -1.0f) {
        pitchTerm = -1.0f;
    }
    data->pitch = -asinf(pitchTerm) * DRV_IMU_RAD2DEG;
    data->roll = atan2f(2.0f * q2 * q3 + 2.0f * q0 * q1,
                        -2.0f * q1 * q1 - 2.0f * q2 * q2 + 1.0f) * DRV_IMU_RAD2DEG;
}

static uint8_t drv_imu_write_bytes(const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    drv_imu_start();
    drv_imu_send_byte((uint8_t)(s_addr << 1));
    if (drv_imu_wait_ack() != 0u) {
        drv_imu_stop();
        return 0u;
    }

    for (i = 0u; i < len; i++) {
        drv_imu_send_byte(buf[i]);
        if (drv_imu_wait_ack() != 0u) {
            drv_imu_stop();
            return 0u;
        }
    }

    drv_imu_stop();
    return 1u;
}

static uint8_t drv_imu_read_block(uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (len == 0u) {
        return 0u;
    }

    drv_imu_start();
    drv_imu_send_byte((uint8_t)((s_addr << 1) | 0x01u));
    if (drv_imu_wait_ack() != 0u) {
        drv_imu_stop();
        return 0u;
    }

    for (i = 0u; i < len; i++) {
        buf[i] = drv_imu_recv_byte();
        if ((i + 1u) < len) {
            drv_imu_send_ack();
        } else {
            drv_imu_send_nack();
        }
    }

    drv_imu_stop();
    return 1u;
}

static uint8_t drv_imu_device_present_at(uint8_t addr)
{
    uint8_t oldAddr = s_addr;
    uint8_t ok = 0u;

    s_addr = addr;
    drv_imu_start();
    drv_imu_send_byte((uint8_t)(s_addr << 1));
    if (drv_imu_wait_ack() == 0u) {
        ok = 1u;
    }
    drv_imu_stop();
    s_addr = oldAddr;
    return ok;
}

static uint8_t drv_imu_wait_present(uint8_t addr, uint16_t timeoutMs)
{
    uint16_t waited = 0u;

    while (waited < timeoutMs) {
        if (drv_imu_device_present_at(addr) != 0u) {
            return 1u;
        }
        Delay_ms(5u);
        waited = (uint16_t)(waited + 5u);
    }

    return 0u;
}

static void drv_imu_hardware_reset(void)
{
    GPIO_ResetBits(BOARD_IMU_RESET_PORT, BOARD_IMU_RESET_PIN);
    Delay_ms(10u);
    GPIO_SetBits(BOARD_IMU_RESET_PORT, BOARD_IMU_RESET_PIN);
    Delay_ms(DRV_IMU_BOOT_DELAY_MS);
}

static uint8_t drv_imu_send_packet(uint8_t channel, const uint8_t *payload, uint8_t payloadLen)
{
    uint8_t packet[DRV_IMU_PACKET_MAX];
    uint16_t packetLen;
    uint8_t i;

    packetLen = (uint16_t)payloadLen + 4u;
    if (packetLen > DRV_IMU_PACKET_MAX) {
        return 0u;
    }

    packet[0] = (uint8_t)(packetLen & 0xFFu);
    packet[1] = (uint8_t)((packetLen >> 8) & 0xFFu);
    packet[2] = channel;
    packet[3] = s_seq[channel]++;
    for (i = 0u; i < payloadLen; i++) {
        packet[4u + i] = payload[i];
    }

    return drv_imu_write_bytes(packet, packetLen);
}

static uint8_t drv_imu_receive_packet(uint8_t *channel, uint16_t *payloadLen)
{
    uint16_t totalLen;
    uint16_t len;
    uint16_t i;
    uint8_t raw[DRV_IMU_PACKET_MAX + 4u];

    if (GPIO_ReadInputDataBit(BOARD_IMU_INT_PORT, BOARD_IMU_INT_PIN) != Bit_RESET) {
        return 0u;
    }

    memset(s_header, 0, sizeof(s_header));
    if (drv_imu_read_block(s_header, 4u) == 0u) {
        return 0u;
    }

    totalLen = (uint16_t)(((uint16_t)s_header[1] << 8) | s_header[0]);
    totalLen &= (uint16_t)~0x8000u;
    if (totalLen < 4u) {
        return 0u;
    }

    len = (uint16_t)(totalLen - 4u);
    if ((len == 0u) || (len > DRV_IMU_PACKET_MAX)) {
        return 0u;
    }

    if (drv_imu_read_block(raw, (uint16_t)(len + 4u)) == 0u) {
        return 0u;
    }

    memset(s_data, 0, sizeof(s_data));
    for (i = 0u; i < len; i++) {
        s_data[i] = raw[i + 4u];
    }

    if (channel != 0) {
        *channel = s_header[2];
    }
    if (payloadLen != 0) {
        *payloadLen = len;
    }

    return 1u;
}

static uint8_t drv_imu_request_product_id(void)
{
    uint8_t req[2];
    uint8_t channel;
    uint16_t len;
    uint16_t retry;

    req[0] = DRV_IMU_REPORT_PRODUCT_ID_REQUEST;
    req[1] = 0u;

    if (drv_imu_send_packet(DRV_IMU_CHANNEL_CONTROL, req, 2u) == 0u) {
        return 0u;
    }

    for (retry = 0u; retry < DRV_IMU_PRODUCT_ID_TIMEOUT_MS; retry++) {
        if (drv_imu_receive_packet(&channel, &len) != 0u) {
            if ((channel == DRV_IMU_CHANNEL_CONTROL) &&
                (len >= 2u) &&
                (s_data[0] == DRV_IMU_REPORT_PRODUCT_ID_RESPONSE)) {
                return 1u;
            }
        }
        Delay_ms(1u);
    }

    return 0u;
}

static uint8_t drv_imu_enable_report(uint8_t reportId, uint32_t intervalUs)
{
    uint8_t payload[17];

    memset(payload, 0, sizeof(payload));
    payload[0] = DRV_IMU_REPORT_SET_FEATURE;
    payload[1] = reportId;
    payload[5] = (uint8_t)(intervalUs & 0xFFu);
    payload[6] = (uint8_t)((intervalUs >> 8) & 0xFFu);
    payload[7] = (uint8_t)((intervalUs >> 16) & 0xFFu);
    payload[8] = (uint8_t)((intervalUs >> 24) & 0xFFu);

    return drv_imu_send_packet(DRV_IMU_CHANNEL_CONTROL, payload, 17u);
}

static uint8_t drv_imu_parse_report(DrvImuBno08xData_t *data, uint16_t len)
{
    uint8_t reportId;

    if ((data == 0) || (len < 9u)) {
        return 0u;
    }
    if (s_data[0] != DRV_IMU_REPORT_BASE_TIMESTAMP) {
        return 0u;
    }

    reportId = s_data[5];

    if ((reportId == DRV_IMU_SENSOR_ACCEL) && (len >= 15u)) {
        data->accelXf = drv_imu_q_to_float(drv_imu_read_s16(&s_data[9]), DRV_IMU_Q_ACCEL);
        data->accelYf = drv_imu_q_to_float(drv_imu_read_s16(&s_data[11]), DRV_IMU_Q_ACCEL);
        data->accelZf = drv_imu_q_to_float(drv_imu_read_s16(&s_data[13]), DRV_IMU_Q_ACCEL);
        data->accelX = (int16_t)(data->accelXf * 1000.0f);
        data->accelY = (int16_t)(data->accelYf * 1000.0f);
        data->accelZ = (int16_t)(data->accelZf * 1000.0f);
        return 1u;
    }

    if ((reportId == DRV_IMU_SENSOR_GYRO) && (len >= 15u)) {
        data->gyroXf = drv_imu_q_to_float(drv_imu_read_s16(&s_data[9]), DRV_IMU_Q_GYRO) * DRV_IMU_RAD2DEG;
        data->gyroYf = drv_imu_q_to_float(drv_imu_read_s16(&s_data[11]), DRV_IMU_Q_GYRO) * DRV_IMU_RAD2DEG;
        data->gyroZf = drv_imu_q_to_float(drv_imu_read_s16(&s_data[13]), DRV_IMU_Q_GYRO) * DRV_IMU_RAD2DEG;
        data->gyroX = (int16_t)(data->gyroXf * 100.0f);
        data->gyroY = (int16_t)(data->gyroYf * 100.0f);
        data->gyroZ = (int16_t)(data->gyroZf * 100.0f);
        return 1u;
    }

    if (((reportId == DRV_IMU_SENSOR_ROTATION) || (reportId == DRV_IMU_SENSOR_GAME_ROTATION)) && (len >= 17u)) {
        data->q1 = drv_imu_q_to_float(drv_imu_read_s16(&s_data[9]), DRV_IMU_Q_ROTATION);
        data->q2 = drv_imu_q_to_float(drv_imu_read_s16(&s_data[11]), DRV_IMU_Q_ROTATION);
        data->q3 = drv_imu_q_to_float(drv_imu_read_s16(&s_data[13]), DRV_IMU_Q_ROTATION);
        data->q0 = drv_imu_q_to_float(drv_imu_read_s16(&s_data[15]), DRV_IMU_Q_ROTATION);
        data->ahrsInited = 1u;
        data->yawSampleUpdated = 1u;
        drv_imu_update_euler(data);
        return 1u;
    }

    return 0u;
}

static uint8_t drv_imu_wait_first_report(DrvImuBno08xData_t *data)
{
    uint16_t t;

    for (t = 0u; t < DRV_IMU_FIRST_REPORT_TIMEOUT_MS; t++) {
        if ((DrvImuBno08x_Read(data) != 0u) && (data->ahrsInited != 0u)) {
            return 1u;
        }
        Delay_ms(1u);
    }

    return 0u;
}

static uint8_t drv_imu_attempt_init_at_addr(uint8_t addr, uint8_t doReset, DrvImuBno08xData_t *tempData)
{
    s_addr = addr;
    s_ready = 0u;
    memset(s_seq, 0, sizeof(s_seq));
    memset(s_header, 0, sizeof(s_header));
    memset(s_data, 0, sizeof(s_data));
    s_initStage = DRV_IMU_STAGE_PROBE;

    if (doReset != 0u) {
        drv_imu_hardware_reset();
    } else {
        Delay_ms(DRV_IMU_NO_RESET_WAIT_MS);
    }

    if (drv_imu_wait_present(addr, DRV_IMU_PRESENT_TIMEOUT_MS) == 0u) {
        s_addr = 0u;
        return 0u;
    }

    s_initStage = DRV_IMU_STAGE_PRESENT;
    s_initStage = DRV_IMU_STAGE_PRODUCT_ID;
    (void)drv_imu_request_product_id();

    s_initStage = DRV_IMU_STAGE_FEATURE;
    if (drv_imu_enable_report(DRV_IMU_SENSOR_GAME_ROTATION, DRV_IMU_REPORT_INTERVAL_US) == 0u) {
        s_addr = 0u;
        return 0u;
    }

    s_ready = 1u;
    s_initStage = DRV_IMU_STAGE_WAIT_REPORT;
    if (drv_imu_wait_first_report(tempData) == 0u) {
        s_ready = 0u;
        s_addr = 0u;
        return 0u;
    }

    s_initStage = DRV_IMU_STAGE_READY;
    s_readFailStreak = 0u;
    return 1u;
}

uint8_t DrvImuBno08x_Init(void)
{
    GPIO_InitTypeDef gpio;
    uint8_t i;
    DrvImuBno08xData_t tempData;
    static const uint8_t addrList[] = {
        DRV_IMU_ADDR_4A,
        DRV_IMU_ADDR_4B,
        DRV_IMU_ADDR_29,
        DRV_IMU_ADDR_28
    };

    s_initBusy = 1u;
    s_ready = 0u;
    s_addr = 0u;
    s_initStage = DRV_IMU_STAGE_IDLE;
    s_yawBase = 0.0f;
    s_rawYaw = 0.0f;
    s_readFailStreak = 0u;
    memset(s_seq, 0, sizeof(s_seq));
    memset(&tempData, 0, sizeof(tempData));

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    gpio.GPIO_Pin = BOARD_IMU_SCL_PIN | BOARD_IMU_SDA_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_IMU_SCL_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_IMU_RESET_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_IMU_RESET_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_IMU_INT_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_IMU_INT_PORT, &gpio);

    GPIO_SetBits(BOARD_IMU_RESET_PORT, BOARD_IMU_RESET_PIN);
    IMU_SCL_H();
    IMU_SDA_H();

    for (i = 0u; i < (uint8_t)(sizeof(addrList) / sizeof(addrList[0])); i++) {
        if (drv_imu_attempt_init_at_addr(addrList[i], 0u, &tempData) != 0u) {
            break;
        }
    }

    if (s_ready == 0u) {
        for (i = 0u; i < (uint8_t)(sizeof(addrList) / sizeof(addrList[0])); i++) {
            if (drv_imu_attempt_init_at_addr(addrList[i], 1u, &tempData) != 0u) {
                break;
            }
        }
    }

    s_initBusy = 0u;
    return s_ready;
}

uint8_t DrvImuBno08x_Read(DrvImuBno08xData_t *data)
{
    uint8_t channel;
    uint16_t len;
    uint8_t gotPacket = 0u;
    uint8_t parsed = 0u;
    uint8_t i;

    if (data == 0) {
        return 0u;
    }

    data->yawSampleUpdated = 0u;

    if (s_ready == 0u) {
        return 0u;
    }

    for (i = 0u; i < 6u; i++) {
        if (drv_imu_receive_packet(&channel, &len) == 0u) {
            break;
        }
        gotPacket = 1u;
        if ((channel == DRV_IMU_CHANNEL_REPORTS) || (channel == DRV_IMU_CHANNEL_WAKE_REPORTS)) {
            parsed |= drv_imu_parse_report(data, len);
        }
    }

    if ((parsed != 0u) || ((data->ahrsInited != 0u) && (gotPacket == 0u))) {
        s_readFailStreak = 0u;
        return 1u;
    }

    if ((s_initBusy == 0u) && (++s_readFailStreak >= DRV_IMU_RECOVER_FAILS)) {
        s_readFailStreak = 0u;
        s_ready = 0u;
        (void)DrvImuBno08x_Init();
    }

    return parsed;
}

void DrvImuBno08x_Calibrate(DrvImuBno08xData_t *data, uint16_t samples)
{
    float sumX = 0.0f;
    float sumY = 0.0f;
    float sumZ = 0.0f;
    uint16_t ok = 0u;
    uint16_t i;

    if (data == 0) {
        return;
    }

    for (i = 0u; i < samples; i++) {
        if (DrvImuBno08x_Read(data) == 0u) {
            Delay_ms(5u);
            continue;
        }
        sumX += data->gyroXf;
        sumY += data->gyroYf;
        sumZ += data->gyroZf;
        ok++;
        Delay_ms(5u);
    }

    if (ok > 0u) {
        data->gyroXOffset = sumX / (float)ok;
        data->gyroYOffset = sumY / (float)ok;
        data->gyroZOffset = sumZ / (float)ok;
    } else {
        data->gyroXOffset = 0.0f;
        data->gyroYOffset = 0.0f;
        data->gyroZOffset = 0.0f;
    }

    DrvImuBno08x_ResetAttitude(data);
}

void DrvImuBno08x_ResetAttitude(DrvImuBno08xData_t *data)
{
    if (data == 0) {
        return;
    }

    s_yawBase = s_rawYaw;
    data->exInt = 0.0f;
    data->eyInt = 0.0f;
    data->ezInt = 0.0f;
    data->yaw = 0.0f;
    data->prevYaw = 0.0f;
    data->yawRate = 0.0f;
    data->yawRateValid = 0u;
    data->yawSampleUpdated = 0u;
}

void DrvImuBno08x_SetBiasTrackEnabled(uint8_t enabled)
{
    s_biasTrackEnabled = enabled ? 1u : 0u;
}

void DrvImuBno08x_UpdateYaw(DrvImuBno08xData_t *data, float dt)
{
    float newYaw;
    float dy;
    float yawRate;
    (void)s_biasTrackEnabled;

    if ((data == 0) || (data->ahrsInited == 0u)) {
        return;
    }

    newYaw = drv_imu_wrap_deg(s_rawYaw - s_yawBase);
    if ((data->yawRateValid != 0u) && (dt > 0.0f)) {
        dy = newYaw - data->prevYaw;
        while (dy > 180.0f) {
            dy -= 360.0f;
        }
        while (dy < -180.0f) {
            dy += 360.0f;
        }
        yawRate = dy / dt;
        if ((fabsf(dy) > DRV_IMU_YAW_JUMP_REJECT_DEG) || (fabsf(yawRate) > DRV_IMU_YAW_RATE_LIMIT_DPS)) {
            float maxStep = DRV_IMU_YAW_RATE_LIMIT_DPS * dt;
            if (maxStep <= 0.0f) {
                maxStep = DRV_IMU_YAW_JUMP_REJECT_DEG;
            }
            if (maxStep > DRV_IMU_YAW_JUMP_REJECT_DEG) {
                maxStep = DRV_IMU_YAW_JUMP_REJECT_DEG;
            }
            if (dy > maxStep) {
                dy = maxStep;
            }
            if (dy < -maxStep) {
                dy = -maxStep;
            }
            newYaw = drv_imu_wrap_deg(data->prevYaw + dy);
            yawRate = dy / dt;
        }
        if (yawRate > DRV_IMU_YAW_RATE_LIMIT_DPS) {
            yawRate = DRV_IMU_YAW_RATE_LIMIT_DPS;
        }
        if (yawRate < -DRV_IMU_YAW_RATE_LIMIT_DPS) {
            yawRate = -DRV_IMU_YAW_RATE_LIMIT_DPS;
        }
        data->yawRate += (yawRate - data->yawRate) * DRV_IMU_YAW_RATE_LPF_ALPHA;
    } else {
        data->yawRate = 0.0f;
        data->yawRateValid = 1u;
    }

    data->yaw = newYaw;
    data->prevYaw = newYaw;
}

float DrvImuBno08x_GetYawError(float targetYaw, float currentYaw)
{
    return drv_imu_wrap_deg(targetYaw - currentYaw);
}

uint8_t DrvImuBno08x_IsReady(void)
{
    return s_ready;
}

uint8_t DrvImuBno08x_GetAddress(void)
{
    return s_addr;
}

uint8_t DrvImuBno08x_GetInitStage(void)
{
    return s_initStage;
}
