/*
 * 工程配置总表:
 * 1. 这里集中保存“引脚映射 + 默认参数 + 运行阈值”。
 * 2. 原则上只有不会引入状态副作用的常量才放在这里。
 * 3. 调参时优先改这里，不要把魔法数字散落到业务代码里。
 */
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
#define GYRO_FAST_LPF_ALPHA 0.50f     /* 主循环里用于航向环阻尼的快速角速度低通 */

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
#define KEY_LONG_PRESS_MS 1000 /* 长按判定时间(ms) */
#define KEY_DEBOUNCE_MS 20     /* 消抖时间(ms) */

/* ========== 控制周期 ========== */
#define CONTROL_PERIOD_MS 10          /* 主控制循环周期(ms) */
#define TELEMETRY_PERIOD_MS 20        /* 串口遥测上报间隔(ms) */
#define IMU_READ_PERIOD_MS 20         /* IMU 读取间隔(ms) */
#define EXP_HOST_SYNC_TIMEOUT_MS 4000 /* py 端实验编号接管的租期(ms), 超时后板子不再自动递增编号 */

/*
 * ============================================================================
 *   PID 调参指南
 * ============================================================================
 *
 * 本车有两种运行模式，但主链不再完全相同：
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
 *   速度环: 编码器 → PID → pwmCore (基础油门)
 *   循迹环: 8路传感器位置 → 前端转向命令 → 直接差速 (headingDiffPWM)
 *     - 这里只保留基础骨架，不含锐角、直角、交叉口、原地找线等特殊逻辑
 *
 * 调参步骤建议：
 *   1. 先调速度环(SKP/SKI)，让小车能平稳地以目标速度直行
 *   2. 直线模式调航向环(AKP/AKD)，让小车走直线不跑偏
 *   3. 循迹模式主要调 LINE_KP/LINE_KD 与 track.capture / track.loss / track.scurve
 *   4. 再根据速度变化补速度环和差速权限，不再先调循迹航向环
 *
 * 串口实时调参命令:
 *   通用接口:
 *     #PING!
 *     #PLIST!                    枚举所有可调参数及范围
 *     #PGET=track.line.kp!       读取单个参数
 *     #PGETG=track_line!         读取一组参数
 *     #PSET=track.line.kp,4.50!  写入单个参数
 *     #PDEF! / #PDEF=track_line! 恢复全部或单组默认值
 *     #MARK=s_curve_entry!       给当前实验日志打标记
 *   兼容旧接口:
 *     #SKP=1.5   #AKP=6.0   #LKP=4.0   #SPD=10   #HTR=0.15
 *   说明:
 *     旧命令现在只是统一参数表的别名，真正的拥有者已经迁到运行时参数层。
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
#define PID_STRAIGHT_HEADING_DIFF_RATIO 0.40f /* 直线模式差速权限比例 */
#define PID_STRAIGHT_HEADING_DIFF_MIN 5.0f    /* 直线模式差速最小下限 */
#define HEADING_TRIM 0.15f             /* 航向偏置(度): 正值=向右补偿左偏 */
#define HEADING_INTEGRAL_ZONE 1.5f     /* 积分区(度): 误差在此范围内用全KI, 超出衰减 */

/* ========== 循迹模式 PID 默认参数 ========== */
/* 速度环: 编码器反馈 → pwmCore */
/* exp0409 的真实目标速度是 50，不是后面混进来的 60。 */
#define PID_TRACK_SPEED_TARGET 50.0f
#define PID_TRACK_SPEED_KP 1.50f
#define PID_TRACK_SPEED_KI 0.10f
#define PID_TRACK_SPEED_KD 0.0f

