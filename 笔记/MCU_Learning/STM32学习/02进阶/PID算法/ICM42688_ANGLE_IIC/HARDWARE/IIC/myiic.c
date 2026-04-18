#include "myiic.h"
#include "delay.h"  
/**************************实现函数********************************************
*函数原型:		void IIC_Init(void)
*功　　能:		初始化I2C对应的接口引脚。
*******************************************************************************/
void IIC_Init(void)
{			
	GPIO_InitTypeDef GPIO_InitStructure;
 	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);			     
 	//配置PB12 PB13 为开漏输出
 	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13;	
  	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;       
  	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  	//应用配置到GPIOB 
  	GPIO_Init(GPIOB, &GPIO_InitStructure);
}

/**************************实现函数********************************************
*函数原型:		void IIC_Start(void)
*功　　能:		产生IIC起始信号
*******************************************************************************/
void IIC_Start(void)
{
	SDA_OUT();     //sda线输出
	IIC_SDA=1;	  	  
	IIC_SCL=1;
	delay_us(4);
 	IIC_SDA=0;//START:when CLK is high,DATA change form high to low 
	delay_us(4);
	IIC_SCL=0;//钳住I2C总线，准备发送或接收数据 
}

/**************************实现函数********************************************
*函数原型:		void IIC_Stop(void)
*功　　能:	    //产生IIC停止信号
*******************************************************************************/	  
void IIC_Stop(void)
{
	SDA_OUT();//sda线输出
	IIC_SCL=0;
	IIC_SDA=0;//STOP:when CLK is high DATA change form low to high
 	delay_us(4);
	IIC_SCL=1; 
	IIC_SDA=1;//发送I2C总线结束信号
	delay_us(4);							   	
}

/**************************实现函数********************************************
*函数原型:		u8 IIC_Wait_Ack(void)
*功　　能:	    等待应答信号到来 
//返回值：1，接收应答失败
//        0，接收应答成功
*******************************************************************************/
u8 IIC_Wait_Ack(void)
{
	u8 ucErrTime=0;
	SDA_IN();      //SDA设置为输入  
	IIC_SDA=1;delay_us(1);	   
	IIC_SCL=1;delay_us(1);	 
	while(READ_SDA)
	{
		ucErrTime++;
		if(ucErrTime>50)
		{
			IIC_Stop();
			return 1;
		}
	  delay_us(1);
	}
	IIC_SCL=0;//时钟输出0 	   
	return 0;  
} 

/**************************实现函数********************************************
*函数原型:		void IIC_Ack(void)
*功　　能:	    产生ACK应答
*******************************************************************************/
void IIC_Ack(void)
{
	IIC_SCL=0;
	SDA_OUT();
	IIC_SDA=0;
	delay_us(2);
	IIC_SCL=1;
	delay_us(2);
	IIC_SCL=0;
}
	
/**************************实现函数********************************************
*函数原型:		void IIC_NAck(void)
*功　　能:	    产生NACK应答
*******************************************************************************/	    
void IIC_NAck(void)
{
	IIC_SCL=0;
	SDA_OUT();
	IIC_SDA=1;
	delay_us(2);
	IIC_SCL=1;
	delay_us(2);
	IIC_SCL=0;
}					 				     

/**************************实现函数********************************************
*函数原型:		void IIC_Send_Byte(u8 txd)
*功　　能:	    IIC发送一个字节
*******************************************************************************/		  
void IIC_Send_Byte(u8 txd)
{                        
    u8 t;   
	SDA_OUT(); 	    
    IIC_SCL=0;//拉低时钟开始数据传输
    for(t=0;t<8;t++)
    {              
        IIC_SDA=(txd&0x80)>>7;
        txd<<=1; 	  
		delay_us(2);   
		IIC_SCL=1;
		delay_us(2); 
		IIC_SCL=0;	
		delay_us(2);
    }	 
} 	 
   
/**************************实现函数********************************************
*函数原型:		u8 IIC_Read_Byte(unsigned char ack)
*功　　能:	    //读1个字节，ack=1时，发送ACK，ack=0，发送nACK 
*******************************************************************************/  
u8 IIC_Read_Byte(unsigned char ack)
{
	unsigned char i,receive=0;
	SDA_IN();//SDA设置为输入
    for(i=0;i<8;i++ )
	{
        IIC_SCL=0; 
        delay_us(2);
		IIC_SCL=1;
        receive<<=1;
        if(READ_SDA)receive++;   
		delay_us(2); 
    }					 
    if (ack)
        IIC_Ack(); //发送ACK 
    else
        IIC_NAck();//发送nACK  
    return receive;
}

