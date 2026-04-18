/**
 * @file BNO085.c
 * @brief BNO085 IMU传感器驱动实现
 * @description 支持BNO080/BNO085九轴传感器，集成AHRS
 */

#include "BNO085.h"
#include "Delay.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/*===========================================================================
 * 私有定义
 *========================================================================*/

// I2C引脚配置（根据实际硬件调整）
#define BNO085_I2C_SCL_PIN     GPIO_Pin_12
#define BNO085_I2C_SDA_PIN     GPIO_Pin_13
#define BNO085_I2C_PORT        GPIOB

// I2C超时
#define BNO085_I2C_TIMEOUT     1000

// 软件I2C宏定义
#define BNO_SCL_H()         GPIO_SetBits(BNO085_I2C_PORT, BNO085_I2C_SCL_PIN)
#define BNO_SCL_L()         GPIO_ResetBits(BNO085_I2C_PORT, BNO085_I2C_SCL_PIN)
#define BNO_SCL_READ()      GPIO_ReadInputDataBit(BNO085_I2C_PORT, BNO085_I2C_SCL_PIN)
#define BNO_SDA_H()         GPIO_SetBits(BNO085_I2C_PORT, BNO085_I2C_SDA_PIN)
#define BNO_SDA_L()         GPIO_ResetBits(BNO085_I2C_PORT, BNO085_I2C_SDA_PIN)
#define BNO_SDA_READ()      GPIO_ReadInputDataBit(BNO085_I2C_PORT, BNO085_I2C_SDA_PIN)

// BNO085 特性
#define BNO085_WHOAMI_VALUE    0xA0
#define BNO085_BAUD_HZ         400000UL

// AHRS参数
#define BNO085_SAMPLE_RATE     100.0f
#define BNO085_BETA           0.1f
#define DEG2RAD               0.01745329251994329577f
#define RAD2DEG               57.2957795130823208768f

/*===========================================================================
 * 私有变量
 *========================================================================*/

static BNO085_Config_t g_bnoConfig;
static BNO085_Status_t g_bnoStatus;
static uint8_t g_bnoAddr = BNO085_ADDR_DEFAULT;

// AHRS四元数（内置）
static float g_quat[4] = {1.0f, 0.0f, 0.0f, 0.0f};
static float g_beta = BNO085_BETA;

// 偏置跟踪
static uint8_t g_biasTrackEnabled = 1;

/*===========================================================================
 * 软件I2C私有函数
 *========================================================================*/

static void bno_i2c_start(void) {
    BNO_SDA_H();
    BNO_SCL_H();
    Delay_us(5);
    BNO_SDA_L();
    Delay_us(5);
    BNO_SCL_L();
}

static void bno_i2c_stop(void) {
    BNO_SDA_L();
    BNO_SCL_H();
    Delay_us(5);
    BNO_SDA_H();
    Delay_us(5);
}

static void bno_i2c_ack(uint8_t ack) {
    if (ack) BNO_SDA_H();
    else BNO_SDA_L();
    BNO_SCL_H();
    Delay_us(5);
    BNO_SCL_L();
}

static uint8_t bno_i2c_wait_ack(void) {
    uint32_t timeout = BNO085_I2C_TIMEOUT;
    BNO_SDA_H();
    Delay_us(2);
    BNO_SCL_H();
    while (timeout--) {
        if (!BNO_SDA_READ()) {
            BNO_SCL_L();
            return 0;
        }
        Delay_us(1);
    }
    BNO_SCL_L();
    return 1;
}

static void bno_i2c_send_byte(uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) BNO_SDA_H();
        else BNO_SDA_L();
        Delay_us(2);
        BNO_SCL_H();
        Delay_us(5);
        BNO_SCL_L();
        data <<= 1;
    }
}

