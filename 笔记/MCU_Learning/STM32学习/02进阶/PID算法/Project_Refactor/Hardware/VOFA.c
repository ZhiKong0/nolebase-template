#include "stm32f10x.h"
#include "VOFA.h"
#include "SoftSerial.h"
#include <string.h>

#define VOFA_RX_BUF_SIZE    64

#define VOFA_TX_BUF_SIZE    2048

// ================================
// VOFA 串口模块（USART2）说明
// ================================
// 本文件实现了：
// 1) 通过 USART2 与上位机(例如 VOFA+/串口助手/PC脚本)进行数据收发
// 2) TX: 使用环形缓冲区(txBuffer) + TXE 中断“边发边取”方式发送，避免阻塞主循环
// 3) RX: 支持接收一类简易命令帧：以 '#' 开头，以 '!' 结尾，例如："#SPD=7!"。
//    - VOFA_FeedRxByte() 是逐字节喂入的状态机
//    - VOFA_TakeCommand() 在主循环中取出完整命令（并做基础合法性校验）
//
// 重要约束：
// - RX 缓冲区固定为 64 字节，命令过长会被丢弃并重置状态
// - 命令帧只保证“#...!”格式；具体命令解析通常在 Control.c / control_parse_cmd 等处完成
// - 本文件同时提供了 VOFA_SendJustFloat3/5，用于 VOFA+ 的 JustFloat 协议画曲线

static char rxBuffer[VOFA_RX_BUF_SIZE];
static volatile uint8_t rxIndex = 0;
static volatile VOFA_State_t rxState = VOFA_STATE_WAIT_HEAD;
static VOFA_Channel_t outputChannel = VOFA_CHANNEL_USB;
static volatile uint32_t g_vofaRxBytes = 0;

static volatile uint16_t txHead = 0;
static volatile uint16_t txTail = 0;
static uint8_t txBuffer[VOFA_TX_BUF_SIZE];
static volatile uint32_t g_vofaTxDropBytes = 0;

#if VOFA_USE_USART1
static void VOFA_SendByte_HW(uint8_t b) {
    uint32_t guard = 200000u;
    while (USART_GetFlagStatus(VOFA_USART, USART_FLAG_TXE) == RESET) {
        if (guard-- == 0u) {
            g_vofaTxDropBytes++;
            return;
        }
    }
    USART_SendData(VOFA_USART, b);
}
#endif

// TX 环形缓冲区下标推进（到末尾回卷）
static uint16_t tx_next(uint16_t v) {
    v++;
    if (v >= (uint16_t)VOFA_TX_BUF_SIZE) v = 0;
    return v;
}

// 触发发送：打开 USART2 的 TXE 中断。
// TXE 触发条件：发送数据寄存器为空。
// 打开后，USART2_IRQHandler 会不断从 txBuffer 取数据写入 DR，直到发送完再关掉 TXE 中断。
static void VOFA_TxKick(void) {
#if !VOFA_USE_USART1
    USART_ITConfig(VOFA_USART, USART_IT_TXE, ENABLE);
#endif
}

// 入队一个待发送字节到 TX 环形缓冲。
// - 使用关中断保护 head/tail 与 buffer 的一致性（IRQ 里也会动 tail）
// - 若缓冲区满(next == txTail)，丢弃该字节并增加丢弃计数
static void VOFA_TxEnqueueByte(uint8_t b) {
#if VOFA_USE_USART1
    (void)b;
    g_vofaTxDropBytes++;
#else
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    {
        uint16_t next = tx_next(txHead);
        if (next != txTail) {
            txBuffer[txHead] = b;
            txHead = next;
            VOFA_TxKick();
        } else {
            g_vofaTxDropBytes++;
        }
    }
    if (primask == 0u) {
        __enable_irq();
    }
#endif
}

// RX 命令帧状态机：逐字节解析 "#...!"。
// - WAIT_HEAD: 等待 '#'
// - RECEIVING: 收集中间内容，遇到 '!' 认为一帧结束；遇到 '#' 认为新帧开始（重置）
// - COMPLETE: 一帧已完整，等待上层通过 VOFA_TakeCommand() 取走。
// 注意：
// - 若帧超长（超过 VOFA_RX_BUF_SIZE-1），会丢弃并回到 WAIT_HEAD
static void VOFA_FeedRxByte(uint8_t d) {
    switch (rxState) {
        case VOFA_STATE_WAIT_HEAD:
            if (d == '#') {
                rxIndex = 0;
                rxBuffer[rxIndex++] = (char)d;
                rxState = VOFA_STATE_RECEIVING;
            }
            break;
        case VOFA_STATE_RECEIVING:
            if (d == '!') {
                if (rxIndex < (VOFA_RX_BUF_SIZE - 1)) {
                    rxBuffer[rxIndex++] = (char)d;
                }
                rxState = VOFA_STATE_COMPLETE;
            } else if (d == '#') {
                rxIndex = 0;
                rxBuffer[rxIndex++] = (char)d;
            } else {
                if (rxIndex < (VOFA_RX_BUF_SIZE - 1)) {
                    rxBuffer[rxIndex++] = (char)d;
                } else {
                    rxState = VOFA_STATE_WAIT_HEAD;
                    rxIndex = 0;
                }
            }
            break;
        case VOFA_STATE_COMPLETE:
            if (d == '#') {
                rxIndex = 0;
                rxBuffer[rxIndex++] = (char)d;
                rxState = VOFA_STATE_RECEIVING;
            }
            break;
    }
}

