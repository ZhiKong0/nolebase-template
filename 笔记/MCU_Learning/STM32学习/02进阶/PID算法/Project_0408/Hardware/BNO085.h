/**
 * @file BNO085.h
 * @brief BNO085 IMU传感器驱动（完整版）
 * @description 支持BNO080/BNO085九轴传感器，带内置AHRS
 */

#ifndef __BNO085_H
#define __BNO085_H

#include "stm32f10x.h"
#include "HardwareConfig.h"

/*===========================================================================
 * BNO085 数据类型
 *========================================================================*/

/**
 * @brief BNO085原始数据
 */
typedef struct {
    int16_t accelX;
    int16_t accelY;
    int16_t accelZ;
    int16_t gyroX;
    int16_t gyroY;
    int16_t gyroZ;
    int16_t magX;
    int16_t magY;
    int16_t magZ;
} BNO085_RawData_t;

/**
 * @brief BNO085融合数据（欧拉角）
 */
typedef struct {
    // 欧拉角（度）
    float pitch;
    float roll;
    float yaw;
    
    // 角速度（度/秒）
    float yawRate;
    float pitchRate;
    float rollRate;
    
    // 四元数
    float q0;
    float q1;
    float q2;
    float q3;
    
    // 加速度（g）
    float accelX;
    float accelY;
    float accelZ;
    
    // 磁力计
    float magX;
    float magY;
    float magZ;
    
    // 状态
    uint8_t ahrsInited;
    uint8_t yawSampleUpdated;
    float temperature;
} BNO085_Data_t;

/*===========================================================================
 * BNO085 配置
 *========================================================================*/

typedef struct {
    // I2C配置
    uint8_t deviceAddress;      // 0x4A 或 0x4B
    
    // 传感器配置
    uint8_t accelRange;        // 加速度计量程
    uint8_t gyroRange;        // 陀螺仪量程
    uint8_t accelBandwidth;   // 加速度计带宽
    uint8_t gyroBandwidth;    // 陀螺仪带宽
    
    // 报告间隔
    uint16_t reportIntervalUs; // 报告间隔（微秒）
    
    hw_bool_t initialized;
} BNO085_Config_t;

typedef struct {
    hw_bool_t connected;
    uint8_t whoAmI;
    uint8_t i2cAddr;
    uint8_t revision;
    uint8_t partInfo[4];
    
    uint32_t readOkCount;
    uint32_t readFailCount;
    
    BNO085_Data_t data;
} BNO085_Status_t;

/*===========================================================================
 * BNO085 寄存器地址
 *========================================================================*/

#define BNO085_ADDR_DEFAULT     0x4A
#define BNO085_ADDR_ALT        0x4B

// 命令寄存器
#define BNO085_REG_COMMAND                    0x54
#define BNO085_REG_COMMAND_RESPONSE            0x05
#define BNO085_REG_COMMAND_STATUS              0x53
#define BNO085_REG_COMMAND_LENGTH              0x52

// 数据寄存器
#define BNO085_REG_ACCEL_X                     0x08
#define BNO085_REG_GYRO_X                      0x14
#define BNO085_REG_MAG_X                      0x20

// 配置寄存器
#define BNO085_REG_ACCEL_CONFIG                0x56
#define BNO085_REG_GYRO_CONFIG                 0x57
#define BNO085_REG_MAG_CONFIG                 0x5A

// 状态寄存器
#define BNO085_REG_PWR_MODE                    0x3E
#define BNO085_REG_OPR_MODE                    0x3D
#define BNO085_REG_INT_EN                      0x12
#define BNO085_REG_ACCEL_STATUS                0x2B
#define BNO085_REG_GYRO_STATUS                 0x2C
#define BNO085_REG_MAG_STATUS                  0x2D

// 设备信息
#define BNO085_REG_WHO_AM_I                    0x00
#define BNO085_REG_REVISION                    0x01
#define BNO085_REG_PART_INFO                   0x02
#define BNO085_REG_STATUS                      0x39

// 命令代码
#define BNO085_CMD_START_DAE                   0x00
#define BNO085_CMD_STOP_DAE                    0x01
#define BNO085_CMD_FLUSH_DAE                   0x02
#define BNO085_CMD_GET_FEATURES                0x03
#define BNO085_CMD_SET_FEATURE                 0x04

// 传感器ID
#define BNO085_SENSOR_ACCELEROMETER            0x01
#define BNO085_SENSOR_GYROSCOPE               0x02
#define BNO085_SENSOR_MAGNETOMETER            0x03
#define BNO085_SENSOR_ROTATION_VECTOR         0x05
#define BNO085_SENSOR_GAME_ROTATION_VECTOR    0x08
#define BNO085_SENSOR_STEP_COUNTER             0x19

/*===========================================================================
 * 公共API
 *========================================================================*/

hw_status_t BNO085_Init(void);
hw_status_t BNO085_InitWithConfig(BNO085_Config_t *config);
hw_status_t BNO085_ReadAll(BNO085_Data_t *data);
hw_status_t BNO085_ReadRaw(BNO085_RawData_t *raw);
void BNO085_UpdateYaw(BNO085_Data_t *data, float dt);
void BNO085_Calibrate(BNO085_Data_t *data, uint16_t samples);
void BNO085_ResetAttitude(BNO085_Data_t *data);
void BNO085_SetBiasTrackEnabled(uint8_t enable);
BNO085_Status_t* BNO085_GetStatus(void);
uint8_t BNO085_GetWhoAmI(void);
uint8_t BNO085_GetI2CAddr(void);

/*===========================================================================
 * 高级API
 *========================================================================*/

hw_status_t BNO085_EnableRotationVector(uint16_t intervalUs);
hw_status_t BNO085_EnableGameRotationVector(uint16_t intervalUs);
hw_status_t BNO085_EnableAccelerometer(uint16_t intervalUs);
hw_status_t BNO085_EnableGyroscope(uint16_t intervalUs);
hw_status_t BNO085_EnableMagnetometer(uint16_t intervalUs);

void BNO085_GetQuaternion(float *w, float *x, float *y, float *z);
void BNO085_SetFeature(uint8_t sensorId, uint16_t reportInterval);

#endif /* __BNO085_H */
