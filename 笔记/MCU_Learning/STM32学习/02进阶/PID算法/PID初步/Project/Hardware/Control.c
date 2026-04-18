#include "stm32f10x.h"
#include "Control.h"
#include "Motor.h"
#include "VOFA.h"
#include "Delay.h"
#include "Key.h"
#include "OLED.h"
#include "FuzzyPID.h"
#include <stdio.h>

volatile uint32_t g_mpuReadOkCount = 0;
volatile uint32_t g_mpuReadFailCount = 0;

// 控制周期
#define OUTER_LOOP_PERIOD_MS    20    // 外环 20ms
#define INNER_LOOP_PERIOD_MS    10    // 内环 10ms
#define VOFA_SEND_PERIOD_MS     50    // VOFA 发送 50ms
#define YAW_LOCK_TIME_MS        300   // 启动后 300ms 锁定航向

// 数据融合权重
#define GYRO_WEIGHT             0.92f  // 陀螺仪权重 92%
#define ENCODER_WEIGHT          0.08f  // 编码器权重 8%

// 软启动：内环每10ms允许PWM变化的最大步进
#define PWM_RAMP_STEP           2

// 航向修正平滑：外环每20ms允许headingCorr变化的最大步进
#define HEADING_CORR_SLEW_STEP  0.5f

// 航向修正注入增益：放大差速幅度，让低速下也能拉开左右轮
#define HEADING_CORR_GAIN       2.0f

// 航向纠偏PWM后混控增益：把headingCorr(°)映射到差速PWM，避免速度环饱和吞掉纠偏
#define HEADING_PWM_GAIN        6.0f

// 初始化
// skipMPU: 1=跳过MPU6050初始化（速度环测试模式），0=正常初始化（使用模糊PID）
void Control_Init(ControlSystem_t *sys, uint8_t skipMPU) {
    // 清零结构体
    uint32_t i;
    uint8_t *ptr = (uint8_t *)sys;
    for (i = 0; i < sizeof(ControlSystem_t); i++) {
        ptr[i] = 0;
    }
    
    // 设置测试模式标志（在清零之后）
    if (skipMPU) {
        sys->testMode = TEST_MODE_SPEED_ONLY;
    } else {
        sys->testMode = TEST_MODE_NORMAL;  // 正常模式使用模糊PID
    }
    
    // 初始化硬件
    Key_Init();
    Encoder_Timer_Init();
    Motor_Init();
    VOFA_Init();
    
    // 初始化模糊PID（航向控制）- 仅在正常模式
    if (!skipMPU) {
        FuzzyPID_Init(&sys->fuzzyHeadingPID);
        // 设置航向控制参数范围
        FuzzyPID_SetParameters(&sys->fuzzyHeadingPID, 
                             2.0f, 0.0f, 0.1f,           // 初始Kp, Ki, Kd
                             -180.0f, 180.0f,            // 误差范围（航向角）
                             -50.0f, 50.0f,              // 误差变化率范围
                             0.0f, 10.0f,                // Kp输出范围
                             0.0f, 0.5f,                 // Ki输出范围
                             0.0f, 2.0f);                // Kd输出范围
    }
    
    // 初始化速度 PID（带前馈和开环力矩）
    PID_Init(&sys->leftSpeedPID, SPEED_PID_KP, SPEED_PID_KI, SPEED_PID_KD,
             SPEED_PID_INT_LIM, SPEED_PID_OUT_LIM);
    PID_Init(&sys->rightSpeedPID, SPEED_PID_KP, SPEED_PID_KI, SPEED_PID_KD,
             SPEED_PID_INT_LIM, SPEED_PID_OUT_LIM);

    // 限制最大PWM占空比（降低整体车速，避免低目标速度也跑很快）
    sys->leftSpeedPID.outputLimit = 20.0f;
    sys->rightSpeedPID.outputLimit = 20.0f;
    
    // 启用前馈控制（基于目标速度）
    // 系数需要通过空载测试确定：各速度下所需的占空比
    PID_SetFeedforward(&sys->leftSpeedPID, SPEED_FEEDFORWARD_K, 0);
    PID_SetFeedforward(&sys->rightSpeedPID, SPEED_FEEDFORWARD_K, 0);
    
    // 启用开环力矩控制（基于当前速度的反电动势）
    // 系数需要通过空载测试确定：反电动势/速度
    PID_SetTorqueControl(&sys->leftSpeedPID, SPEED_EMF_K, 0);
    PID_SetTorqueControl(&sys->rightSpeedPID, SPEED_EMF_K, 0);
    
    // 速度环测试模式：跳过MPU6050初始化
    if (!skipMPU) {
        // 正常模式：初始化并校准 MPU6050
        MPU6050_Init();
        OLED_ShowString(1, 1, "Calibrating...");
        MPU6050_Calibrate(&sys->mpu, 200);  // 200 次采样
        OLED_ShowString(1, 1, "Ready!       ");
    }
    
    // 禁用电机
    Motor_Disable();
    sys->isRunning = 0;
}

