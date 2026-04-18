#include "stm32f10x.h"
#include "MPU6050.h"
#include "Delay.h"

#define MPU_I2C_SCL_PIN     GPIO_Pin_12
#define MPU_I2C_SDA_PIN     GPIO_Pin_13
#define MPU_I2C_PORT        GPIOB

#define MPU_SCL_H()         GPIO_SetBits(MPU_I2C_PORT, MPU_I2C_SCL_PIN)
#define MPU_SCL_L()         GPIO_ResetBits(MPU_I2C_PORT, MPU_I2C_SCL_PIN)
#define MPU_SDA_H()         GPIO_SetBits(MPU_I2C_PORT, MPU_I2C_SDA_PIN)
#define MPU_SDA_L()         GPIO_ResetBits(MPU_I2C_PORT, MPU_I2C_SDA_PIN)
#define MPU_SDA_READ()      GPIO_ReadInputDataBit(MPU_I2C_PORT, MPU_I2C_SDA_PIN)

#define MPU_I2C_ACK_TIMEOUT  400

static void MPU_I2C_Delay(void) {
    uint8_t i;
    for (i = 0; i < 40; i++) {
        __NOP();
    }
}

static void MPU_SDA_OUT(void) {
    GPIO_InitTypeDef g;
    g.GPIO_Pin = MPU_I2C_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU_I2C_PORT, &g);
}

static void MPU_SDA_IN(void) {
    GPIO_InitTypeDef g;
    g.GPIO_Pin = MPU_I2C_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(MPU_I2C_PORT, &g);
}

static uint8_t MPU_I2C_WaitAck(void) {
    uint16_t timeout = 0;
    MPU_SDA_IN();
    MPU_SDA_H();
    MPU_SCL_H();
    MPU_I2C_Delay();
    while (MPU_SDA_READ()) {
        if (++timeout >= MPU_I2C_ACK_TIMEOUT) {
            MPU_SCL_L();
            MPU_SDA_OUT();
            return 1;
        }
        MPU_I2C_Delay();
    }
    MPU_SCL_L();
    MPU_SDA_OUT();
    return 0;
}

static void MPU_I2C_SendAck(void) {
    MPU_SDA_OUT();
    MPU_SDA_L();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SCL_L();
}

static void MPU_I2C_SendNack(void) {
    MPU_SDA_OUT();
    MPU_SDA_H();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SCL_L();
}

static void MPU_I2C_SendByte(uint8_t data) {
    uint8_t i;
    MPU_SDA_OUT();
    MPU_SCL_L();
    for (i = 0; i < 8; i++) {
        if (data & 0x80) MPU_SDA_H();
        else MPU_SDA_L();
        data <<= 1;
        MPU_I2C_Delay();
        MPU_SCL_H();
        MPU_I2C_Delay();
        MPU_SCL_L();
    }
}

static uint8_t MPU_I2C_RecvByte(void) {
    uint8_t i, data = 0;
    MPU_SDA_IN();
    for (i = 0; i < 8; i++) {
        MPU_SCL_L();
        MPU_I2C_Delay();
        MPU_SCL_H();
        data <<= 1;
        if (MPU_SDA_READ()) data |= 0x01;
        MPU_I2C_Delay();
    }
    MPU_SCL_L();
    return data;
}

static void MPU_I2C_Start(void) {
    MPU_SDA_OUT();
    MPU_SDA_H();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SDA_L();
    MPU_I2C_Delay();
    MPU_SCL_L();
}

static void MPU_I2C_Stop(void) {
    MPU_SDA_OUT();
    MPU_SCL_L();
    MPU_SDA_L();
    MPU_I2C_Delay();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SDA_H();
    MPU_I2C_Delay();
}