/**************************实现函数********************************************
*函数原型:		unsigned char I2C_ReadOneByte(unsigned char I2C_Addr,unsigned char addr)
*功　　能:	    读取指定设备 指定寄存器的一个值
输入	I2C_Addr  目标设备地址
		addr	   寄存器地址
返回   读出来的值
*******************************************************************************/ 
unsigned char I2C_ReadOneByte(unsigned char I2C_Addr,unsigned char addr)
{
	unsigned char res=0;
	
	IIC_Start();	
	IIC_Send_Byte(I2C_Addr);	   //发送写命令
	res++;
	IIC_Wait_Ack();
	IIC_Send_Byte(addr); res++;  //发送地址
	IIC_Wait_Ack();	  
	//IIC_Stop();//产生一个停止条件	
	IIC_Start();
	IIC_Send_Byte(I2C_Addr+1); res++;          //进入接收模式			   
	IIC_Wait_Ack();
	res=IIC_Read_Byte(0);	   
    IIC_Stop();//产生一个停止条件

	return res;
}


/**************************实现函数********************************************
*函数原型:		u8 IICreadBytes(u8 dev, u8 reg, u8 length, u8 *data)
*功　　能:	    读取指定设备 指定寄存器的 length个值
输入	dev  目标设备地址
		reg	  寄存器地址
		length 要读的字节数
		*data  读出的数据将要存放的指针
返回   读出来的字节数量
*******************************************************************************/ 
u8 IICreadBytes(u8 dev, u8 reg, u8 length, u8 *data){
  u8 count = 0;
	u8 temp;
	IIC_Start();
	IIC_Send_Byte(dev);	   //发送写命令
	IIC_Wait_Ack();
	IIC_Send_Byte(reg);   //发送地址
    IIC_Wait_Ack();	  
	IIC_Start();
	IIC_Send_Byte(dev+1);  //进入接收模式	
	IIC_Wait_Ack();
    for(count=0;count<length;count++){
		 
		 if(count!=(length-1))
		 	temp = IIC_Read_Byte(1);  //带ACK的读数据
		 	else  
			temp = IIC_Read_Byte(0);	 //最后一个字节NACK

		data[count] = temp;
	}
    IIC_Stop();//产生一个停止条件
    return count;
}

/**************************实现函数********************************************
*函数原型:		u8 IICwriteBytes(u8 dev, u8 reg, u8 length, u8* data)
*功　　能:	    将多个字节写入指定设备 指定寄存器
输入	dev  目标设备地址
		reg	  寄存器地址
		length 要写的字节数
		*data  将要写的数据的首地址
返回   返回是否成功
*******************************************************************************/ 
u8 IICwriteBytes(u8 dev, u8 reg, u8 length, u8* data){
  
 	u8 count = 0;
	IIC_Start();
	IIC_Send_Byte(dev);	   //发送写命令
	IIC_Wait_Ack();
	IIC_Send_Byte(reg);   //发送地址
    IIC_Wait_Ack();	  
	for(count=0;count<length;count++){
		IIC_Send_Byte(data[count]); 
		IIC_Wait_Ack(); 
	 }
	IIC_Stop();//产生一个停止条件

    return 1; //status == 0;
}

/**************************实现函数********************************************
*函数原型:		u8 IICreadByte(u8 dev, u8 reg, u8 *data)
*功　　能:	    读取指定设备 指定寄存器的一个值
输入	dev  目标设备地址
		reg	   寄存器地址
		*data  读出的数据将要存放的地址
返回   1
*******************************************************************************/ 
u8 IICreadByte(u8 dev, u8 reg, u8 *data){
	*data=I2C_ReadOneByte(dev, reg);
    return 1;
}

/**************************实现函数********************************************
*函数原型:		unsigned char IICwriteByte(unsigned char dev, unsigned char reg, unsigned char data)
*功　　能:	    写入指定设备 指定寄存器一个字节
输入	dev  目标设备地址
		reg	   寄存器地址
		data  将要写入的字节
返回   1
*******************************************************************************/ 
unsigned char IICwriteByte(unsigned char dev, unsigned char reg, unsigned char data){
    return IICwriteBytes(dev, reg, 1, &data);
}