// 启动
void Control_Start(ControlSystem_t *sys) {
    sys->tickCount = 0;
    
    // 速度环测试模式：直接启动，不需要航向锁定
    if (sys->testMode == TEST_MODE_SPEED_ONLY) {
        PID_Reset(&sys->leftSpeedPID);
        PID_Reset(&sys->rightSpeedPID);
        Motor_Enable();
        sys->isRunning = 1;
        return;
    }
    
    // 正常模式：先锁定航向/复位，再进入运行，避免中断提前介入
    Control_LockHeading(sys);
    FuzzyPID_Reset(&sys->fuzzyHeadingPID);
    PID_Reset(&sys->leftSpeedPID);
    PID_Reset(&sys->rightSpeedPID);
    
    Motor_Enable();
    sys->isRunning = 1;
}

// 停止
void Control_Stop(ControlSystem_t *sys) {
    Motor_Stop();
    Motor_Disable();
    sys->headingCorr = 0.0f;
    sys->yawErr = 0.0f;
    sys->leftPWM = 0;
    sys->rightPWM = 0;
    if (sys->testMode == TEST_MODE_NORMAL) {
        FuzzyPID_Reset(&sys->fuzzyHeadingPID);
    }
    PID_Reset(&sys->leftSpeedPID);
    PID_Reset(&sys->rightSpeedPID);
    sys->isRunning = 0;
}

// 设置测试模式
void Control_SetTestMode(ControlSystem_t *sys, TestMode_t mode) {
    sys->testMode = mode;
}

// 设置目标速度
void Control_SetTargetSpeed(ControlSystem_t *sys, int16_t speed) {
    sys->targetSpeed = speed;
}

// 航向锁定
void Control_LockHeading(ControlSystem_t *sys) {
    sys->targetYaw = sys->mpu.yaw;
    PID_Reset(&sys->headingPID);
}

// 数据融合
void Control_FuseHeading(ControlSystem_t *sys, float dt) {
    float gyroYawRate = sys->mpu.yawRate;
    
    // 编码器差分估算角速度（简化模型）
    // 左轮快 -> 右转（正角速度），右轮快 -> 左转（负角速度）
    float encoderYawRate = 0.0f;
    if (sys->isRunning) {
        int16_t encoderDiff = sys->encoder.leftSpeed - sys->encoder.rightSpeed;
        encoderYawRate = -encoderDiff * 0.1f;  // 系数需根据实际调整
    }
    
    // 融合角速度
    float fusedYawRate = GYRO_WEIGHT * gyroYawRate + ENCODER_WEIGHT * encoderYawRate;
    
    // 更新航向角
    sys->mpu.yaw += fusedYawRate * dt;
    
    // 归一化
    while (sys->mpu.yaw > 180.0f) sys->mpu.yaw -= 360.0f;
    while (sys->mpu.yaw < -180.0f) sys->mpu.yaw += 360.0f;
}

