#ifndef __CONFIG_H
#define __CONFIG_H

#include "stm32f10x.h"

/* ========== 控制模式 ========== */
typedef enum
{
    MODE_STRAIGHT = 0, /* 直线模式 */
    MODE_TRACK = 1     /* 循迹模式 */
} ControlMode_t;

typedef enum
{
    SYS_STOP = 0,     /* 停止 */
    SYS_STRAIGHT = 1, /* 直线运行中 */
    SYS_TRACKING = 2  /* 循迹运行中 */
} SystemState_t;

typedef enum
{
    KEY_EVENT_NONE = 0,        /* 无事件 */
    KEY_EVENT_SHORT_PRESS = 1, /* 短按 */
    KEY_EVENT_LONG_PRESS = 2   /* 长按 */
} KeyEvent_t;

/* ========== 电机引脚 (TB6612 驱动 + TIM1 PWM) ========== */
#define MOTOR_STBY_PORT GPIOB
#define MOTOR_STBY_PIN GPIO_Pin_0

#define MOTOR_AIN1_PORT GPIOA
#define MOTOR_AIN1_PIN GPIO_Pin_4
#define MOTOR_AIN2_PORT GPIOA
#define MOTOR_AIN2_PIN GPIO_Pin_5

#define MOTOR_BIN1_PORT GPIOB
#define MOTOR_BIN1_PIN GPIO_Pin_1
#define MOTOR_BIN2_PORT GPIOB
#define MOTOR_BIN2_PIN GPIO_Pin_10

#define MOTOR_PWM_TIM TIM1
#define MOTOR_PWM_PERIOD 1000  /* PWM 周期 */
#define MOTOR_PWM_PRESCALER 72 /* 预分频 */
#define MOTOR_PWM_MAX 600      /* 单轮 PWM 上限 */
#define MOTOR_DIFF_MAX 200     /* 航向差速上限 */
#define MOTOR_DEADZONE 80      /* 死区补偿：低于此值电机不动，自动抬升到此值 */

/* ========== 编码器 (TIM2 左轮, TIM3 右轮) ========== */
#define ENC_LEFT_TIM TIM2
#define ENC_RIGHT_TIM TIM3
#define ENC_IC_FILTER 12 /* 输入捕获滤波 */

#define ENC_PPR 11               /* 每转脉冲数 */
#define ENC_RATIO 30             /* 减速比 */
#define ENC_QUAD_MULT 4          /* 正交4倍频 */
#define ENC_LEFT_SIGN (-1)       /* 左轮计数方向修正 */
#define ENC_RIGHT_SIGN 1         /* 右轮计数方向修正 */
#define ENC_MAX_DELTA 2600       /* 单周期最大合法增量，超过视为毛刺丢弃 */
#define ENC_SPEED_LPF_ALPHA 0.3f /* 速度低通滤波系数 (0~1, 越大越灵敏) */

/* ========== BNO085 九轴IMU (软件I2C) ========== */
#define BNO_SCL_PORT GPIOB
#define BNO_SCL_PIN GPIO_Pin_12
#define BNO_SDA_PORT GPIOB
#define BNO_SDA_PIN GPIO_Pin_13
#define BNO_RST_PORT GPIOB
#define BNO_RST_PIN GPIO_Pin_14
#define BNO_INT_PORT GPIOA
#define BNO_INT_PIN GPIO_Pin_15

#define BNO_ADDR_DEFAULT 0x4B
#define BNO_ADDR_ALT 0x4A
#define BNO_ADDR_DOC_DEF 0x28
#define BNO_ADDR_DOC_ALT 0x29

#define BNO_REPORT_INTERVAL_US 20000UL /* 报告间隔 20ms */
#define BNO_BOOT_DELAY_MS 80u          /* 上电等待 */
#define BNO_PRESENT_TIMEOUT_MS 1000u   /* 检测超时 */
#define BNO_PACKET_MAX 384u            /* 最大包长 */

#define BNO_YAW_JUMP_REJECT_DEG 45.0f /* 航向跳变抑制阈值(度) */
#define BNO_YAW_RATE_LIMIT_DPS 180.0f /* 角速度限幅(度/秒) */
#define BNO_YAW_RATE_LPF_ALPHA 0.14f  /* 角速度低通滤波系数 */

