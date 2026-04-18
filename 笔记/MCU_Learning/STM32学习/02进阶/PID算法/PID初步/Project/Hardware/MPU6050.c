#include "stm32f10x.h"
#include "MPU6050.h"
#include "Delay.h"

// 软件 I2C 引脚定义 (PB12=SCL, PB13=SDA)
#define MPU_I2C_SCL_PIN     GPIO_Pin_12
#define MPU_I2C_SDA_PIN     GPIO_Pin_13
#define MPU_I2C_PORT        GPIOB

// I2C 位操作
#define MPU_SCL_H()         GPIO_SetBits(MPU_I2C_PORT, MPU_I2C_SCL_PIN)
#define MPU_SCL_L()         GPIO_ResetBits(MPU_I2C_PORT, MPU_I2C_SCL_PIN)
#define MPU_SDA_H()         GPIO_SetBits(MPU_I2C_PORT, MPU_I2C_SDA_PIN)
#define MPU_SDA_L()         GPIO_ResetBits(MPU_I2C_PORT, MPU_I2C_SDA_PIN)
#define MPU_SDA_READ()      GPIO_ReadInputDataBit(MPU_I2C_PORT, MPU_I2C_SDA_PIN)

// ACK超时计数（越大越不容易误判，但阻塞时间越长）
#define MPU_I2C_ACK_TIMEOUT  200

// 软件 I2C 延时（约400kHz）
static void MPU_I2C_Delay(void) {
    uint8_t i;
    for (i = 0; i < 10; i++) {
        __NOP();
    }
}

// SDA 方向切换（前向声明，供ACK等待使用）
static void MPU_SDA_OUT(void);
static void MPU_SDA_IN(void);

// I2C 等待应答（带超时）
// 返回：0=收到ACK，1=超时/无ACK
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

// I2C 发送应答
static void MPU_I2C_SendAck(void) {
    MPU_SDA_OUT();
    MPU_SDA_L();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SCL_L();
}

// I2C 发送非应答
static void MPU_I2C_SendNack(void) {
    MPU_SDA_OUT();
    MPU_SDA_H();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SCL_L();
}

// I2C 发送一个字节
static void MPU_I2C_SendByte(uint8_t data) {
    uint8_t i;
    MPU_SDA_OUT();
    MPU_SCL_L();
    for (i = 0; i < 8; i++) {
        if (data & 0x80) {
            MPU_SDA_H();
        } else {
            MPU_SDA_L();
        }
        data <<= 1;
        MPU_I2C_Delay();
        MPU_SCL_H();
        MPU_I2C_Delay();
        MPU_SCL_L();
    }
}

// I2C 接收一个字节
static uint8_t MPU_I2C_RecvByte(void) {
    uint8_t i, data = 0;
    MPU_SDA_IN();
    for (i = 0; i < 8; i++) {
        MPU_SCL_L();
        MPU_I2C_Delay();
        MPU_SCL_H();
        data <<= 1;
        if (MPU_SDA_READ()) {
            data |= 0x01;
        }
        MPU_I2C_Delay();
    }
    MPU_SCL_L();
    return data;
}

// I2C 起始信号
static void MPU_I2C_Start(void) {
    MPU_SDA_OUT();
    MPU_SDA_H();
    MPU_SCL_H();
    MPU_I2C_Delay();
    MPU_SDA_L();
    MPU_I2C_Delay();
    MPU_SCL_L();
}

// I2C 停止信号
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

// SDA 方向切换
static void MPU_SDA_OUT(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = MPU_I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU_I2C_PORT, &GPIO_InitStructure);
}

static void MPU_SDA_IN(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = MPU_I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(MPU_I2C_PORT, &GPIO_InitStructure);
}

// 写寄存器
// 返回：1=成功，0=失败
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

// 读寄存器
// 返回：1=成功，0=失败
static uint8_t MPU6050_ReadReg(uint8_t reg, uint8_t *out) {
    uint8_t data;
    MPU_I2C_Start();
    MPU_I2C_SendByte(MPU6050_ADDR << 1);
    if (MPU_I2C_WaitAck()) { MPU_I2C_Stop(); return 0; }
    MPU_I2C_SendByte(reg);
    if (MPU_I2C_WaitAck()) { MPU_I2C_Stop(); return 0; }
    MPU_I2C_Start();
    MPU_I2C_SendByte((MPU6050_ADDR << 1) | 0x01);
    if (MPU_I2C_WaitAck()) { MPU_I2C_Stop(); return 0; }
    data = MPU_I2C_RecvByte();
    MPU_I2C_SendNack();
    MPU_I2C_Stop();
    *out = data;
    return 1;
}