// 外环控制（航向模糊PID）
void Control_OuterLoop(ControlSystem_t *sys) {
    if (!sys->isRunning) return;
    
    // 仅在正常模式执行模糊PID航向控制
    if (sys->testMode != TEST_MODE_NORMAL) return;
    
    // 计算航向误差
    sys->yawErr = Control_GetYawError(sys);

    // 误差死区：小误差不纠偏，避免积分/漂移导致慢慢越纠偏
    float yawAbs = sys->yawErr;
    if (yawAbs < 0.0f) yawAbs = -yawAbs;
    if (yawAbs < 1.0f) {
        sys->headingCorr = 0.0f;
        FuzzyPID_Reset(&sys->fuzzyHeadingPID);
        return;
    }
    
    // 使用模糊PID计算航向修正量
    // 目标航向角 = sys->targetYaw，当前航向角 = sys->mpu.yaw
    float corr = FuzzyPID_Compute(&sys->fuzzyHeadingPID, sys->targetYaw, sys->mpu.yaw);
    
    // 应用控制方向（根据实际硬件调整符号）
    corr *= HEADING_CORR_SIGN;

    // 航向修正限幅：降低打满概率，减少左右轮“停/满”抖动
    // A: 允许修正量按目标速度的一半变化，低速也至少保留一点修正能力
    float maxCorr = (float)sys->targetSpeed;
    if (maxCorr < 0.0f) maxCorr = -maxCorr;
    maxCorr *= 0.5f;
    if (maxCorr < 1.0f) maxCorr = 1.0f;
    if (maxCorr > 10.0f) maxCorr = 10.0f;
    if (corr > maxCorr) corr = maxCorr;
    else if (corr < -maxCorr) corr = -maxCorr;

    // B: headingCorr限速/平滑（外环20ms）
    {
        float d = corr - sys->headingCorr;
        if (d > HEADING_CORR_SLEW_STEP) d = HEADING_CORR_SLEW_STEP;
        else if (d < -HEADING_CORR_SLEW_STEP) d = -HEADING_CORR_SLEW_STEP;
        sys->headingCorr += d;
        // 平滑后再做一次绝对限幅，避免历史值超过maxCorr
        if (sys->headingCorr > maxCorr) sys->headingCorr = maxCorr;
        else if (sys->headingCorr < -maxCorr) sys->headingCorr = -maxCorr;
    }
}

// 内环控制（速度 PI + 前馈 + 开环力矩）
// 控制框图：
// 目标速度 ──→ (+) ──→ PID ──→ (+) ──→ PWM输出
//              ↑         ↑       ↑
//           反馈速度    前馈    反电动势
//                        (目标)  (当前)
void Control_InnerLoop(ControlSystem_t *sys) {
    if (!sys->isRunning) return;
    
    // 更新编码器速度
    Encoder_UpdateSpeed(&sys->encoder, INNER_LOOP_PERIOD_MS);
    
    int16_t leftTarget, rightTarget;
    
    // 根据测试模式计算目标速度
    if (sys->testMode == TEST_MODE_SPEED_ONLY) {
        // 速度环测试模式：两轮相同目标速度
        leftTarget = sys->targetSpeed;
        rightTarget = sys->targetSpeed;
    } else {
        // 偏航较大时优先回正：降低基础速度，避免“越跑越偏”
        int16_t baseSpeed = sys->targetSpeed;
        float yawAbs = sys->yawErr;
        if (yawAbs < 0.0f) yawAbs = -yawAbs;
        const int16_t minForwardSpeed = 2;
        const int16_t minBaseSpeedRun = 2;
        uint8_t hardStop = 0;
        if (yawAbs > 15.0f) {
            // 不做“硬停=0”，否则会出现长时间L/R=1/1几乎不走；改为最低仍保持前进
            baseSpeed = minForwardSpeed;
            hardStop = 1;
        }
        // 给纠偏留余量：纠偏越大，公共速度分量越小，避免两侧同时饱和导致纠偏无效
        {
            float corrAbs = sys->headingCorr;
            if (corrAbs < 0.0f) corrAbs = -corrAbs;
            // 低速时不能减得过狠，否则baseSpeed会到0导致两轮长期0/0“刹停”抽搐
            // SPD<=3 时先不做baseSpeed下调，否则差速空间太小会把corr夹成0，导致无法纠偏只会越走越偏
            if (sys->targetSpeed > 3) {
                baseSpeed -= (int16_t)(corrAbs + 0.5f);
            }
            if (!hardStop) {
                if (baseSpeed < minBaseSpeedRun) baseSpeed = minBaseSpeedRun;
            }
        }
        // 正常模式：基础速度 + 航向修正
        {
            int16_t corrInt = (int16_t)(sys->headingCorr * HEADING_CORR_GAIN);
            // 避免蛇形摆动：保证运行时两侧目标速度都>=1，禁止出现“单边刹车(=0)”式纠偏
            {
                const int16_t minWheel = 1;
                int16_t maxCorrInt = (int16_t)(baseSpeed - minWheel);
                if (maxCorrInt < 0) maxCorrInt = 0;
                if (corrInt > maxCorrInt) corrInt = maxCorrInt;
                else if (corrInt < -maxCorrInt) corrInt = -maxCorrInt;
            }
            leftTarget = baseSpeed - corrInt;
            rightTarget = baseSpeed + corrInt;
        }
    }

    // 防止目标速度为负导致电机反转（反转会造成原地打转）
    if (leftTarget < 0) leftTarget = 0;
    if (rightTarget < 0) rightTarget = 0;
    
    // 计算速度误差
    int16_t leftErr = leftTarget - sys->encoder.leftSpeed;
    int16_t rightErr = rightTarget - sys->encoder.rightSpeed;
    
    // 使用完整版 PID 计算（PID + 前馈 + 开环力矩）
    // 参数：误差, dt, 目标速度(用于前馈), 当前速度(用于反电动势)
    float leftOut = PID_CalculateFull(&sys->leftSpeedPID, leftErr, 
                                       INNER_LOOP_PERIOD_MS / 1000.0f,
                                       (float)leftTarget, 
                                       (float)sys->encoder.leftSpeed);
    float rightOut = PID_CalculateFull(&sys->rightSpeedPID, rightErr, 
                                        INNER_LOOP_PERIOD_MS / 1000.0f,
                                        (float)rightTarget, 
                                        (float)sys->encoder.rightSpeed);
    
    // 转换为 PWM
    {
        int16_t leftTargetPwm = (int16_t)leftOut;
        int16_t rightTargetPwm = (int16_t)rightOut;

        // PWM 后混控：在速度环输出之上叠加航向差速，确保纠偏能体现在L/R上
        if (sys->testMode != TEST_MODE_SPEED_ONLY) {
            int16_t diffPwm = (int16_t)(sys->headingCorr * HEADING_PWM_GAIN);
            leftTargetPwm -= diffPwm;
            rightTargetPwm += diffPwm;
        }

        // 叠加后做总限幅（与速度环输出限幅一致），避免越界
        {
            int16_t limL = (int16_t)(sys->leftSpeedPID.outputLimit);
            int16_t limR = (int16_t)(sys->rightSpeedPID.outputLimit);
            if (limL < 0) limL = -limL;
            if (limR < 0) limR = -limR;
            if (leftTargetPwm > limL) leftTargetPwm = limL;
            else if (leftTargetPwm < -limL) leftTargetPwm = -limL;
            if (rightTargetPwm > limR) rightTargetPwm = limR;
            else if (rightTargetPwm < -limR) rightTargetPwm = -limR;
        }

        int16_t dl = leftTargetPwm - sys->leftPWM;
        int16_t dr = rightTargetPwm - sys->rightPWM;
        if (dl > PWM_RAMP_STEP) dl = PWM_RAMP_STEP;
        else if (dl < -PWM_RAMP_STEP) dl = -PWM_RAMP_STEP;
        if (dr > PWM_RAMP_STEP) dr = PWM_RAMP_STEP;
        else if (dr < -PWM_RAMP_STEP) dr = -PWM_RAMP_STEP;
        sys->leftPWM += dl;
        sys->rightPWM += dr;
    }
    
    // 设置电机
    Motor_SetDiffSpeed(sys->leftPWM, sys->rightPWM);
}