/* ========== 8路循迹传感器 (低电平有效) ========== */
#define LINE_SENSOR_COUNT 8 /* 传感器数量 */
#define LINE_ACTIVE_LOW 1   /* 1=检测到黑线时输出低电平 */

#define LINE_S1_PORT GPIOA
#define LINE_S1_PIN GPIO_Pin_10
#define LINE_S2_PORT GPIOA
#define LINE_S2_PIN GPIO_Pin_11
#define LINE_S3_PORT GPIOA
#define LINE_S3_PIN GPIO_Pin_12
#define LINE_S4_PORT GPIOB
#define LINE_S4_PIN GPIO_Pin_3
#define LINE_S5_PORT GPIOB
#define LINE_S5_PIN GPIO_Pin_4
#define LINE_S6_PORT GPIOB
#define LINE_S6_PIN GPIO_Pin_9
#define LINE_S7_PORT GPIOB
#define LINE_S7_PIN GPIO_Pin_11
#define LINE_S8_PORT GPIOC
#define LINE_S8_PIN GPIO_Pin_13

/* ========== 串口通信 (USART2) ========== */
#define COMM_USART USART2
#define COMM_USART_IRQn USART2_IRQn
#define COMM_USART_RCC RCC_APB1Periph_USART2
#define COMM_TX_PORT GPIOA
#define COMM_TX_PIN GPIO_Pin_2
#define COMM_RX_PORT GPIOA
#define COMM_RX_PIN GPIO_Pin_3
#define COMM_BAUDRATE 115200  /* 波特率 */
#define COMM_TX_BUF_SIZE 2048 /* 发送缓冲区 */
#define COMM_RX_BUF_SIZE 64   /* 接收缓冲区 */

/* ========== 系统定时器 (TIM4, 1ms 节拍) ========== */
#define SYS_TIM TIM4
#define SYS_TIM_IRQn TIM4_IRQn
#define SYS_TIM_RCC RCC_APB1Periph_TIM4
#define SYS_TIM_PRESCALER 72
#define SYS_TIM_PERIOD 1000

/* ========== OLED 显示屏 (软件I2C) ========== */
#define OLED_SCL_PORT GPIOB
#define OLED_SCL_PIN GPIO_Pin_7
#define OLED_SDA_PORT GPIOB
#define OLED_SDA_PIN GPIO_Pin_8

/* ========== 按键 (PB5, 低电平有效, 内部上拉) ========== */
#define KEY1_PORT GPIOB
#define KEY1_PIN GPIO_Pin_5
#define KEY_LONG_PRESS_MS 1000         /* 长按判定时间(ms) */
#define KEY_DEBOUNCE_MS 20             /* 消抖时间(ms) */
#define KEY_DOUBLE_CLICK_WINDOW_MS 250 /* 循迹待机态下双击窗口: 单击=屏蔽版, 双击=无屏蔽版 */

/* ========== 控制周期 ========== */
#define CONTROL_PERIOD_MS 10          /* 主控制循环周期(ms), 速度环+巡线环都在此周期运行 */
#define HEADING_LOOP_MS 30            /* 航向环执行间隔(ms) */
#define TELEMETRY_PERIOD_MS 20        /* 串口遥测上报间隔(ms) */
#define IMU_READ_PERIOD_MS 20         /* IMU 读取间隔(ms) */
#define EXP_HOST_SYNC_TIMEOUT_MS 4000 /* py 端实验编号接管的租期(ms), 超时后板子不再自动递增编号 */

