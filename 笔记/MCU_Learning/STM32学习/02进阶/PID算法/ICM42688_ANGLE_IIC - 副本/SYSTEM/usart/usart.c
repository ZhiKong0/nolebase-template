#include "sys.h"
#include "usart.h"	  
////////////////////////////////////////////////////////////////////////////////// 	 
//如果使用ucos,则包括下面的头文件即可.
#if SYSTEM_SUPPORT_UCOS
#include "includes.h"					//ucos 使用	  
#endif
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32开发板
//串口1初始化		   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//修改日期:2012/8/18
//版本：V1.5
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2009-2019
//All rights reserved
//********************************************************************************
//V1.3修改说明 
//支持适应不同频率下的串口波特率设置.
//加入了对printf的支持
//增加了串口接收命令功能.
//修正了printf第一个字符丢失的bug
//V1.4修改说明
//1,修改串口初始化IO的bug
//2,修改了USART_RX_STA,使得串口最大接收字节数为2的14次方
//3,增加了USART_REC_LEN,用于定义串口最大允许接收的字节数(不大于2的14次方)
//4,修改了EN_USART1_RX的使能方式
//V1.5修改说明
//1,增加了对UCOSII的支持
////////////////////////////////////////////////////////////////////////////////// 	  
 
#define USART2_TX_BUF_SIZE 2048
static volatile uint16_t g_txHead = 0;
static volatile uint16_t g_txTail = 0;
static uint8_t g_txBuf[USART2_TX_BUF_SIZE];
static volatile uint32_t g_txDropBytes = 0;

#define USART2_RX_RING_SIZE 256
static volatile uint16_t g_rxHead = 0;
static volatile uint16_t g_rxTail = 0;
static uint8_t g_rxBuf[USART2_RX_RING_SIZE];
static volatile uint32_t g_rxDropBytes = 0;

static uint16_t tx_next(uint16_t v)
{
	v++;
	if (v >= (uint16_t)USART2_TX_BUF_SIZE) v = 0;
	return v;
}

static void USART2_TxKick(void)
{
	USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
}

void USART2_SendByte(uint8_t b)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	{
		uint16_t next = tx_next(g_txHead);
		if (next != g_txTail)
		{
			g_txBuf[g_txHead] = b;
			g_txHead = next;
			USART2_TxKick();
		}
		else
		{
			g_txDropBytes++;
		}
	}
	if (primask == 0u) __enable_irq();
}

void USART2_SendBuffer(const uint8_t* buf, uint16_t len)
{
	uint16_t i;
	if (!buf) return;
	for (i = 0; i < len; i++)
	{
		USART2_SendByte(buf[i]);
	}
}

void USART2_SendString(const char* s)
{
	if (!s) return;
	while (*s)
	{
		USART2_SendByte((uint8_t)(*s++));
	}
}

static uint16_t rx_next(uint16_t v)
{
	v++;
	if (v >= (uint16_t)USART2_RX_RING_SIZE) v = 0;
	return v;
}

uint16_t USART2_RxAvailable(void)
{
	uint16_t head;
	uint16_t tail;
	uint16_t n;
	uint32_t primask;

	primask = __get_PRIMASK();
	__disable_irq();
	head = g_rxHead;
	tail = g_rxTail;
	if (primask == 0u) __enable_irq();

	if (head >= tail) n = (uint16_t)(head - tail);
	else n = (uint16_t)(USART2_RX_RING_SIZE - (tail - head));
	return n;
}

int USART2_ReadByte(uint8_t* out)
{
	uint32_t primask;
	if (!out) return 0;

	primask = __get_PRIMASK();
	__disable_irq();
	if (g_rxTail == g_rxHead)
	{
		if (primask == 0u) __enable_irq();
		return 0;
	}
	*out = g_rxBuf[g_rxTail];
	g_rxTail = rx_next(g_rxTail);
	if (primask == 0u) __enable_irq();
	return 1;
}
#define RX_BUFFER_SIZE 100
#define b_uart_head  0x80
#define b_rx_over    0x40