/* 读线骨架: 位置 → 前端转向命令 */
#define PID_TRACK_LINE_KP 4.00f
#define PID_TRACK_LINE_KD 1.20f
#define TRACK_POS_LPF 0.45f
#define TRACK_CAPTURE_POS_ALPHA 1.00f
#define TRACK_DERIV_LPF 0.35f
#define TRACK_LINE_CENTER_BIAS 0.50f
#define TRACK_CAPTURE_ERROR_START 0.85f
#define TRACK_CAPTURE_ERROR_GAIN 2.10f
/* 当边缘只剩 1~2 个灯时，普通权重平均容易把位置看得偏里。
 * 这里给一个额外的“向外推”倍率，让稀疏边缘位型更像真实的贴边状态。 */
#define TRACK_CAPTURE_SPARSE_POS_GAIN 1.18f
/* exp0547 证明第一次边缘捕获过硬:
 * - 一旦 S1/S2 或 S7/S8 刚出现，capture 会立刻给较大的最小拉回角；
 * - 在当前 50 速档下，这个角度叠加较大的 diff_min，很容易第一下就把车打进全灭盲区。
 * 这里先把 capture 的最低拉回角收一档，让“第一次摸到边缘”先把车往回带，
 * 但不要立刻变成近似满舵。 */
#define TRACK_CAPTURE_YAW_FLOOR 20.0f
#define TRACK_CAPTURE_EXIT_ERROR 0.85f
#define TRACK_TARGET_YAW_LIMIT 30.0f
#define TRACK_HEADING_DIFF_RATIO 0.55f
#define TRACK_HEADING_DIFF_MIN 12.0f
#define TRACK_CAPTURE_HEADING_DIFF_RATIO 0.86f
/* capture 的最小差速权限和 yaw_floor 是耦合的:
 * yaw_floor 决定“至少要给多大的转向意图”，
 * diff_min 决定“这个转向意图至少能落成多大的左右轮差速”。
 * exp0547 的问题不是看不见线，而是第一次捕获时落地差速太狠，
 * 所以这里同步收一档，避免一抓到边缘就把车继续甩进盲区。 */
#define TRACK_CAPTURE_HEADING_DIFF_MIN 96.0f
/* capture 渐入权限:
 * 1. 第一拍只给 45% 的 capture 权限，避免 `ca=1` 当拍直接打满；
 * 2. 连续 3 拍仍然是边缘捕获，再逐步放到满额；
 * 3. 脱离边缘位型后按 0.82 衰减，避免“已经回到线附近了还死命拽”。 */
#define TRACK_CAPTURE_ENTRY_SCALE 0.45f
#define TRACK_CAPTURE_FULL_CONFIRM_COUNT 3u
#define TRACK_CAPTURE_RELEASE_DECAY 0.82f
/* 反向换边抑制:
 * 用户现在看到的问题不是“完全抓不住”，而是“刚回中又在另一侧重新见边时，
 * capture 很快重新拉满，把车头再次甩出去”。
 * 所以这里单独给换边第一段一套更软的接管参数:
 * 1. 起始权限更低;
 * 2. 连续确认拍数更长;
 * 3. 只有上一拍误差已经回到中心附近，才认定这是“换边第一段”。 */
#define TRACK_CAPTURE_SWITCH_ENTRY_SCALE 0.22f
#define TRACK_CAPTURE_SWITCH_CONFIRM_COUNT 6u
#define TRACK_CAPTURE_SWITCH_ERROR_LIMIT 1.30f
/* 顺向角速度卸力:
 * 当车头已经顺着当前边缘方向明显在转时，继续抬高 capture 权限只会越拉越深。
 * 这里把“已经在顺着弯转”的场景单独抽出来:
 * 1. 角速度超过 start 后，capture 开始主动卸力;
 * 2. 超过 full 后，capture 只保留一小部分最小钳位;
 * 3. 这样既保留沿边方向，又不会在中后段继续把车甩进盲区。 */