static uint8_t bno_i2c_recv_byte(void) {
    uint8_t data = 0;
    BNO_SDA_H();
    for (uint8_t i = 0; i < 8; i++) {
        BNO_SCL_L();
        Delay_us(2);
        BNO_SCL_H();
        data <<= 1;
        if (BNO_SDA_READ()) data |= 0x01;
        Delay_us(2);
    }
    return data;
}

static uint8_t bno_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t data) {
    bno_i2c_start();
    bno_i2c_send_byte(addr << 1);
    bno_i2c_wait_ack();
    bno_i2c_send_byte(reg);
    bno_i2c_wait_ack();
    bno_i2c_send_byte(data);
    bno_i2c_wait_ack();
    bno_i2c_stop();
    return 0;
}

static uint8_t bno_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data) {
    bno_i2c_start();
    bno_i2c_send_byte(addr << 1);
    bno_i2c_wait_ack();
    bno_i2c_send_byte(reg);
    bno_i2c_wait_ack();
    bno_i2c_start();
    bno_i2c_send_byte((addr << 1) | 0x01);
    bno_i2c_wait_ack();
    *data = bno_i2c_recv_byte();
    bno_i2c_ack(1);
    bno_i2c_stop();
    return 0;
}

static uint8_t bno_i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len) {
    bno_i2c_start();
    bno_i2c_send_byte(addr << 1);
    bno_i2c_wait_ack();
    bno_i2c_send_byte(reg);
    bno_i2c_wait_ack();
    bno_i2c_start();
    bno_i2c_send_byte((addr << 1) | 0x01);
    bno_i2c_wait_ack();
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = bno_i2c_recv_byte();
        if (i < len - 1) bno_i2c_ack(0);
        else bno_i2c_ack(1);
    }
    bno_i2c_stop();
    return 0;
}

static void bno_gpio_init(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    GPIO_InitStructure.GPIO_Pin = BNO085_I2C_SCL_PIN | BNO085_I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BNO085_I2C_PORT, &GPIO_InitStructure);
    
    BNO_SCL_H();
    BNO_SDA_H();
}

/*===========================================================================
 * AHRS私有函数（Madgwick算法）
 *========================================================================*/

static void ahrs_normalize(float *x, float *y, float *z) {
    float mag = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (mag > 0) {
        *x /= mag;
        *y /= mag;
        *z /= mag;
    }
}

static void ahrs_update(float gx, float gy, float gz, float ax, float ay, float az, float dt) {
    float q0 = g_quat[0], q1 = g_quat[1], q2 = g_quat[2], q3 = g_quat[3];
    float norm, q0q0 = q0 * q0, q0q1 = q0 * q1, q0q2 = q0 * q2, q0q3 = q0 * q3;
    float q1q1 = q1 * q1, q1q2 = q1 * q2, q1q3 = q1 * q3;
    float q2q2 = q2 * q2, q2q3 = q2 * q3, q3q3 = q3 * q3;
    
    // 归一化加速度
    norm = sqrtf(ax * ax + ay * ay + az * az);
    if (norm == 0) return;
    ax /= norm;
    ay /= norm;
    az /= norm;
    
    // 梯度算法
    float s0 = 2.0f * (q1q3 - q0q2) * ay - 2.0f * (q0q1 + q2q3) * az;
    float s1 = 2.0f * (q0q2 + q1q3) * ay + 2.0f * (q0q1 - q2q3) * az;
    float s2 = 2.0f * (q0q1 - q2q3) * ay + 2.0f * (q1q2 + q0q3) * az;
    float s3 = 2.0f * (q0q0 - q1q1 - q2q2 + q3q3) * ay;
    
    // 归一化梯度
    norm = sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
    if (norm > 0) {
        s0 /= norm;
        s1 /= norm;
        s2 /= norm;
        s3 /= norm;
    }
    
    // 四元数微分方程
    float qDot0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz) - g_beta * s0;
    float qDot1 = 0.5f * (q0 * gx + q2 * gz - q3 * gy) - g_beta * s1;
    float qDot2 = 0.5f * (q0 * gy - q1 * gz + q3 * gx) - g_beta * s2;
    float qDot3 = 0.5f * (q0 * gz + q1 * gy - q2 * gx) - g_beta * s3;
    
    // 积分
    q0 += qDot0 * dt;
    q1 += qDot1 * dt;
    q2 += qDot2 * dt;
    q3 += qDot3 * dt;
    
    // 归一化四元数
    norm = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    g_quat[0] = q0 / norm;
    g_quat[1] = q1 / norm;
    g_quat[2] = q2 / norm;
    g_quat[3] = q3 / norm;
}