/*
 * ============================================================================
 *   PID 调参指南
 * ============================================================================
 *
 * 本车有两种运行模式，每种模式包含两个 PID 环：
 *
 * 【直线模式】 (MODE_STRAIGHT)
 *   速度环: 编码器 → PID → pwmCore (基础油门)
 *     - KP: 速度偏差的比例响应。偏大→电机抖动；偏小→加速慢
 *     - KI: 消除稳态误差。偏大→积分饱和震荡；偏小→长期有速度偏差
 *     - KD: 一般设0，编码器噪声大时D项会引入抖动
 *   航向环: IMU偏航角 → PID → 差速 (headingDiffPWM)
 *     - KP: 偏航角偏差的比例修正。偏大→蛇形走位；偏小→跑偏修正慢
 *     - KI: 消除长期偏航。偏大→过冲；偏小→持续微偏
 *     - KD: 使用陀螺仪角速度作为阻尼。偏大→响应迟钝；偏小→过冲
 *
 * 【循迹模式】 (MODE_TRACK)
 *   速度环: 编码器 → PID → pwmCore (基础油门) [与直线模式共用速度PID结构]
 *     - 参数含义同上，但循迹时可独立设置不同的增益
 *   巡线环: 8路传感器加权位置 → PD → 差速 (devSpeed)
 *     - KP: 偏离中线时的比例纠偏。偏大→过冲摆头；偏小→弯道跟不上
 *     - KD: 偏离速度的阻尼(带低通滤波)。偏大→响应迟钝；偏小→摆头震荡
 *     - 注意: D项有低通滤波(TRACK_DERIV_LPF)，因为8路传感器是离散的，
 *       传感器跳变时原始dPos可达±200，不滤波会产生巨大PWM尖峰
 *     - 无I项：循迹环不需要积分，线在哪就往哪修正
 *
 * 调参步骤建议：
 *   1. 先调速度环(SKP/SKI)，让小车能平稳地以目标速度直行
 *   2. 直线模式调航向环(AKP/AKD)，让小车走直线不跑偏
 *   3. 循迹模式先把 LINE_KP 从小往大调，直到弯道能跟住
 *   4. 如果出现摆头震荡，加大 LINE_KD 来抑制
 *   5. 如果直道上一直微微摆头，适当减小 LINE_KP
 *
 * 串口实时调参命令 (运行中可改)：
 *   #SKP=1.5   速度环 KP       #AKP=6.0   航向环 KP
 *   #SKI=0.1   速度环 KI       #AKI=0.06  航向环 KI
 *   #SKD=0.0   速度环 KD       #AKD=3.2   航向环 KD
 *   #LKP=0.5   巡线环 KP       #LKD=4.0   巡线环 KD
 *   #SPD=10    当前模式目标速度   #HTR=0.15  航向偏置(度)
 *   #STAT!     查询当前所有PID参数
 * ============================================================================
 */

/* ========== 直线模式 PID 默认参数 ========== */
/* 速度环: 编码器反馈 → pwmCore */
#define PID_STRAIGHT_SPEED_TARGET 10.0f /* 直线模式目标速度 (串口 #SPD= 可运行时修改) */
#define PID_STRAIGHT_SPEED_KP 1.50f     /* 比例: 速度误差→油门补偿 */
#define PID_STRAIGHT_SPEED_KI 0.10f     /* 积分: 消除稳态速度偏差 */
#define PID_STRAIGHT_SPEED_KD 0.0f      /* 微分: 通常设0(编码器噪声) */
/* 航向环: IMU偏航角 → 差速 */
#define PID_STRAIGHT_HEADING_KP 6.00f  /* 比例: 偏航角→差速修正 */
#define PID_STRAIGHT_HEADING_KI 0.060f /* 积分: 消除长期偏航 */
#define PID_STRAIGHT_HEADING_KD 3.20f  /* 微分: 陀螺仪角速度阻尼 */
#define HEADING_TRIM 0.15f             /* 航向偏置(度): 正值=向右补偿左偏 */
#define HEADING_INTEGRAL_ZONE 1.5f     /* 积分区(度): 误差在此范围内用全KI, 超出衰减 */

/* ========== 循迹模式 PID 默认参数 ========== */
/* 速度环: 编码器反馈 → pwmCore (结构同直线, 参数可独立调) */
#define PID_TRACK_SPEED_TARGET 25.0f /* 回退到 25 速稳定版 */
#define PID_TRACK_SPEED_KP 1.50f     /* 25 速基线速度环比例 */
#define PID_TRACK_SPEED_KI 0.10f     /* 25 速基线速度环积分 */
#define PID_TRACK_SPEED_KD 0.0f      /* 微分 */
/* 巡线环: 8路传感器加权位置 → 差速 (仅PD, 无积分)
 *   P输出范围: KP * [-350..+350]
 *   D输出范围: KD * 滤波后dPos (双重LPF后 ±5~15)
 *   两项加和应在 [-100..+100] 左右为宜 (低速循迹) */