// 轮询式读取 RXNE 标志，把硬件串口收到的字节取出后喂给状态机。
// 说明：即便开启了 RXNE 中断（USART2_IRQHandler 也会喂），这里仍保留轮询，
// 用于在某些场景下确保把 FIFO/标志位里的数据尽快清空。
static void VOFA_PollRx(void) {
    while (USART_GetFlagStatus(VOFA_USART, USART_FLAG_RXNE) != RESET) {
        uint8_t d = (uint8_t)USART_ReceiveData(VOFA_USART);
        g_vofaRxBytes++;
        VOFA_FeedRxByte(d);
    }
    if (USART_GetFlagStatus(VOFA_USART, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(VOFA_USART);
    }
}

uint32_t VOFA_GetRxByteCount(void) {
    return g_vofaRxBytes;
}

uint32_t VOFA_GetTxDropByteCount(void) {
    return g_vofaTxDropBytes;
}

static void VOFA_SendByte_USB(uint8_t b) {
#if VOFA_USE_USART1
    VOFA_SendByte_HW(b);
#else
    VOFA_TxEnqueueByte(b);
#endif
}

// 初始化 USART2（PA2/PA3）用于 VOFA/上位机通信。
// - 115200, 8N1, 无流控
// - 使能 RXNE 中断（接收）
// - TX 使用 TXE 中断按需使能（有数据要发才打开）
void VOFA_Init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

#if VOFA_USART_APBx == 2
    RCC_APB2PeriphClockCmd(VOFA_USART_RCC, ENABLE);
#else
    RCC_APB1PeriphClockCmd(VOFA_USART_RCC, ENABLE);
#endif

    GPIO_InitTypeDef g;
    g.GPIO_Speed = GPIO_Speed_50MHz;

    g.GPIO_Pin = VOFA_TX_PIN;
    g.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(VOFA_GPIO_PORT, &g);

    g.GPIO_Pin = VOFA_RX_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(VOFA_GPIO_PORT, &g);

    USART_InitTypeDef u;
    u.USART_BaudRate = VOFA_BAUDRATE;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    u.USART_Parity = USART_Parity_No;
    u.USART_StopBits = USART_StopBits_1;
    u.USART_WordLength = USART_WordLength_8b;
    USART_Init(VOFA_USART, &u);

#if !VOFA_USE_USART1
    USART_ITConfig(VOFA_USART, USART_IT_RXNE, ENABLE);
    USART_ITConfig(VOFA_USART, USART_IT_TXE, DISABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitTypeDef n;
    n.NVIC_IRQChannel = VOFA_USART_IRQn;
    n.NVIC_IRQChannelCmd = ENABLE;
    n.NVIC_IRQChannelPreemptionPriority = 1;
    n.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&n);
#endif

    USART_Cmd(VOFA_USART, ENABLE);

    SoftSerial_Init();

    rxIndex = 0;
    rxState = VOFA_STATE_WAIT_HEAD;
    memset(rxBuffer, 0, sizeof(rxBuffer));

    txHead = 0;
    txTail = 0;
    g_vofaTxDropBytes = 0;
}

void VOFA_SetChannel(VOFA_Channel_t channel) {
    outputChannel = channel;
}

void VOFA_SendString(const char *s) {
    const char *p;
    if (!s) return;
    p = s;
    if (outputChannel == VOFA_CHANNEL_USB || outputChannel == VOFA_CHANNEL_BOTH) {
        while (*p) {
            VOFA_SendByte_USB((uint8_t)(*p++));
        }
    }
    if (outputChannel == VOFA_CHANNEL_BLUETOOTH || outputChannel == VOFA_CHANNEL_BOTH) {
        SoftSerial_SendString(s);
    }
}

void VOFA_SendJustFloat3(float ch0, float ch1, float ch2) {
    union {
        float f;
        uint8_t b[4];
    } u;
    uint8_t tail[4] = {0x00u, 0x00u, 0x80u, 0x7Fu};

    if (outputChannel == VOFA_CHANNEL_USB || outputChannel == VOFA_CHANNEL_BOTH) {
        u.f = ch0;
        VOFA_SendByte_USB(u.b[0]);
        VOFA_SendByte_USB(u.b[1]);
        VOFA_SendByte_USB(u.b[2]);
        VOFA_SendByte_USB(u.b[3]);

        u.f = ch1;
        VOFA_SendByte_USB(u.b[0]);
        VOFA_SendByte_USB(u.b[1]);
        VOFA_SendByte_USB(u.b[2]);
        VOFA_SendByte_USB(u.b[3]);

        u.f = ch2;
        VOFA_SendByte_USB(u.b[0]);
        VOFA_SendByte_USB(u.b[1]);
        VOFA_SendByte_USB(u.b[2]);
        VOFA_SendByte_USB(u.b[3]);

        VOFA_SendByte_USB(tail[0]);
        VOFA_SendByte_USB(tail[1]);
        VOFA_SendByte_USB(tail[2]);
        VOFA_SendByte_USB(tail[3]);
    }
    if (outputChannel == VOFA_CHANNEL_BLUETOOTH || outputChannel == VOFA_CHANNEL_BOTH) {
        SoftSerial_SendFloat3(ch0, ch1, ch2);
    }
}