/**************************实现函数********************************************
*函数原型:		u8 IICwriteBits(u8 dev,u8 reg,u8 bitStart,u8 length,u8 data)
*功　　能:	    读 修改 写 指定设备 指定寄存器一个字节 中的多个位
输入	dev  目标设备地址
		reg	   寄存器地址
		bitStart  目标字节的起始位
		length   位长度
		data    存放改变目标字节位的值
返回   成功 为1 
 		失败为0
*******************************************************************************/ 
u8 IICwriteBits(u8 dev,u8 reg,u8 bitStart,u8 length,u8 data)
{

    u8 b;
    if (IICreadByte(dev, reg, &b) != 0) {
        u8 mask = (0xFF << (bitStart + 1)) | 0xFF >> ((8 - bitStart) + length - 1);
        data <<= (8 - length);
        data >>= (7 - bitStart);
        b &= mask;
        b |= data;
        return IICwriteByte(dev, reg, b);
    } else {
        return 0;
    }
}
//

/**************************实现函数********************************************
*函数原型:		u8 IICwriteBit(u8 dev, u8 reg, u8 bitNum, u8 data)
*功　　能:	    读 修改 写 指定设备 指定寄存器一个字节 中的1个位
输入	dev  目标设备地址
		reg	   寄存器地址
		bitNum  要修改目标字节的bitNum位
		data  为0 时，目标位将被清0 否则将被置位
返回   成功 为1 
 		失败为0
*******************************************************************************/ 
u8 IICwriteBit(u8 dev, u8 reg, u8 bitNum, u8 data){
    u8 b;
    IICreadByte(dev, reg, &b);
    b = (data != 0) ? (b | (1 << bitNum)) : (b & ~(1 << bitNum));
    return IICwriteByte(dev, reg, b);
}
/*********************************************/
/********other iic****************************/



/******************************************************************************
 * 函数名称: I2c_delay
 * 函数功能: I2c 延时函数
 * 入口参数: 无
 ******************************************************************************/
static void I2c_delay(void)
{
    volatile int i = 7;
    while (i)
        i--;
}



/******************************************************************************
 * 函数名称: I2c_Start
 * 函数功能: I2c  起始信号
 * 入口参数: 无
 ******************************************************************************/
static uint8_t I2c_Start(void)
{
		SDA_OUT();     //sda线输出
    SDA_H;
    SCL_H;
    I2c_delay();
    if (!SDA_read)
        return false;
    SDA_L;
    I2c_delay();
    if (SDA_read)
        return false;
    SDA_L;
    I2c_delay();
    return true;
}

/******************************************************************************
 * 函数名称: I2c_Stop
 * 函数功能: I2c  停止信号
 * 入口参数: 无
 ******************************************************************************/
static void I2c_Stop(void)
{
		SDA_OUT();     //sda线输出
    SCL_L;
    I2c_delay();
    SDA_L;
    I2c_delay();
    SCL_H;
    I2c_delay();
    SDA_H;
    I2c_delay();
}

/******************************************************************************
 * 函数名称: I2c_Ack
 * 函数功能: I2c  产生应答信号
 * 入口参数: 无
 ******************************************************************************/
static void I2c_Ack(void)
{
		SDA_OUT();
    SCL_L;
    I2c_delay();
    SDA_L;
    I2c_delay();
    SCL_H;
    I2c_delay();
    SCL_L;
    I2c_delay();
}

/******************************************************************************
 * 函数名称: I2c_NoAck
 * 函数功能: I2c  产生NAck
 * 入口参数: 无
 ******************************************************************************/
static void I2c_NoAck(void)
{
    SCL_L;
		SDA_OUT();
    I2c_delay();
    SDA_H;
    I2c_delay();
    SCL_H;
    I2c_delay();
    SCL_L;
    I2c_delay();
}

/*******************************************************************************
 *函数名称:	I2c_WaitAck
 *函数功能:	等待应答信号到来
 *返回值：   1，接收应答失败
 *           0，接收应答成功
 *******************************************************************************/
static uint8_t I2c_WaitAck(void)
{
		SDA_IN();      //SDA设置为输入  
    SCL_L;
    I2c_delay();
    SDA_H;
    I2c_delay();
    SCL_H;
    I2c_delay();
    if (SDA_read) {
        SCL_L;
        return false;
    }
    SCL_L;
    return true;
}