#define PID_TRACK_LINE_KP 0.28f /* 25 速稳定版巡线P */
#define PID_TRACK_LINE_KD 2.0f  /* 25 速稳定版巡线D */
#define TRACK_POS_LPF 0.6f      /* 25 速稳定版位置低通 */
#define TRACK_DERIV_LPF 0.4f    /* 25 速稳定版D项低通 */
#define TRACK_CENTER_RIGHT_POS_OFFSET 50 /* 当前车上直线常落在S5单灯(sb=16, lp≈+50), 先对右侧中心零点做校正 */
#define TRACK_PID_POS_DEADBAND 12            /* 25 速稳定版位置死区 */
#define TRACK_PID_CENTER_POS_MAX 60          /* 25 速稳定版中心区 */
#define TRACK_PID_CENTER_KP_SCALE 0.75f      /* 25 速稳定版中心区P缩放 */
#define TRACK_PID_CENTER_KD_SCALE 0.60f      /* 25 速稳定版中心区D缩放 */
#define TRACK_PID_EDGE_POS_MIN 90            /* 明显偏离中线后，进入“弯中约束增强区” */
#define TRACK_PID_EDGE_KP_SCALE 1.38f        /* 25 速稳定版边缘P增强 */
#define TRACK_PID_EDGE_KD_SCALE 0.85f        /* 25 速稳定版边缘D缩放 */
#define TRACK_PID_D_DEADBAND 6.0f            /* D项死区: 很小的位置变化不参与纠偏 */
#define TRACK_PID_D_MAX 18.0f                /* 25 速稳定版D限幅 */