#define TRACK_CAPTURE_RATE_RELIEF_START 55.0f
#define TRACK_CAPTURE_RATE_RELIEF_FULL 130.0f
#define TRACK_CAPTURE_RATE_RELIEF_MIN_SCALE 0.35f
/* 回中重锁:
 * 1. 只有中间灯重新出现且原始位置已回到中心附近，才触发 recenter;
 * 2. 触发后把 scurve 的边缘观测适度拉回原始中心位置;
 * 3. 同时更快更新 filteredPosition，并主动收一档边缘权限和误差力度，
 *    避免“已经重新压到线附近，但前端还沿着上一次边缘方向继续甩头”。 */
#define TRACK_RECENTER_ERROR_LIMIT 1.10f
#define TRACK_RECENTER_BLEND 0.72f
#define TRACK_RECENTER_POS_ALPHA 0.88f
#define TRACK_RECENTER_ERROR_SCALE 0.62f
#define TRACK_RECENTER_AUTHORITY_SCALE 0.58f
/* 回中灯位往往只亮 1 拍左右。
 * 如果 rc 当拍亮、下一拍立刻掉成 0，就会在换边第一拍重新吃满 capture。
 * 这里给 recenter 一点短衰减，让“回中刚完成”的余力再续 1~2 拍。 */
#define TRACK_RECENTER_RELEASE_DECAY 0.72f
#define TRACK_CAPTURE_SPEED_SCALE_MIN 0.76f
#define TRACK_LINE_LOSS_HOLD_MS 40u
#define TRACK_LINE_LOSS_TIMEOUT_MS 5000u /* 离散循迹传感器会出现短暂全灭，适当放宽丢线停车阈值 */
#define TRACK_LINE_LOSS_YAW_DECAY 0.985f
#define TRACK_LINE_LOSS_SPEED_SCALE 0.36f
/* 关键改动:
 * capture 的“狠狠干拽”只应该在还能看见线时生效。
 * 一旦整排全灭，最多只保留一小段盲区钳位，然后就撤掉 capture 强约束，
 * 否则车会沿着上一次边缘方向越转越深。 */
#define TRACK_LINE_LOSS_CAPTURE_HOLD_MS 40u
#define TRACK_LINE_LOSS_CAPTURE_YAW_FLOOR_SCALE 0.45f
#define TRACK_CURVE_SPEED_POS_START 0.35f
#define TRACK_CURVE_SPEED_POS_FULL 1.30f
#define TRACK_CURVE_SPEED_SCALE_MIN 0.30f
/* S 弯态和普通居中稳态分开调。
 * 直线段优先追求“压中线的稳定性”；
 * S 弯态优先追求“边缘一出现就能把车强拉回来，同时不要掉太多速度”。 */
#define TRACK_SCURVE_ENTER_YAW_RATE 32.0f
#define TRACK_SCURVE_ENTER_ERROR 0.95f
#define TRACK_SCURVE_ENTER_DELTA 0.42f
#define TRACK_SCURVE_ENTER_YAW_CMD 10.0f
#define TRACK_SCURVE_EXIT_ERROR 0.40f
/* exp0409 的 `yl=40`、`dr=0.94` 对应的就是这组 S 弯态权限。 */
#define TRACK_SCURVE_YAW_LIMIT 40.0f
/* S 弯态仍要比直线态更有拉回力，但不能继续沿用过硬的 0.94。
 * exp0547 里 `sc=1` 期间长期维持高权限，导致重新见线后仍容易大幅摆动。
 * 这里把 S 弯态整体差速比例降一档，保留“能抓住边缘”的能力，
 * 同时减小“刚回线又被再次打出去”的概率。 */
#define TRACK_SCURVE_DIFF_RATIO 0.88f
#define TRACK_SCURVE_DIFF_MIN 94.0f
/* S 弯态误差成形:
 * - center_zone 以内弱化中线约束，减少“刚回中线就被拉回去”的左右摆；
 * - 超过 center_zone 后，剩余误差按 edge_gain 放大，保证两侧拉回力更强。 */
