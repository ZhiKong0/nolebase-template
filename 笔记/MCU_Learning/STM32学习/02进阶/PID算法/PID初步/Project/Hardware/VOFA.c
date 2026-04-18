#include "stm32f10x.h"
#include "VOFA.h"
#include "SoftSerial.h"
#include <string.h>
#include <stdlib.h>
 #include <stdio.h>

// 接收缓冲区
#define VOFA_RX_BUF_SIZE    32
static char rxBuffer[VOFA_RX_BUF_SIZE];
static uint8_t rxIndex = 0;
static VOFA_State_t rxState = VOFA_STATE_WAIT_HEAD;

// 输出通道选择
static VOFA_Channel_t outputChannel = VOFA_CHANNEL_BOTH;

// 初始化 USART2 (PA2=TX, PA3=RX)
void VOFA_Init(void) {
    // 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    
    // GPIO 配置
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    
    // TX (PA2) 复用推挽
    GPIO_InitStructure.GPIO_Pin = VOFA_TX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(VOFA_GPIO_PORT, &GPIO_InitStructure);
    
    // RX (PA3) 上拉输入
    GPIO_InitStructure.GPIO_Pin = VOFA_RX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(VOFA_GPIO_PORT, &GPIO_InitStructure);
    
    // USART 配置
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = VOFA_BAUDRATE;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_Init(USART2, &USART_InitStructure);
    
    // 使能接收中断
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    
    // NVIC 配置
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);
    
    // 使能 USART
    USART_Cmd(USART2, ENABLE);

    // 上电自检输出（不依赖主循环/定时器）
    {
        uint32_t csr = RCC->CSR;
        RCC_ClearFlag();
        char bootMsg[96];
        snprintf(bootMsg, sizeof(bootMsg),
                 "BOOT csr=%08lX POR=%u PIN=%u SFT=%u IWDG=%u WWDG=%u LPWR=%u\r\n",
                 (unsigned long)csr,
                 (unsigned)((csr & RCC_CSR_PORRSTF) != 0),
                 (unsigned)((csr & RCC_CSR_PINRSTF) != 0),
                 (unsigned)((csr & RCC_CSR_SFTRSTF) != 0),
                 (unsigned)((csr & RCC_CSR_IWDGRSTF) != 0),
                 (unsigned)((csr & RCC_CSR_WWDGRSTF) != 0),
                 (unsigned)((csr & RCC_CSR_LPWRRSTF) != 0));
        {
            const char *s = bootMsg;
            while (*s) {
                while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
                USART_SendData(USART2, (uint8_t)(*s++));
            }
        }
    }
    
    // 初始化软件串口（蓝牙）
    SoftSerial_Init();
    
    // 初始化状态
    rxIndex = 0;
    rxState = VOFA_STATE_WAIT_HEAD;
    memset(rxBuffer, 0, VOFA_RX_BUF_SIZE);
}

// 设置输出通道
void VOFA_SetChannel(VOFA_Channel_t channel) {
    outputChannel = channel;
}

// 发送一个字节（USB通道）
static void VOFA_SendByte_USB(uint8_t byte) {
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, byte);
}

void VOFA_SendString(const char *s) {
    if (!s) return;
    while (*s) {
        VOFA_SendByte_USB((uint8_t)*s++);
    }
}

uint8_t VOFA_TakeCommand(char *out, uint8_t outSize) {
    uint8_t i;
    if (!out || outSize == 0) return 0;
    if (rxState != VOFA_STATE_COMPLETE) return 0;
    if (rxIndex < 2) {
        rxState = VOFA_STATE_WAIT_HEAD;
        rxIndex = 0;
        out[0] = '\0';
        return 0;
    }
    if (rxBuffer[0] != '#' || rxBuffer[rxIndex - 1] != '!') {
        rxState = VOFA_STATE_WAIT_HEAD;
        rxIndex = 0;
        out[0] = '\0';
        return 0;
    }
    for (i = 0; i < (uint8_t)(rxIndex - 1) && i < (uint8_t)(outSize - 1); i++) {
        out[i] = rxBuffer[i];
    }
    out[i] = '\0';
    rxState = VOFA_STATE_WAIT_HEAD;
    rxIndex = 0;
    return 1;
}