// VOFA+ 数据发送
// 调速度环时显示：
// ch0: 目标速度
// ch1: 左电机实际速度
// ch2: 右电机实际速度
void Control_SendVOFA(ControlSystem_t *sys) {
#if ENABLE_REALTIME_STREAM
    // 发送速度数据（方便调速度环PID）
    // 目标速度、左轮速度、右轮速度
    float targetSpeed = (float)sys->targetSpeed;
    float leftSpeed = (float)sys->encoder.leftSpeed;
    float rightSpeed = (float)sys->encoder.rightSpeed;
    VOFA_SendFloat3(targetSpeed, leftSpeed, rightSpeed);
#else
    char out[128];
    int y = (int)sys->mpu.yaw;
    int e = (int)(sys->yawErr * 10.0f);
    int c = (int)(sys->headingCorr * 10.0f);
    int yr = (int)(sys->mpu.yawRate * 100.0f);
    int ty = (int)(sys->targetYaw);
    snprintf(out, sizeof(out),
             "HB tick=%lu run=%u spd=%d y=%d ty=%d e=%d c=%d yr=%d gz=%d gzo=%d L=%d R=%d ok=%lu fail=%lu\r\n",
             (unsigned long)sys->tickCount,
             (unsigned)sys->isRunning,
             (int)sys->targetSpeed,
             y, ty, e, c, yr,
             (int)sys->mpu.gyroZ,
             (int)sys->mpu.gyroZOffset,
             (int)sys->leftPWM,
             (int)sys->rightPWM,
             (unsigned long)g_mpuReadOkCount,
             (unsigned long)g_mpuReadFailCount);
    VOFA_SendString(out);
#endif
}