void VOFA_SendJustFloat5(float ch0, float ch1, float ch2, float ch3, float ch4) {
    union {
        float f;
        uint8_t b[4];
    } u;
    uint8_t tail[4] = {0x00u, 0x00u, 0x80u, 0x7Fu};

    if (outputChannel == VOFA_CHANNEL_USB || outputChannel == VOFA_CHANNEL_BOTH) {
        u.f = ch0;
        VOFA_SendByte_USB(u.b[0]);
        VOFA_SendByte_USB(u.b[1]);
        VOFA_SendByte_USB(u.b[2]);
        VOFA_SendByte_USB(u.b[3]);

        u.f = ch1;
        VOFA_SendByte_USB(u.b[0]);
        VOFA_SendByte_USB(u.b[1]);
        VOFA_SendByte_USB(u.b[2]);
        VOFA_SendByte_USB(u.b[3]);

        u.f = ch2;
        VOFA_SendByte_USB(u.b[0]);
        VOFA_SendByte_USB(u.b[1]);
        VOFA_SendByte_USB(u.b[2]);
        VOFA_SendByte_USB(u.b[3]);

        u.f = ch3;
        VOFA_SendByte_USB(u.b[0]);
        VOFA_SendByte_USB(u.b[1]);
        VOFA_SendByte_USB(u.b[2]);
        VOFA_SendByte_USB(u.b[3]);

        u.f = ch4;
        VOFA_SendByte_USB(u.b[0]);
        VOFA_SendByte_USB(u.b[1]);
        VOFA_SendByte_USB(u.b[2]);
        VOFA_SendByte_USB(u.b[3]);

        VOFA_SendByte_USB(tail[0]);
        VOFA_SendByte_USB(tail[1]);
        VOFA_SendByte_USB(tail[2]);
        VOFA_SendByte_USB(tail[3]);
    }
    if (outputChannel == VOFA_CHANNEL_BLUETOOTH || outputChannel == VOFA_CHANNEL_BOTH) {
        SoftSerial_SendFloat3(ch0, ch1, ch2);
        SoftSerial_SendFloat3(ch3, ch4, 0.0f);
    }
}

uint8_t VOFA_TakeCommand(char *out, uint8_t outSize) {
    uint8_t i;
    uint8_t cmdLen;
    uint32_t primask;
    if (!out || outSize == 0) return 0;

    VOFA_PollRx();

    primask = __get_PRIMASK();
    __disable_irq();

    if (rxState != VOFA_STATE_COMPLETE) {
        if (primask == 0u) {
            __enable_irq();
        }
        return 0;
    }

    if (rxIndex < 2 || rxBuffer[0] != '#' || rxBuffer[rxIndex - 1] != '!') {
        rxState = VOFA_STATE_WAIT_HEAD;
        rxIndex = 0;
        out[0] = '\0';
        if (primask == 0u) {
            __enable_irq();
        }
        return 0;
    }

    cmdLen = (uint8_t)(rxIndex - 1);
    if (cmdLen >= outSize) {
        cmdLen = (uint8_t)(outSize - 1);
    }
    for (i = 0; i < cmdLen; i++) {
        out[i] = rxBuffer[i];
    }
    out[cmdLen] = '\0';

    rxState = VOFA_STATE_WAIT_HEAD;
    rxIndex = 0;

    if (primask == 0u) {
        __enable_irq();
    }
    return 1;
}

// USART2 中断：
// - RXNE: 收到一个字节，立刻取走并喂给命令状态机
// - TXE : 发送寄存器空，从 TX 环形缓冲取一个字节发送；缓冲空则关闭 TXE 中断
#if !VOFA_USE_USART1
void USART2_IRQHandler(void) {
    if (USART_GetITStatus(VOFA_USART, USART_IT_RXNE) != RESET) {
        uint8_t d = (uint8_t)USART_ReceiveData(VOFA_USART);
        g_vofaRxBytes++;
        VOFA_FeedRxByte(d);
    }

    if (USART_GetFlagStatus(VOFA_USART, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(VOFA_USART);
    }

    if (USART_GetITStatus(VOFA_USART, USART_IT_TXE) != RESET) {
        if (txTail != txHead) {
            uint8_t b = txBuffer[txTail];
            txTail = tx_next(txTail);
            USART_SendData(VOFA_USART, b);
        } else {
            USART_ITConfig(VOFA_USART, USART_IT_TXE, DISABLE);
        }
    }
}
#endif