/******************************************************************************
 * 函数名称: I2c_SendByte
 * 函数功能: I2c  发送一个字节数据
 * 入口参数: byte  发送的数据
 ******************************************************************************/
static void I2c_SendByte(uint8_t byte)
{
    uint8_t i = 8;
		SDA_OUT(); 	    

    while (i--) {
        SCL_L;
        I2c_delay();
        if (byte & 0x80)
            SDA_H;
        else
            SDA_L;
        byte <<= 1;
        I2c_delay();
        SCL_H;
        I2c_delay();
    }
    SCL_L;
}

/******************************************************************************
 * 函数名称: I2c_ReadByte
 * 函数功能: I2c  读取一个字节数据
 * 入口参数: 无
 * 返回值	 读取的数据
 ******************************************************************************/
static uint8_t I2c_ReadByte(void)
{
    uint8_t i = 8;
    uint8_t byte = 0;
	SDA_IN();//SDA设置为输入

    SDA_H;
    while (i--) {
        byte <<= 1;
        SCL_L;
        I2c_delay();
        SCL_H;
        I2c_delay();
        if (SDA_read) {
            byte |= 0x01;
        }
    }
    SCL_L;
    return byte;
}

/******************************************************************************
 * 函数名称: i2cWriteBuffer
 * 函数功能: I2c       向设备的某一个地址写入固定长度的数据
 * 入口参数: addr,     设备地址
 *           reg，     寄存器地址
 *			 len，     数据长度
 *			 *data	   数据指针
 * 返回值	 1
 ******************************************************************************/
uint8_t i2cWriteBuffer(uint8_t addr, uint8_t reg, uint8_t len, uint8_t * data)
{
    int i;
    if (!I2c_Start())
        return false;
    I2c_SendByte(addr << 1 | I2C_Direction_Trans);
    if (!I2c_WaitAck()) {
        I2c_Stop();
        return false;
    }
    I2c_SendByte(reg);
    I2c_WaitAck();
    for (i = 0; i < len; i++) {
        I2c_SendByte(data[i]);
        if (!I2c_WaitAck()) {
            I2c_Stop();
            return false;
        }
    }
    I2c_Stop();
    return true;
}
/////////////////////////////////////////////////////////////////////////////////
int8_t i2cwrite(uint8_t addr, uint8_t reg, uint8_t len, uint8_t * data)
{
	if(i2cWriteBuffer(addr,reg,len,data))
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
	//return FALSE;
}

/******************************************************************************
 * 函数名称: i2cread
 * 函数功能: I2c  向设备的某一个地址读取固定长度的数据
 * 入口参数: addr,   设备地址
 *           reg，   寄存器地址首地址
 *			 len，   数据长度
 *			 *buf	 数据指针
 * 返回值	 成功 返回 TRUE
 *           错误 返回 FALSE
 ******************************************************************************/
int8_t i2cread(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
	if(i2cRead(addr,reg,len,buf))
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
	//return FALSE;
}

/*****************************************************************************
 *函数名称:	i2cWrite
 *函数功能:	写入指定设备 指定寄存器一个字节
 *入口参数： addr 目标设备地址
 *		     reg   寄存器地址
 *		     data 读出的数据将要存放的地址
 *******************************************************************************/
uint8_t i2cWrite(uint8_t addr, uint8_t reg, uint8_t data)
{
    if (!I2c_Start())
        return false;
    I2c_SendByte(addr << 1 | I2C_Direction_Trans);
    if (!I2c_WaitAck()) {
        I2c_Stop();
        return false;
    }
    I2c_SendByte(reg);
    I2c_WaitAck();
    I2c_SendByte(data);
    I2c_WaitAck();
    I2c_Stop();
    return true;
}

uint8_t i2cRead(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    if (!I2c_Start())
        return false;
    I2c_SendByte(addr << 1 | I2C_Direction_Trans);
    if (!I2c_WaitAck()) {
        I2c_Stop();
        return false;
    }
    I2c_SendByte(reg);
    I2c_WaitAck();
    I2c_Start();
    I2c_SendByte(addr << 1 | I2C_Direction_Rec);
    I2c_WaitAck();
    while (len) {
        *buf = I2c_ReadByte();
        if (len == 1)
            I2c_NoAck();
        else
            I2c_Ack();
        buf++;
        len--;
    }
    I2c_Stop();
    return true;
}

//------------------End of File----------------------------