// VOFA+ 参数解析（暂时禁用，因为使用模糊PID自适应调参）
void Control_ParseVOFA(ControlSystem_t *sys) {
    char cmd[32];
    uint8_t got = VOFA_TakeCommand(cmd, sizeof(cmd));
    if (!got) {
        return;
    }

    if (cmd[0] != '#') {
        return;
    }

    if (cmd[1] == 'S' && cmd[2] == 'T' && cmd[3] == 'A' && cmd[4] == 'T' && cmd[5] == '\0') {
        char out[128];
        int y = (int)sys->mpu.yaw;
        int e = (int)(sys->yawErr * 10.0f);
        int c = (int)(sys->headingCorr * 10.0f);
        int yr = (int)(sys->mpu.yawRate * 100.0f);
        int ty = (int)(sys->targetYaw);
        snprintf(out, sizeof(out),
                 "STAT tick=%lu run=%u spd=%d y=%d ty=%d e=%d c=%d yr=%d gz=%d gzo=%d L=%d R=%d ok=%lu fail=%lu\r\n",
                 (unsigned long)sys->tickCount,
                 (unsigned)sys->isRunning,
                 (int)sys->targetSpeed,
                 y, ty, e, c, yr,
                 (int)sys->mpu.gyroZ,
                 (int)sys->mpu.gyroZOffset,
                 (int)sys->leftPWM,
                 (int)sys->rightPWM,
                 (unsigned long)g_mpuReadOkCount,
                 (unsigned long)g_mpuReadFailCount);
        VOFA_SendString(out);
        return;
    }

    if (cmd[1] == 'R' && cmd[2] == 'U' && cmd[3] == 'N' && cmd[4] == '\0') {
        Control_LockHeading(sys);
        Control_Start(sys);
        VOFA_SendString("OK RUN\r\n");
        return;
    }

    if (cmd[1] == 'S' && cmd[2] == 'T' && cmd[3] == 'O' && cmd[4] == 'P' && cmd[5] == '\0') {
        Control_Stop(sys);
        VOFA_SendString("OK STOP\r\n");
        return;
    }

    if (cmd[1] == 'S' && cmd[2] == 'P' && cmd[3] == 'D' && cmd[4] == '=') {
        int spd = 0;
        int sign = 1;
        const char *p = &cmd[5];
        if (*p == '-') {
            sign = -1;
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            spd = spd * 10 + (*p - '0');
            p++;
        }
        spd *= sign;
        Control_SetTargetSpeed(sys, (int16_t)spd);
        VOFA_SendString("OK SPD\r\n");
        return;
    }

    VOFA_SendString("ERR\r\n");
}

// 定时器中断回调（1ms）
void Control_Tick(ControlSystem_t *sys) {
    static uint16_t outerCounter = 0;
    static uint16_t innerCounter = 0;
    static uint16_t vofaCounter = 0;
    
    sys->tickCount++;
    
    // 外环（20ms）- 仅在非测试模式执行
    if (sys->testMode != TEST_MODE_SPEED_ONLY) {
        if (++outerCounter >= OUTER_LOOP_PERIOD_MS) {
            outerCounter = 0;
            
            // 读取 MPU6050
            if (MPU6050_ReadAll(&sys->mpu)) {
                g_mpuReadOkCount++;
                // 数据融合
                Control_FuseHeading(sys, OUTER_LOOP_PERIOD_MS / 1000.0f);
                
                // 外环控制
                if (sys->isRunning) {
                    Control_OuterLoop(sys);
                }
            } else {
                g_mpuReadFailCount++;
            }
        }
    }
    
    // 内环（10ms）
    if (++innerCounter >= INNER_LOOP_PERIOD_MS) {
        innerCounter = 0;
        if (sys->isRunning) {
            Control_InnerLoop(sys);
        } else {
            Motor_Stop();
            sys->headingCorr = 0.0f;
            sys->yawErr = 0.0f;
            sys->leftPWM = 0;
            sys->rightPWM = 0;
        }
    }
    
    // VOFA 发送（50ms）
    if (++vofaCounter >= VOFA_SEND_PERIOD_MS) {
        vofaCounter = 0;
        Control_SendVOFA(sys);
    }
    
    // VOFA 参数解析（每次中断都检查）
    Control_ParseVOFA(sys);
}

// 获取航向误差
float Control_GetYawError(ControlSystem_t *sys) {
    return MPU6050_GetYawError(sys->targetYaw, sys->mpu.yaw);
}