//////////////////////////////////////////////////////////////////
//加入以下代码,支持printf函数,而不需要选择use MicroLIB	  
#if 1
#pragma import(__use_no_semihosting)             
//标准库需要的支持函数                 
struct __FILE 
{ 
	int handle; 

}; 

FILE __stdout;       
//定义_sys_exit()以避免使用半主机模式    
_sys_exit(int x) 
{ 
	x = x; 
} 
//重定义fputc函数 
int fputc(int ch, FILE *f)
{      
	USART2_SendByte((uint8_t)ch);
	return ch;
}
#endif 

/*使用microLib的方法*/
 /* 
int fputc(int ch, FILE *f)
{
	USART_SendData(USART2, (uint8_t) ch);

	while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET) {}	
   
    return ch;
}
int GetKey (void)  { 

    while (!(USART2->SR & USART_FLAG_RXNE));

    return ((int)(USART2->DR & 0x1FF));
}
*/
 
#if EN_USART1_RX   //如果使能了接收
//串口1中断服务程序
//注意,读取USARTx->SR能避免莫名其妙的错误   	
u8 USART_RX_BUF[USART_REC_LEN];     //接收缓冲,最大USART_REC_LEN个字节.
//接收状态
//bit15，	接收完成标志
//bit14，	接收到0x0d
//bit13~0，	接收到的有效字节数目
u16 USART_RX_STA=0;       //接收状态标记	  

//初始化IO 串口1 
//bound:波特率
void uart_init(u32 bound){
    //GPIO端口设置
    GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	//使能GPIOA时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);	//使能USART2时钟
	USART_DeInit(USART2);  //复位串口2
 	//USART2_TX   PA.2
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2; //PA.2
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
    GPIO_Init(GPIOA, &GPIO_InitStructure); //初始化PA2
   
    //USART2_RX	  PA.3
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空输入
    GPIO_Init(GPIOA, &GPIO_InitStructure);  //初始化PA3

   //Usart1 NVIC 配置

    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=3 ;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器
  
   //USART 初始化设置

	USART_InitStructure.USART_BaudRate = bound;//一般设置为9600;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

    USART_Init(USART2, &USART_InitStructure); //初始化串口
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);//开启中断
    USART_Cmd(USART2, ENABLE);                    //使能串口 

}
void UART1_Put_Char(unsigned char DataToSend)
{
	USART2_SendByte((uint8_t)DataToSend);
}

//发送的数据为原码  为什么？ 你猜！
void UART1_ReportIMU(int16_t yaw,int16_t pitch,int16_t roll
,int16_t ax,int16_t ay,int16_t az,int16_t gx,int16_t gy,int16_t gz,
int16_t hx,int16_t hy,int16_t hz,int32_t alt,int16_t tempr,int32_t press)	  //A1
{
 	unsigned int temp=0x23;
	char ctemp;
	UART1_Put_Char(0xa5);
	UART1_Put_Char(0x5a);
	UART1_Put_Char(35);

	if(yaw<0)yaw=32768-yaw;  
	ctemp=yaw>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=yaw;							
	UART1_Put_Char(ctemp);
	temp+=ctemp;

	if(pitch<0)pitch=32768-pitch;
	ctemp=pitch>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=pitch;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
								 
	if(roll<0)roll=32768-roll;
	ctemp=roll>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=roll;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	
	if(ax<0)ax=32768-ax;
	ctemp=ax>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=ax;
	UART1_Put_Char(ctemp);
	temp+=ctemp;

	if(ay<0)ay=32768-ay;
	ctemp=ay>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=ay;
	UART1_Put_Char(ctemp);
	temp+=ctemp;

	if(az<0)az=32768-az;
	ctemp=az>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=az;
	UART1_Put_Char(ctemp);
	temp+=ctemp;

	if(gx<0)gx=32768-gx;
	ctemp=gx>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=gx;
	UART1_Put_Char(ctemp);
	temp+=ctemp;

	if(gy<0)gy=32768-gy;
	ctemp=gy>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=gy;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
//-------------------------
	if(gz<0)gz=32768-gz;
	ctemp=gz>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=gz;
	UART1_Put_Char(ctemp);
	temp+=ctemp;

	if(hx<0)hx=32768-hx;
	ctemp=hx>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=hx;
	UART1_Put_Char(ctemp);
	temp+=ctemp;

	if(hy<0)hy=32768-hy;
	ctemp=hy>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=hy;
	UART1_Put_Char(ctemp);
	temp+=ctemp;

	if(hz<0)hz=32768-hz;
	ctemp=hz>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=hz;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	
  if(press<0)press=2147483648-alt;
	ctemp=alt>>24;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=alt>>16;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=alt>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=alt;
	UART1_Put_Char(ctemp);
	temp+=ctemp;

	if(tempr<0)tempr=32768-tempr;
	ctemp=tempr>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=tempr;
	UART1_Put_Char(ctemp);	   
	temp+=ctemp;

  if(press<0)press=2147483648-press;
	ctemp=press>>24;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=press>>16;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=press>>8;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	ctemp=press;
	UART1_Put_Char(ctemp);
	temp+=ctemp;
	UART1_Put_Char(temp%256);
	UART1_Put_Char(0xaa);
}

