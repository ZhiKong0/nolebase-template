#include "stm32f10x.h"
#include "SoftSerial.h"
#include "Delay.h"

// 位时间计算（us）
// 115200: 1000000/115200 ≈ 8.68us
// 9600: 1000000/9600 ≈ 104.17us
#define BIT_TIME_US    (1000000 / SOFTSERIAL_BAUDRATE)

// 接收缓冲
#define RX_BUF_SIZE    64
static uint8_t rxBuffer[RX_BUF_SIZE];
static volatile uint8_t rxHead = 0;
static volatile uint8_t rxTail = 0;

// TX 引脚操作
#define TX_H()    GPIO_SetBits(SOFTSERIAL_GPIO, SOFTSERIAL_TX_PIN)
#define TX_L()    GPIO_ResetBits(SOFTSERIAL_GPIO, SOFTSERIAL_TX_PIN)
#define RX_READ() GPIO_ReadInputDataBit(SOFTSERIAL_GPIO, SOFTSERIAL_RX_PIN)

// 微秒延时（使用Delay模块）
static void SoftSerial_DelayUs(uint32_t us) {
    Delay_us(us);
}

// 初始化
void SoftSerial_Init(void) {
    // 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    
    // 禁用JTAG，释放PB14/PB15
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    
    // TX 配置为推挽输出
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = SOFTSERIAL_TX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SOFTSERIAL_GPIO, &GPIO_InitStructure);
    
    // RX 配置为上拉输入
    GPIO_InitStructure.GPIO_Pin = SOFTSERIAL_RX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(SOFTSERIAL_GPIO, &GPIO_InitStructure);
    
    // TX 空闲状态为高
    TX_H();
    
    // 清空接收缓冲
    rxHead = 0;
    rxTail = 0;
}

// 发送一个字节（阻塞方式）
// 格式：起始位(0) + 8数据位 + 停止位(1)
void SoftSerial_SendByte(uint8_t data) {
    uint8_t i;
    
    // 起始位
    TX_L();
    SoftSerial_DelayUs(BIT_TIME_US);
    
    // 8位数据（LSB先发）
    for (i = 0; i < 8; i++) {
        if (data & 0x01) {
            TX_H();
        } else {
            TX_L();
        }
        data >>= 1;
        SoftSerial_DelayUs(BIT_TIME_US);
    }
    
    // 停止位
    TX_H();
    SoftSerial_DelayUs(BIT_TIME_US);
}

// 发送数组
void SoftSerial_SendArray(uint8_t *data, uint16_t len) {
    uint16_t i;
    for (i = 0; i < len; i++) {
        SoftSerial_SendByte(data[i]);
    }
}

// 发送字符串
void SoftSerial_SendString(char *str) {
    while (*str) {
        SoftSerial_SendByte(*str++);
    }
}

// 发送 JustFloat 协议（3通道）
void SoftSerial_SendFloat3(float ch0, float ch1, float ch2) {
    uint8_t *ptr;
    uint8_t tail[4] = {0x00, 0x00, 0x80, 0x7f};
    
    // 发送 ch0
    ptr = (uint8_t *)&ch0;
    SoftSerial_SendByte(ptr[0]);
    SoftSerial_SendByte(ptr[1]);
    SoftSerial_SendByte(ptr[2]);
    SoftSerial_SendByte(ptr[3]);
    
    // 发送 ch1
    ptr = (uint8_t *)&ch1;
    SoftSerial_SendByte(ptr[0]);
    SoftSerial_SendByte(ptr[1]);
    SoftSerial_SendByte(ptr[2]);
    SoftSerial_SendByte(ptr[3]);
    
    // 发送 ch2
    ptr = (uint8_t *)&ch2;
    SoftSerial_SendByte(ptr[0]);
    SoftSerial_SendByte(ptr[1]);
    SoftSerial_SendByte(ptr[2]);
    SoftSerial_SendByte(ptr[3]);
    
    // 发送尾部
    SoftSerial_SendByte(tail[0]);
    SoftSerial_SendByte(tail[1]);
    SoftSerial_SendByte(tail[2]);
    SoftSerial_SendByte(tail[3]);
}

// 检查是否有接收数据
uint8_t SoftSerial_Available(void) {
    return (rxHead != rxTail);
}

// 读取一个字节
uint8_t SoftSerial_ReadByte(void) {
    if (rxHead != rxTail) {
        uint8_t data = rxBuffer[rxTail];
        rxTail = (rxTail + 1) % RX_BUF_SIZE;
        return data;
    }
    return 0;
}

// 接收轮询（在主循环中调用）
// 返回：1=接收到数据，0=无数据
uint8_t SoftSerial_Poll(void) {
    static uint8_t rxState = 0;
    static uint8_t rxData = 0;
    static uint8_t rxBitCount = 0;
    static uint32_t lastTick = 0;
    
    // 检测起始位（RX下降沿）
    if (rxState == 0 && RX_READ() == 0) {
        rxState = 1;
        rxBitCount = 0;
        rxData = 0;
        lastTick = SysTick->VAL;  // 记录时间
        return 0;
    }
    
    // 接收数据位（简化：使用延时采样）
    if (rxState == 1) {
        // 等待半个位时间后采样
        SoftSerial_DelayUs(BIT_TIME_US / 2);
        
        // 采样8位数据
        if (rxBitCount < 8) {
            if (RX_READ()) {
                rxData |= (1 << rxBitCount);
            }
            rxBitCount++;
            SoftSerial_DelayUs(BIT_TIME_US);
        } else {
            // 停止位
            SoftSerial_DelayUs(BIT_TIME_US);
            rxState = 0;
            
            // 存入缓冲
            rxBuffer[rxHead] = rxData;
            rxHead = (rxHead + 1) % RX_BUF_SIZE;
            return 1;
        }
    }
    
    return 0;
}