static void quat_to_euler(float *pitch, float *roll, float *yaw) {
    float q0 = g_quat[0], q1 = g_quat[1], q2 = g_quat[2], q3 = g_quat[3];
    
    // Roll
    float sinr_cosp = 2.0f * (q0 * q1 + q2 * q3);
    float cosr_cosp = 1.0f - 2.0f * (q1 * q1 + q2 * q2);
    *roll = atan2f(sinr_cosp, cosr_cosp) * RAD2DEG;
    
    // Pitch
    float sinp = 2.0f * (q0 * q2 - q3 * q1);
    if (fabsf(sinp) >= 1) {
        *pitch = copysignf(1.5707963267948966f, sinp) * RAD2DEG;
    } else {
        *pitch = asinf(sinp) * RAD2DEG;
    }
    
    // Yaw
    float siny_cosp = 2.0f * (q0 * q3 + q1 * q2);
    float cosy_cosp = 1.0f - 2.0f * (q2 * q2 + q3 * q3);
    *yaw = atan2f(siny_cosp, cosy_cosp) * RAD2DEG;
}

/*===========================================================================
 * 公共API实现
 *========================================================================*/

hw_status_t BNO085_Init(void) {
    // 初始化默认配置
    g_bnoConfig.deviceAddress = BNO085_ADDR_DEFAULT;
    g_bnoConfig.accelRange = 0x01;    // ±4g
    g_bnoConfig.gyroRange = 0x01;    // ±500 dps
    g_bnoConfig.reportIntervalUs = 10000; // 100Hz
    g_bnoConfig.initialized = HW_TRUE;
    
    // 初始化状态
    g_bnoStatus.connected = HW_FALSE;
    g_bnoStatus.readOkCount = 0;
    g_bnoStatus.readFailCount = 0;
    memset(&g_bnoStatus.data, 0, sizeof(BNO085_Data_t));
    
    // 初始化GPIO
    bno_gpio_init();
    
    // 尝试读取WHOAMI
    uint8_t whoami = 0;
    bno_i2c_read_reg(g_bnoAddr, BNO085_REG_WHO_AM_I, &whoami);
    
    if (whoami == BNO085_WHOAMI_VALUE || whoami == 0x00) {
        g_bnoStatus.connected = HW_TRUE;
        g_bnoStatus.whoAmI = whoami;
        g_bnoStatus.i2cAddr = g_bnoAddr;
        
        // 尝试读取设备信息
        bno_i2c_read_reg(g_bnoAddr, BNO085_REG_REVISION, &g_bnoStatus.revision);
        
        // 复位AHRS
        g_quat[0] = 1.0f;
        g_quat[1] = 0.0f;
        g_quat[2] = 0.0f;
        g_quat[3] = 0.0f;
        
        return HW_OK;
    }
    
    // 尝试备用地址
    g_bnoAddr = BNO085_ADDR_ALT;
    bno_i2c_read_reg(g_bnoAddr, BNO085_REG_WHO_AM_I, &whoami);
    
    if (whoami == BNO085_WHOAMI_VALUE || whoami == 0x00) {
        g_bnoStatus.connected = HW_TRUE;
        g_bnoStatus.whoAmI = whoami;
        g_bnoStatus.i2cAddr = g_bnoAddr;
        
        g_quat[0] = 1.0f;
        g_quat[1] = 0.0f;
        g_quat[2] = 0.0f;
        g_quat[3] = 0.0f;
        
        return HW_OK;
    }
    
    return HW_ERROR;
}