volatile unsigned char rx_buffer[RX_BUFFER_SIZE];
volatile unsigned char rx_wr_index;
volatile unsigned char RC_Flag;
//------------------------------------------------------
void USART2_IRQHandler(void)
{
  unsigned char data;

	if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET)
	{
		if (g_txTail != g_txHead)
		{
			USART_SendData(USART2, g_txBuf[g_txTail]);
			g_txTail = tx_next(g_txTail);
		}
		else
		{
			USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
		}
		USART_ClearITPendingBit(USART2, USART_IT_TXE);
	}

	if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
	{
		uint16_t next;
		data = (unsigned char)USART_ReceiveData(USART2);

		next = rx_next(g_rxHead);
		if (next != g_rxTail)
		{
			g_rxBuf[g_rxHead] = (uint8_t)data;
			g_rxHead = next;
		}
		else
		{
			g_rxDropBytes++;
		}

		if (data == 0xa5)
		{
			RC_Flag |= b_uart_head;
			rx_buffer[rx_wr_index++] = data;
		}
		else if (data == 0x5a)
		{
			if (RC_Flag & b_uart_head)
			{
				rx_wr_index = 0;
				RC_Flag &= ~b_rx_over;
			}
			else
				rx_buffer[rx_wr_index++] = data;
			RC_Flag &= ~b_uart_head;
		}
		else
		{
			rx_buffer[rx_wr_index++] = data;
			RC_Flag &= ~b_uart_head;
			if (rx_wr_index == rx_buffer[0])
			{
				RC_Flag |= b_rx_over;
			}
		}
		if (rx_wr_index == RX_BUFFER_SIZE) rx_wr_index--;

		USART_ClearITPendingBit(USART2, USART_IT_RXNE);
	}
}

/*
+------------------------------------------------------------------------------
| Function    : Sum_check(void)
+------------------------------------------------------------------------------
| Description : check
|
| Parameters  : 
| Returns     : 
|
+------------------------------------------------------------------------------
*/
unsigned char Sum_check(void)
{ 
  unsigned char i;
  unsigned int checksum=0; 
  for(i=0;i<rx_buffer[0]-2;i++)
   checksum+=rx_buffer[i];
  if((checksum%256)==rx_buffer[rx_buffer[0]-2])
   return(0x01); //Checksum successful
  else
   return(0x00); //Checksum error
}

unsigned char UART1_CommandRoute(void)
{
 if(RC_Flag&b_rx_over){
		RC_Flag&=~b_rx_over;
		if(Sum_check()){
		return rx_buffer[1];
		}
	}
return 0xff; //没有收到上位机的命令，或者是命令效验没有通过
}
#endif	