static uint8_t MPU6050_WriteReg(uint8_t reg, uint8_t data) {
    MPU_I2C_Start();
    MPU_I2C_SendByte(MPU6050_ADDR << 1);
    if (MPU_I2C_WaitAck()) { MPU_I2C_Stop(); return 0; }
    MPU_I2C_SendByte(reg);
    if (MPU_I2C_WaitAck()) { MPU_I2C_Stop(); return 0; }
    MPU_I2C_SendByte(data);
    if (MPU_I2C_WaitAck()) { MPU_I2C_Stop(); return 0; }
    MPU_I2C_Stop();
    return 1;
}

static uint8_t MPU6050_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
    uint8_t i;
    if (len == 0) return 0;

    MPU_I2C_Start();
    MPU_I2C_SendByte(MPU6050_ADDR << 1);
    if (MPU_I2C_WaitAck()) { MPU_I2C_Stop(); return 0; }
    MPU_I2C_SendByte(reg);
    if (MPU_I2C_WaitAck()) { MPU_I2C_Stop(); return 0; }

    MPU_I2C_Start();
    MPU_I2C_SendByte((MPU6050_ADDR << 1) | 0x01);
    if (MPU_I2C_WaitAck()) { MPU_I2C_Stop(); return 0; }

    for (i = 0; i < (uint8_t)(len - 1); i++) {
        buf[i] = MPU_I2C_RecvByte();
        MPU_I2C_SendAck();
    }
    buf[len - 1] = MPU_I2C_RecvByte();
    MPU_I2C_SendNack();
    MPU_I2C_Stop();
    return 1;
}

void MPU6050_Init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef g;
    g.GPIO_Pin = MPU_I2C_SCL_PIN | MPU_I2C_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU_I2C_PORT, &g);

    MPU_SCL_H();
    MPU_SDA_H();

    Delay_ms(100);

    MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x80);
    Delay_ms(100);
    MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x00);
    Delay_ms(10);

    MPU6050_WriteReg(MPU6050_GYRO_CONFIG, 0x18);
    MPU6050_WriteReg(MPU6050_ACCEL_CONFIG, 0x00);
}

uint8_t MPU6050_ReadAll(MPU6050_Data_t *data) {
    uint8_t buf[14];
    if (!MPU6050_ReadRegs(MPU6050_ACCEL_XOUT_H, buf, 14)) return 0;

    data->accelX = (int16_t)((buf[0] << 8) | buf[1]);
    data->accelY = (int16_t)((buf[2] << 8) | buf[3]);
    data->accelZ = (int16_t)((buf[4] << 8) | buf[5]);

    data->gyroX = (int16_t)((buf[8] << 8) | buf[9]);
    data->gyroY = (int16_t)((buf[10] << 8) | buf[11]);
    data->gyroZ = (int16_t)((buf[12] << 8) | buf[13]);

    data->yawRate = (data->gyroZ - data->gyroZOffset) / MPU6050_GYRO_SENS;
    return 1;
}

void MPU6050_Calibrate(MPU6050_Data_t *data, uint16_t samples) {
    int32_t sumX = 0, sumY = 0, sumZ = 0;
    uint16_t ok = 0;
    uint16_t i;

    for (i = 0; i < samples; i++) {
        if (!MPU6050_ReadAll(data)) {
            Delay_ms(2);
            continue;
        }
        sumX += data->gyroX;
        sumY += data->gyroY;
        sumZ += data->gyroZ;
        ok++;
        Delay_ms(5);
    }

    if (ok == 0) return;
    data->gyroXOffset = (float)sumX / ok;
    data->gyroYOffset = (float)sumY / ok;
    data->gyroZOffset = (float)sumZ / ok;
}

void MPU6050_UpdateYaw(MPU6050_Data_t *data, float dt) {
    data->yaw += data->yawRate * dt;
    while (data->yaw > 180.0f) data->yaw -= 360.0f;
    while (data->yaw < -180.0f) data->yaw += 360.0f;
}

float MPU6050_GetYawError(float targetYaw, float currentYaw) {
    float e = targetYaw - currentYaw;
    while (e > 180.0f) e -= 360.0f;
    while (e < -180.0f) e += 360.0f;
    return e;
}
