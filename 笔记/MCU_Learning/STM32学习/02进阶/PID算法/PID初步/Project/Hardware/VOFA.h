#ifndef __VOFA_H
#define __VOFA_H
#include "stm32f10x.h"
#include "PID.h"

// VOFA+ 配置
#define VOFA_BAUDRATE       115200
#define VOFA_TX_PIN         GPIO_Pin_2   // PA2
#define VOFA_RX_PIN         GPIO_Pin_3   // PA3
#define VOFA_GPIO_PORT      GPIOA

// 数据流使能开关
#define ENABLE_REALTIME_STREAM    0U    // 启用数据流，方便观察波形

// 输出通道选择
typedef enum {
    VOFA_CHANNEL_USB = 0,      // USB-TTL (USART2)
    VOFA_CHANNEL_BLUETOOTH,    // 蓝牙 (软件串口)
    VOFA_CHANNEL_BOTH          // 双通道同时发送
} VOFA_Channel_t;

// 数据包格式定义
// 帧头: '#'
// 帧尾: '!'
// 格式: #P1=1.50! (设置KP=1.5)
//       #P2=0.02! (设置KI=0.02)
//       #P3=0.24! (设置KD=0.24)

// 接收状态机
typedef enum {
    VOFA_STATE_WAIT_HEAD = 0,   // 等待帧头 '#'
    VOFA_STATE_RECEIVING,       // 接收数据中
    VOFA_STATE_COMPLETE         // 接收完成（收到帧尾 '!'）
} VOFA_State_t;

// 初始化 USART2
void VOFA_Init(void);

// 设置输出通道
void VOFA_SetChannel(VOFA_Channel_t channel);

// 发送 JustFloat 协议数据（3通道）
// 格式: ch0(ch1)(ch2) + 尾部 0x00 0x00 0x80 0x7f
void VOFA_SendFloat3(float ch0, float ch1, float ch2);

// 解析参数命令（在主循环中调用）
// 返回: 1=成功解析并更新参数，0=无新命令
uint8_t VOFA_ParseCommand(PID_t *pid);

// 获取当前接收状态
VOFA_State_t VOFA_GetState(void);

// 获取接收到的命令字符串
const char* VOFA_GetCommand(void);

// 发送文本（USART2）
void VOFA_SendString(const char *s);

// 取走一条命令（'#'开头，'!'结尾），返回的字符串不包含末尾'!'
// 返回: 1=成功取走并清空接收状态，0=无新命令
uint8_t VOFA_TakeCommand(char *out, uint8_t outSize);

#endif