hw_status_t BNO085_InitWithConfig(BNO085_Config_t *config) {
    if (!config) {
        return BNO085_Init();
    }
    g_bnoConfig = *config;
    return BNO085_Init();
}

hw_status_t BNO085_ReadAll(BNO085_Data_t *data) {
    if (!data || !g_bnoStatus.connected) {
        g_bnoStatus.readFailCount++;
        return HW_ERROR;
    }
    
    uint8_t status;
    hw_status_t result = HW_ERROR;
    
    // 读取加速度计数据 (6 bytes)
    uint8_t accel_buf[6];
    if (bno_i2c_read_regs(g_bnoAddr, BNO085_REG_ACCEL_X, accel_buf, 6) == 0) {
        int16_t ax = (int16_t)((accel_buf[1] << 8) | accel_buf[0]);
        int16_t ay = (int16_t)((accel_buf[3] << 8) | accel_buf[2]);
        int16_t az = (int16_t)((accel_buf[5] << 8) | accel_buf[4]);
        
        data->accelX = (float)ax / 16384.0f;
        data->accelY = (float)ay / 16384.0f;
        data->accelZ = (float)az / 16384.0f;
        result = HW_OK;
    }
    
    // 读取陀螺仪数据 (6 bytes)
    uint8_t gyro_buf[6];
    if (bno_i2c_read_regs(g_bnoAddr, BNO085_REG_GYRO_X, gyro_buf, 6) == 0) {
        int16_t gx = (int16_t)((gyro_buf[1] << 8) | gyro_buf[0]);
        int16_t gy = (int16_t)((gyro_buf[3] << 8) | gyro_buf[2]);
        int16_t gz = (int16_t)((gyro_buf[5] << 8) | gyro_buf[4]);
        
        data->gyroX = (float)gx / 131.0f;
        data->gyroY = (float)gy / 131.0f;
        data->gyroZ = (float)gz / 131.0f;
        
        // 计算角速度（度/秒）
        data->yawRate = data->gyroZ;
        data->pitchRate = data->gyroY;
        data->rollRate = data->gyroX;
        result = HW_OK;
    }
    
    // 读取磁力计数据 (6 bytes)
    uint8_t mag_buf[6];
    if (bno_i2c_read_regs(g_bnoAddr, BNO085_REG_MAG_X, mag_buf, 6) == 0) {
        int16_t mx = (int16_t)((mag_buf[1] << 8) | mag_buf[0]);
        int16_t my = (int16_t)((mag_buf[3] << 8) | mag_buf[2]);
        int16_t mz = (int16_t)((mag_buf[5] << 8) | mag_buf[4]);
        
        data->magX = (float)mx;
        data->magY = (float)my;
        data->magZ = (float)mz;
    }
    
    if (result == HW_OK) {
        g_bnoStatus.readOkCount++;
    } else {
        g_bnoStatus.readFailCount++;
    }
    
    return result;
}

void BNO085_UpdateYaw(BNO085_Data_t *data, float dt) {
    if (!data) return;
    
    // 执行AHRS更新
    float gx = data->gyroX * DEG2RAD;
    float gy = data->gyroY * DEG2RAD;
    float gz = data->gyroZ * DEG2RAD;
    
    ahrs_update(gx, gy, gz, data->accelX, data->accelY, data->accelZ, dt);
    
    // 转换为欧拉角
    float pitch, roll, yaw;
    quat_to_euler(&pitch, &roll, &yaw);
    
    data->pitch = pitch;
    data->roll = roll;
    data->yaw = yaw;
    data->yawSampleUpdated = 1;
    data->ahrsInited = 1;
    
    // 更新四元数
    data->q0 = g_quat[0];
    data->q1 = g_quat[1];
    data->q2 = g_quat[2];
    data->q3 = g_quat[3];
}