// 读多个寄存器
// 返回：1=成功，0=失败
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
    for (i = 0; i < len - 1; i++) {
        buf[i] = MPU_I2C_RecvByte();
        MPU_I2C_SendAck();
    }
    buf[len - 1] = MPU_I2C_RecvByte();
    MPU_I2C_SendNack();
    MPU_I2C_Stop();
    return 1;
}

// 初始化
void MPU6050_Init(void) {
    // 开启 GPIO 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
    // 配置 SCL/SDA 为开漏输出（模拟I2C推荐）
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = MPU_I2C_SCL_PIN | MPU_I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU_I2C_PORT, &GPIO_InitStructure);
    
    // 初始状态
    MPU_SCL_H();
    MPU_SDA_H();
    
    // 延时等待上电
    Delay_ms(100);
    
    // 复位 MPU6050
    MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x80);
    Delay_ms(100);
    
    // 唤醒，使用内部 8MHz 时钟
    MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x00);
    Delay_ms(10);
    
    // 陀螺仪量程 ±2000°/s
    MPU6050_WriteReg(MPU6050_GYRO_CONFIG, 0x18);
    
    // 加速度计量程 ±2g
    MPU6050_WriteReg(MPU6050_ACCEL_CONFIG, 0x00);
    
    // 滤波器配置（可选）
    // MPU6050_WriteReg(0x1A, 0x03);  // DLPF 42Hz
}

// 读取所有数据
// 返回：1=成功，0=失败
uint8_t MPU6050_ReadAll(MPU6050_Data_t *data) {
    uint8_t buf[14];
    if (!MPU6050_ReadRegs(MPU6050_ACCEL_XOUT_H, buf, 14)) {
        return 0;
    }
    
    data->accelX = (int16_t)((buf[0] << 8) | buf[1]);
    data->accelY = (int16_t)((buf[2] << 8) | buf[3]);
    data->accelZ = (int16_t)((buf[4] << 8) | buf[5]);
    
    // 跳过温度
    data->gyroX = (int16_t)((buf[8] << 8) | buf[9]);
    data->gyroY = (int16_t)((buf[10] << 8) | buf[11]);
    data->gyroZ = (int16_t)((buf[12] << 8) | buf[13]);
    
    // 减去零偏，转换为角速度 (°/s)
    data->yawRate = (data->gyroZ - data->gyroZOffset) / MPU6050_GYRO_SENS;
    return 1;
}

// 零偏校准
void MPU6050_Calibrate(MPU6050_Data_t *data, uint16_t samples) {
    int32_t sumX = 0, sumY = 0, sumZ = 0;
    uint16_t okCount = 0;
    uint16_t i;
    
    for (i = 0; i < samples; i++) {
        if (!MPU6050_ReadAll(data)) {
            Delay_ms(2);
            continue;
        }
        sumX += data->gyroX;
        sumY += data->gyroY;
        sumZ += data->gyroZ;
        okCount++;
        Delay_ms(5);
    }
    
    if (okCount == 0) {
        // 读取失败时保持原零偏，避免除零
        return;
    }
    data->gyroXOffset = (float)sumX / okCount;
    data->gyroYOffset = (float)sumY / okCount;
    data->gyroZOffset = (float)sumZ / okCount;
}

// 航向角更新（积分陀螺仪）
void MPU6050_UpdateYaw(MPU6050_Data_t *data, float dt) {
    // 简单积分：yaw += yawRate * dt
    data->yaw += data->yawRate * dt;
    
    // 归一化到 -180 ~ +180
    while (data->yaw > 180.0f) data->yaw -= 360.0f;
    while (data->yaw < -180.0f) data->yaw += 360.0f;
}

// 获取归一化航向误差
float MPU6050_GetYawError(float targetYaw, float currentYaw) {
    float error = targetYaw - currentYaw;
    while (error > 180.0f) error -= 360.0f;
    while (error < -180.0f) error += 360.0f;
    return error;
}
