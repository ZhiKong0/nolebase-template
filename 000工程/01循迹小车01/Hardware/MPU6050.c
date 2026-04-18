#include "MPU6050.h"
#include "SoftI2C.h"
#include "Delay.h"

#define MPU6050_ADDR_7BIT 0x68
#define MPU6050_ADDR_W    ((MPU6050_ADDR_7BIT << 1) | 0)
#define MPU6050_ADDR_R    ((MPU6050_ADDR_7BIT << 1) | 1)

#define MPU6050_RA_SMPLRT_DIV     0x19
#define MPU6050_RA_CONFIG         0x1A
#define MPU6050_RA_GYRO_CONFIG    0x1B
#define MPU6050_RA_ACCEL_CONFIG   0x1C
#define MPU6050_RA_INT_ENABLE     0x38
#define MPU6050_RA_ACCEL_XOUT_H   0x3B
#define MPU6050_RA_PWR_MGMT_1     0x6B
#define MPU6050_RA_WHO_AM_I       0x75

static float g_gyroZ_bias_dps = 0.0f;

static void MPU6050_WriteReg(uint8_t Reg, uint8_t Data)
{
	SoftI2C_Start();
	SoftI2C_WriteByte(MPU6050_ADDR_W);
	SoftI2C_WriteByte(Reg);
	SoftI2C_WriteByte(Data);
	SoftI2C_Stop();
}

static uint8_t MPU6050_ReadReg(uint8_t Reg)
{
	uint8_t val;
	SoftI2C_Start();
	SoftI2C_WriteByte(MPU6050_ADDR_W);
	SoftI2C_WriteByte(Reg);
	SoftI2C_Start();
	SoftI2C_WriteByte(MPU6050_ADDR_R);
	val = SoftI2C_ReadByte(0);
	SoftI2C_Stop();
	return val;
}

static void MPU6050_ReadBytes(uint8_t Reg, uint8_t *Buf, uint8_t Len)
{
	SoftI2C_Start();
	SoftI2C_WriteByte(MPU6050_ADDR_W);
	SoftI2C_WriteByte(Reg);
	SoftI2C_Start();
	SoftI2C_WriteByte(MPU6050_ADDR_R);
	for (uint8_t i = 0; i < Len; i++)
	{
		Buf[i] = SoftI2C_ReadByte((i + 1) < Len);
	}
	SoftI2C_Stop();
}

void MPU6050_Init(void)
{
	SoftI2C_Init();

	MPU6050_WriteReg(MPU6050_RA_PWR_MGMT_1, 0x00);
	Delay_ms(10);

	MPU6050_WriteReg(MPU6050_RA_SMPLRT_DIV, 0x07);
	MPU6050_WriteReg(MPU6050_RA_CONFIG, 0x06);
	MPU6050_WriteReg(MPU6050_RA_GYRO_CONFIG, 0x18);
	MPU6050_WriteReg(MPU6050_RA_ACCEL_CONFIG, 0x00);
	MPU6050_WriteReg(MPU6050_RA_INT_ENABLE, 0x00);
}

uint8_t MPU6050_WhoAmI(void)
{
	return MPU6050_ReadReg(MPU6050_RA_WHO_AM_I);
}

void MPU6050_ReadRaw(MPU6050_Raw_t *Raw)
{
	uint8_t buf[14];
	MPU6050_ReadBytes(MPU6050_RA_ACCEL_XOUT_H, buf, 14);

	Raw->Ax = (int16_t)((buf[0] << 8) | buf[1]);
	Raw->Ay = (int16_t)((buf[2] << 8) | buf[3]);
	Raw->Az = (int16_t)((buf[4] << 8) | buf[5]);
	Raw->Temp = (int16_t)((buf[6] << 8) | buf[7]);
	Raw->Gx = (int16_t)((buf[8] << 8) | buf[9]);
	Raw->Gy = (int16_t)((buf[10] << 8) | buf[11]);
	Raw->Gz = (int16_t)((buf[12] << 8) | buf[13]);
}

float MPU6050_ReadGyroZ_dps(void)
{
	MPU6050_Raw_t raw;
	MPU6050_ReadRaw(&raw);

	return ((float)raw.Gz) / 16.4f;
}

float MPU6050_ReadGyroZ_dps_Calibrated(void)
{
	return MPU6050_ReadGyroZ_dps() - g_gyroZ_bias_dps;
}

void MPU6050_CalibrateGyroZ(uint16_t Samples, uint16_t DelayMs)
{
	float sum = 0.0f;
	for (uint16_t i = 0; i < Samples; i++)
	{
		sum += MPU6050_ReadGyroZ_dps();
		Delay_ms(DelayMs);
	}
	g_gyroZ_bias_dps = sum / (float)Samples;
}

// 调试：直接读取原始值
int16_t MPU6050_ReadGyroZ_Raw(void)
{
	MPU6050_Raw_t raw;
	MPU6050_ReadRaw(&raw);
	return raw.Gz;
}