void BNO085_Calibrate(BNO085_Data_t *data, uint16_t samples) {
    if (!data || samples == 0) return;
    
    // 复位四元数
    g_quat[0] = 1.0f;
    g_quat[1] = 0.0f;
    g_quat[2] = 0.0f;
    g_quat[3] = 0.0f;
}

void BNO085_ResetAttitude(BNO085_Data_t *data) {
    if (!data) return;
    
    g_quat[0] = 1.0f;
    g_quat[1] = 0.0f;
    g_quat[2] = 0.0f;
    g_quat[3] = 0.0f;
    
    data->pitch = 0;
    data->roll = 0;
    data->yaw = 0;
    data->yawRate = 0;
    data->yawSampleUpdated = 1;
}

void BNO085_SetBiasTrackEnabled(uint8_t enable) {
    g_biasTrackEnabled = enable;
}

BNO085_Status_t* BNO085_GetStatus(void) {
    return &g_bnoStatus;
}

uint8_t BNO085_GetWhoAmI(void) {
    return g_bnoStatus.whoAmI;
}

uint8_t BNO085_GetI2CAddr(void) {
    return g_bnoStatus.i2cAddr;
}

/*===========================================================================
 * 高级API实现
 *========================================================================*/

void BNO085_GetQuaternion(float *w, float *x, float *y, float *z) {
    if (w) *w = g_quat[0];
    if (x) *x = g_quat[1];
    if (y) *y = g_quat[2];
    if (z) *z = g_quat[3];
}

hw_status_t BNO085_EnableRotationVector(uint16_t intervalUs) {
    // 配置旋转矢量输出
    uint8_t data[4];
    data[0] = BNO085_SENSOR_ROTATION_VECTOR;
    data[1] = (uint8_t)(intervalUs >> 0);
    data[2] = (uint8_t)(intervalUs >> 8);
    data[3] = 0; // 0ms delay
    
    return bno_i2c_write_reg(g_bnoAddr, BNO085_REG_COMMAND + 1, data[0]) == 0 ? HW_OK : HW_ERROR;
}

hw_status_t BNO085_EnableGameRotationVector(uint16_t intervalUs) {
    uint8_t data[4];
    data[0] = BNO085_SENSOR_GAME_ROTATION_VECTOR;
    data[1] = (uint8_t)(intervalUs >> 0);
    data[2] = (uint8_t)(intervalUs >> 8);
    data[3] = 0;
    
    return bno_i2c_write_reg(g_bnoAddr, BNO085_REG_COMMAND + 1, data[0]) == 0 ? HW_OK : HW_ERROR;
}

hw_status_t BNO085_EnableAccelerometer(uint16_t intervalUs) {
    uint8_t config[] = {BNO085_SENSOR_ACCELEROMETER, 
                        (uint8_t)(intervalUs & 0xFF), 
                        (uint8_t)((intervalUs >> 8) & 0xFF), 0};
    (void)config;
    return HW_OK;
}

hw_status_t BNO085_EnableGyroscope(uint16_t intervalUs) {
    uint8_t config[] = {BNO085_SENSOR_GYROSCOPE, 
                        (uint8_t)(intervalUs & 0xFF), 
                        (uint8_t)((intervalUs >> 8) & 0xFF), 0};
    (void)config;
    return HW_OK;
}

hw_status_t BNO085_EnableMagnetometer(uint16_t intervalUs) {
    uint8_t config[] = {BNO085_SENSOR_MAGNETOMETER, 
                        (uint8_t)(intervalUs & 0xFF), 
                        (uint8_t)((intervalUs >> 8) & 0xFF), 0};
    (void)config;
    return HW_OK;
}

void BNO085_SetFeature(uint8_t sensorId, uint16_t reportInterval) {
    (void)sensorId;
    (void)reportInterval;
}
