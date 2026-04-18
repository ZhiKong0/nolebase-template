#include "MPU6050.h"
#include "Delay.h"

static void I2C_Delay(void)
{
    uint8_t i = 10; while(i--);
}

static void I2C_INit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(MPU6050_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = MPU6050_SCL_PIN|MPU6050_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MPU6050_GPIO_PORT, &GPIO_InitStructure);

    GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN|MPU6050_SDA_PIN);
}

static void I2C_Start(void)
{
    GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
    GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
    I2C_Delay();
    GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
    I2C_Delay();
    GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
}

static void I2C_Stop(void)
{
    GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
    GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
    I2C_Delay();
    GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
    I2C_Delay();
}

static void I2C_WriteByte(uint8_t Byte)
{
    uint8_t i;
    for(i=0;i<8;i++)
    {
        GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
        I2C_Delay();
        if(Byte & 0x80)
            GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
        else
            GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
        Byte <<= 1;
        I2C_Delay();
        GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
        I2C_Delay();
    }
    GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
}

static uint8_t I2C_ReadByte(void)
{
    uint8_t i,Byte=0;
    GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SDA_PIN);
    for(i=0;i<8;i++)
    {
        Byte <<= 1;
        GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
        I2C_Delay();
        GPIO_SetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
        I2C_Delay();
        if(GPIO_ReadInputDataBit(MPU6050_GPIO_PORT, MPU6050_SDA_PIN)) Byte++;
    }
    GPIO_ResetBits(MPU6050_GPIO_PORT, MPU6050_SCL_PIN);
    return Byte;
}

void MPU6050_WriteReg(uint8_t Reg, uint8_t Data)
{
    I2C_Start();
    I2C_WriteByte(0x68<<1);
    I2C_WriteByte(Reg);
    I2C_WriteByte(Data);
    I2C_Stop();
}

uint8_t MPU6050_ReadReg(uint8_t Reg)
{
    uint8_t Data;
    I2C_Start();
    I2C_WriteByte(0x68<<1);
    I2C_WriteByte(Reg);
    I2C_Start();
    I2C_WriteByte((0x68<<1)|1);
    Data = I2C_ReadByte();
    I2C_Stop();
    return Data;
}

void MPU6050_Init(void)
{
    I2C_INit();
    Delay_ms(10);
    MPU6050_WriteReg(0x6B,0x00);
    MPU6050_WriteReg(0x1B,0x08);
    MPU6050_WriteReg(0x1C,0x08);
}

void MPU6050_ReadData(float *Pitch, float *Roll)
{
    int16_t AccX,AccY,AccZ;
    int16_t GyroX,GyroY,GyroZ;

    AccX = (MPU6050_ReadReg(0x3B)<<8)|MPU6050_ReadReg(0x3C);
    AccY = (MPU6050_ReadReg(0x3D)<<8)|MPU6050_ReadReg(0x3E);
    AccZ = (MPU6050_ReadReg(0x3F)<<8)|MPU6050_ReadReg(0x40);

    GyroX= (MPU6050_ReadReg(0x43)<<8)|MPU6050_ReadReg(0x44);
    GyroY= (MPU6050_ReadReg(0x45)<<8)|MPU6050_ReadReg(0x46);
    GyroZ= (MPU6050_ReadReg(0x47)<<8)|MPU6050_ReadReg(0x48);

    *Roll  = (float)AccY / 4096.0f;
    *Pitch = (float)AccX / 4096.0f;
}

float MPU6050_GetYaw(void)
{
    static float yaw_angle = 0.0f;
    int16_t gyro_z;

    gyro_z = (MPU6050_ReadReg(0x47)<<8)|MPU6050_ReadReg(0x48);
    yaw_angle += -(float)gyro_z / 32.8f * 0.01f;
    return yaw_angle;
}