/* ========== 循迹行为参数 ========== */
#define TRACK_DEV_MAX_RATIO 0.78f                /* 25 速稳定版最大差速比例 */
#define TRACK_PWM_MAX 400                        /* 25 速稳定版单轮PWM上限 */
#define TRACK_PWM_MIN 0                          /* 单轮PWM下限 (0=滑行) */
#define TRACK_OUTPUT_SLEW_STEP 20                /* 25 速稳定版TRK输出斜率 */
#define TRACK_OUTPUT_RISK_DROP_STEP 20           /* 回退到与常规斜率一致, 不再保留50速风险快降 */
#define TRACK_OUTPUT_SLEW_BYPASS_POS_MAX 90      /* 已回到中心区时不再做TRK输出斜率限制, 避免 exp106 这类顿一下 */
#define TRACK_EDGE_SPEED_CAP_POS 110             /* 25 速版边缘限速触发 */
#define TRACK_EDGE_BASE_PWM_MAX 140              /* 25 速版边缘基础PWM上限 */
#define TRACK_ZERO_LOSS_BASE_PWM_MAX 140         /* 纯全灭期间不再额外高配, 与25速边缘上限保持一致 */
#define TRACK_CURVE_YAWRATE_CAP_DPS 999.0f       /* 基本关闭50速试验用弯中角速度限速 */
#define TRACK_CURVE_BASE_PWM_MAX 140             /* 即使命中也保持25速边缘口径 */
#define TRACK_TURN_PWM_FAST 130                  /* 25 速版CSR外轮PWM */
#define TRACK_TURN_PWM_SLOW 95                   /* 25 速版CSR内轮PWM */
#define TRACK_RIGHT_ANGLE_TURN_PWM_FAST 130      /* 25 速版R90外轮PWM */
#define TRACK_RIGHT_ANGLE_TURN_PWM_SLOW 80       /* 25 速版R90内轮PWM */
#define TRACK_RIGHT_ANGLE_MIN_YAW 18.0f          /* 15° 版本稳定性下降, 回到更稳的 18° */
#define TRACK_RIGHT_ANGLE_ACCEPT_LATCH_YAW 10.0f /* R90 超过该角度后若命中过目标线则锁存, 防止瞬时丢线造成过转 */
#define TRACK_RIGHT_ANGLE_TIMEOUT_MS 900         /* 直角专用找线超时 */
#define TRACK_RIGHT_ANGLE_REARM_MS 200           /* 直角专用触发的重入锁定 */
#define TRACK_LEFT_RIGHT_ANGLE_CONFIRM_COUNT 3   /* 左侧R90需连续3帧确认, 避免左转回线早期被单帧边缘图样抢占 */
#define TRACK_TURN_INTENT_HOLD_MS 120            /* 首次看到某侧转向意图后短暂锁定该侧, 防止另一侧信号抢占 */
#define TRACK_STARTUP_SKIP_SHORT_BLOCK_MS 700    /* 启动期 one-shot 短屏蔽, 只挡当前这一段扰动, 不再拉几秒长窗 */
#define TRACK_STARTUP_SKIP_REARM_STABLE_COUNT 3  /* 短屏蔽后需连续若干帧稳定循迹, 才再 arm 一次额外 one-shot 屏蔽 */
#define TRACK_STARTUP_SKIP_REARM_POS_MAX 90      /* 重新 arm 额外屏蔽时, 认为已回到稳定循迹的最大位置偏差 */
#define TRACK_STARTUP_SKIP_REARM_EDGE_POS_MAX 160 /* 中心区未完全扫回时, 允许边缘回线也可 re-arm 额外短屏蔽 */
#define TRACK_STARTUP_SKIP_WINDOW_OPPOSITE_DEV_MAX 25 /* 短屏蔽窗口内若被交叉线从对侧拉扯, 限制TRK对该侧的最大纠偏, 避免明显抖一下 */
#define TRACK_SHORT_LOSS_ARM_POS_MAX 90          /* 直线短时全灭前若|lp|仍较小, 认为是短丢线而非真实转角 */
#define TRACK_SHORT_LOSS_DECAY_STEP 35           /* 短时全灭期间每拍把冻结位置向0拉回一点, 避免恢复时瞬间抽动 */
#define TRACK_SHORT_LOSS_RECOVER_MS 180          /* 重新见线后的短恢复窗口, 只压住直线短丢线后的第一段抽动 */
#define TRACK_SHORT_LOSS_RECOVER_DEV_MAX 45      /* 25 速稳定版短恢复差速上限 */
#define TRACK_SHORT_LOSS_RECOVER_BASE_PWM_MAX 120 /* 25 速稳定版短恢复基础PWM上限 */
#define TRACK_OVERRUN_LIMIT 150                  /* 连续丢线次数上限(1.5s), 超过则自动停车 */
#define TRACK_CORNER_CONFIRM 20                  /* 连续全灭确认(200ms), 更快进入旋转减少过弯漂移 */
#define TRACK_CORNER_FAST_CONFIRM 3              /* 强边缘丢线快速接管: 连续3帧全灭即可提前进CSR */
#define TRACK_CORNER_FAST_EDGE_POS_MIN 120       /* 仅当丢线前位置已明显贴边时才启用快速接管 */
#define TRACK_CORNER_FAST_YAWRATE_CONFIRM 3      /* 25 速口径下基本关闭“超快角速度抢接管” */
#define TRACK_CORNER_FAST_YAWRATE_DPS 999.0f     /* 关闭50速试验用角速度快接管 */
#define TRACK_CORNER_FALLBACK_YAWRATE_DPS 999.0f /* 关闭50速试验用角速度方向补历史 */
#define TRACK_SHORT_LOSS_YAWRATE_MAX 16.0f       /* 保留短丢线角速度上限 */
#define TRACK_DEFAULT_CROSSINGS 4                /* 默认交叉口计数, 到达后自动停车 */
#define TRACK_CROSS_FILTER 50                    /* 两次交叉口之间最小间隔(节拍) */
#define TRACK_CROSS_MIN_COUNT 5                  /* 判定交叉口的最少亮灯数 */
#define TRACK_CORNER_MIN_YAW 24.0f               /* 20° 版本未改善第6个弯道, 回到更稳的 24° */
#define TRACK_CORNER_ACCEPT_LATCH_YAW 12.0f      /* 第3个弯道目标线出现较早, 提前锁存以减少看到线后继续过转 */
#define TRACK_CORNER_EXIT_POS_MAX 110            /* CSR 找线退出时, 若还挂在单侧边缘超过该偏差则继续找, 不急着回TRK */