#define TRACK_SCURVE_CENTER_ZONE 0.80f
#define TRACK_SCURVE_CENTER_GAIN 0.44f
#define TRACK_SCURVE_EDGE_GAIN 1.35f
/* S 弯态专用传感器参与系数:
 * - center_sensor_gain 对应 S4/S5，设到接近 0 表示中心双灯基本不参与纠偏；
 * - inner_sensor_gain 对应 S3/S6；
 * - outer_sensor_gain 对应 S2/S7；
 * - edge_sensor_gain 对应最外侧 S1/S8，用更高权重尽快把车从边缘拉回来。 */
#define TRACK_SCURVE_CENTER_SENSOR_GAIN 0.00f
#define TRACK_SCURVE_INNER_SENSOR_GAIN 1.00f
#define TRACK_SCURVE_OUTER_SENSOR_GAIN 1.15f
#define TRACK_SCURVE_EDGE_SENSOR_GAIN 1.25f
/* exp0409 日志里的 `ks=1.20` 来自这里。 */
/* `line_kp_scale` 负责把前端位置误差放大后送进 yawCommand。
 * exp0547 的症状是:
 * 1. 第一次见边后前端给出的 `yc/ty` 很快顶大；
 * 2. 重新见线时又会以同样偏硬的观测面继续放大误差，造成来回甩。
 * 这里把 S 弯态前端误差放大倍率从 1.20 收到 1.10，
 * 让 S 弯仍然沿边抓线，但不要一进边缘就把前端当成“必须狠狠干拽”的状态。 */
#define TRACK_SCURVE_LINE_KP_SCALE 1.10f
/* exp0409 的 S 弯底速和丢线底速。 */
#define TRACK_SCURVE_SPEED_SCALE_MIN 0.84f
#define TRACK_SCURVE_LOSS_SPEED_SCALE_MIN 0.66f
/* 只保留简单的中心/变化率/角速度退出证据，不再带时间门槛。 */
#define TRACK_SCURVE_EXIT_CENTER_ERROR 0.90f
#define TRACK_SCURVE_EXIT_CENTER_DELTA 0.22f
#define TRACK_SCURVE_EXIT_CENTER_YAW_RATE 70.0f
#define TRACK_SCURVE_EXIT_CONFIRM_COUNT 3u

/* ========== 速度斜坡 ========== */
#define SPEED_TARGET_DEFAULT 10.0f  /* 初始目标速度 (仅用于上电初始化, 切换模式时会覆盖) */
#define SPEED_ENTRY 9.2f            /* 起步速度 = 死区/前馈增益, 确保不卡死区 */
/* exp0409 起步时 `sr` 约从 40 开始再往 50 爬升。 */
#define SPEED_START_DEFAULT 40.0f
#define SPEED_RAMP_RATE 20.0f
#define SPEED_RAMP_DOWN_RATE 18.0f
#define SPEED_CORE_SLEW_STEP 18     /* 速度环 pwmCore 每拍最大变化量, 压住 exp123 这类直线段油门抽动 */
#define SPEED_OUTPUT_LIMIT 600.0f   /* 速度环PWM输出上限 */
#define SPEED_FEEDFORWARD_GAIN 8.7f /* 前馈增益: target*ff≈87, 刚好超过死区 */
/* 速度环负积分耦合修正:
 * 1. capture / scurve 丢线低速 这类约束态会主动压低目标速度，如果仍按全 KI 积负误差，
 *    speedPID 会欠下一笔很大的“负积分债”，后面即使约束放开，pwmCore 也会继续往下掉；
 * 2. 这里把约束态下的负积分衰减到更小，同时在约束解除后主动释放这笔负积分，
 *    让“前端临时减速”不再污染整段后续速度。 */
#define SPEED_NEG_INTEGRAL_ATTEN 0.20f
#define SPEED_INTEGRAL_RELEASE_RATE 24.0f
#define PID_DERIV_LPF_ALPHA 0.15f   /* 微分项低通滤波系数 */
#define HEADING_INTEGRAL_ATTEN 0.3f /* 积分衰减: 误差超过INTEGRAL_ZONE时KI乘以此系数 */

#endif