// 发送 JustFloat 协议（3通道）
// VOFA+ JustFloat协议: 连续发送float数据，尾部为0x00 0x00 0x80 0x7f
void VOFA_SendFloat3(float ch0, float ch1, float ch2) {
    uint8_t *ptr;
    uint8_t tail[4] = {0x00, 0x00, 0x80, 0x7f};
    
    // USB通道发送
    if (outputChannel == VOFA_CHANNEL_USB || outputChannel == VOFA_CHANNEL_BOTH) {
        // 发送 ch0
        ptr = (uint8_t *)&ch0;
        VOFA_SendByte_USB(ptr[0]);
        VOFA_SendByte_USB(ptr[1]);
        VOFA_SendByte_USB(ptr[2]);
        VOFA_SendByte_USB(ptr[3]);
        
        // 发送 ch1
        ptr = (uint8_t *)&ch1;
        VOFA_SendByte_USB(ptr[0]);
        VOFA_SendByte_USB(ptr[1]);
        VOFA_SendByte_USB(ptr[2]);
        VOFA_SendByte_USB(ptr[3]);
        
        // 发送 ch2
        ptr = (uint8_t *)&ch2;
        VOFA_SendByte_USB(ptr[0]);
        VOFA_SendByte_USB(ptr[1]);
        VOFA_SendByte_USB(ptr[2]);
        VOFA_SendByte_USB(ptr[3]);
        
        // 发送尾部
        VOFA_SendByte_USB(tail[0]);
        VOFA_SendByte_USB(tail[1]);
        VOFA_SendByte_USB(tail[2]);
        VOFA_SendByte_USB(tail[3]);
    }
    
    // 蓝牙通道发送
    if (outputChannel == VOFA_CHANNEL_BLUETOOTH || outputChannel == VOFA_CHANNEL_BOTH) {
        SoftSerial_SendFloat3(ch0, ch1, ch2);
    }
}

// USART2 中断服务函数
// 使用状态机方式解析数据包
// 帧头识别: '#'
// 帧尾识别: '!'
void USART2_IRQHandler(void) {
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t data = USART_ReceiveData(USART2);
        
        switch (rxState) {
            case VOFA_STATE_WAIT_HEAD:
                // 等待帧头 '#'
                if (data == '#') {
                    rxIndex = 0;
                    rxBuffer[rxIndex++] = data;
                    rxState = VOFA_STATE_RECEIVING;
                }
                break;
                
            case VOFA_STATE_RECEIVING:
                // 接收数据中
                if (data == '!') {
                    // 收到帧尾，接收完成
                    if (rxIndex < VOFA_RX_BUF_SIZE) {
                        rxBuffer[rxIndex++] = data;
                    }
                    rxState = VOFA_STATE_COMPLETE;
                } else if (data == '#') {
                    // 收到新的帧头，重新开始
                    rxIndex = 0;
                    rxBuffer[rxIndex++] = data;
                } else {
                    // 普通数据
                    if (rxIndex < VOFA_RX_BUF_SIZE) {
                        rxBuffer[rxIndex++] = data;
                    } else {
                        // 缓冲区溢出，重新等待帧头
                        rxState = VOFA_STATE_WAIT_HEAD;
                    }
                }
                break;
                
            case VOFA_STATE_COMPLETE:
                // 已完成，等待主循环处理
                // 如果收到新数据，说明上一包未处理，丢弃
                if (data == '#') {
                    rxIndex = 0;
                    rxBuffer[rxIndex++] = data;
                    rxState = VOFA_STATE_RECEIVING;
                }
                break;
        }
    }
}

// 获取当前接收状态
VOFA_State_t VOFA_GetState(void) {
    return rxState;
}

// 获取接收到的命令字符串
const char* VOFA_GetCommand(void) {
    return rxBuffer;
}

// 解析参数命令
// 格式: #P1=1.50! (设置KP=1.5)
//       #P2=0.02! (设置KI=0.02)
//       #P3=0.24! (设置KD=0.24)
// 解析流程:
// 1. 检查状态是否为 COMPLETE
// 2. 检查帧头 '#'
// 3. 提取参数编号 P1/P2/P3
// 4. 提取等号后的数值
// 5. 更新 PID 参数并重置
uint8_t VOFA_ParseCommand(PID_t *pid) {
    // 检查是否接收完成
    if (rxState != VOFA_STATE_COMPLETE) {
        return 0;
    }
    
    // 检查帧头和帧尾
    if (rxBuffer[0] != '#' || rxBuffer[rxIndex - 1] != '!') {
        rxState = VOFA_STATE_WAIT_HEAD;
        return 0;
    }
    
    // 检查格式: #Px=yy.yy!
    // rxBuffer[1] 应该是 'P'
    // rxBuffer[2] 应该是参数编号 '1'/'2'/'3'
    // rxBuffer[3] 应该是 '='
    if (rxBuffer[1] != 'P' || rxBuffer[3] != '=') {
        rxState = VOFA_STATE_WAIT_HEAD;
        return 0;
    }
    
    // 提取参数编号
    uint8_t paramNum = rxBuffer[2] - '0';
    
    // 提取数值（从 rxBuffer[4] 开始，到帧尾前结束）
    // 将帧尾 '!' 替换为字符串结束符 '\0'
    rxBuffer[rxIndex - 1] = '\0';
    float value = atof(&rxBuffer[4]);
    
    // 根据参数编号更新 PID 参数
    switch (paramNum) {
        case 1:  // P1 = KP
            pid->Kp = value;
            break;
        case 2:  // P2 = KI
            pid->Ki = value;
            break;
        case 3:  // P3 = KD
            pid->Kd = value;
            break;
        default:
            rxState = VOFA_STATE_WAIT_HEAD;
            return 0;
    }
    
    // 参数改变时重置 PID 状态
    PID_Reset(pid);
    
    // 重置接收状态，准备接收下一包
    rxState = VOFA_STATE_WAIT_HEAD;
    rxIndex = 0;
    
    return 1;  // 成功解析
}