/* ========== 锐角早期检测 (S1/S8 + 中间同时亮 → 提前识别锐角) ========== */
#define TRACK_ACUTE_WINDOW_MS 80                  /* 25 速版锐角窗口 */
#define TRACK_ACUTE_INNER_PWM (-20)               /* 25 速版内轮轻微反转 */
#define TRACK_ACUTE_OUTER_PWM 200                 /* 25 速版外轮PWM */
#define TRACK_ACUTE_LEFT_INNER_PWM 0              /* 左锐角过转更明显, 左侧专用ATN改为低速前进而非轻微倒转 */
#define TRACK_ACUTE_LEFT_OUTER_PWM 185            /* 25 速版左锐角外轮PWM */
#define TRACK_ACUTE_MIN_YAW 7.0f                  /* 实测首次重新扫到S4/S5约在7.2°, 提前允许退出避免转过头 */
#define TRACK_ACUTE_TIMEOUT_MS 2000               /* 锐角转弯超时(ms) */
#define TRACK_ACUTE_REARM_MS 300                  /* 锐角退出后短暂锁定, 防止同一锐角被立刻重复触发 */
#define TRACK_ACUTE_RECOVER_HOLD_MS 100           /* 退出后直行缓冲时间, 避免PD立刻反向接管 */
#define TRACK_ACUTE_RECOVER_TIMEOUT_MS 250        /* 缓冲最长时长, 防止恢复态卡住 */
#define TRACK_ACUTE_RECOVER_STABLE_COUNT 2        /* 恢复为正常PD前至少连续两帧见到S3~S6 */
#define TRACK_ACUTE_RECOVER_BASE_PWM 110          /* 25 速版恢复态最低基速 */
#define TRACK_ACUTE_RECOVER_DIFF_PWM 25           /* 25 速版恢复态差速 */
#define TRACK_ACUTE_RECOVER_LEFT_DIFF_PWM 12      /* 25 速版左锐角恢复差速 */
#define TRACK_ACUTE_RECOVER_EXIT_POS_MAX 90       /* 恢复态允许放回PD的最大|lp|, 超过说明仍过偏 */
#define TRACK_ACUTE_RECOVER_LEFT_EDGE_POS_MAX 180 /* 左锐角恢复时若只先扫到S3, 允许更宽的边缘回线位置再放回PD */
#define TRACK_CORNER_MAX_YAW 180.0f               /* 最大转角(度): 超过则停止 */
#define TRACK_CORNER_TIMEOUT_MS 2000              /* 弯道旋转超时(ms), 超时则放弃 */
#define TRACK_CORNER_RESUME_SPEED_MAX 16.0f       /* 25 速版出弯恢复上限 */

/* ========== 速度斜坡 ========== */
#define SPEED_TARGET_DEFAULT 10.0f  /* 初始目标速度 (仅用于上电初始化, 切换模式时会覆盖) */
#define SPEED_ENTRY 9.2f            /* 起步速度 = 死区/前馈增益, 确保不卡死区 */
#define SPEED_RAMP_RATE 20.0f       /* 25 速稳定版加速斜率 */
#define SPEED_CORE_SLEW_STEP 18     /* 25 速稳定版速度环输出斜率 */
#define SPEED_OUTPUT_LIMIT 600.0f   /* 速度环PWM输出上限 */
#define SPEED_FEEDFORWARD_GAIN 8.7f /* 前馈增益: target*ff≈87, 刚好超过死区 */
#define PID_DERIV_LPF_ALPHA 0.15f   /* 微分项低通滤波系数 */
#define HEADING_INTEGRAL_ATTEN 0.3f /* 积分衰减: 误差超过INTEGRAL_ZONE时KI乘以此系数 */

#endif
