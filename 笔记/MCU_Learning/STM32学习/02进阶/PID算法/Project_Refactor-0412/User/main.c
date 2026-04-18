#include "stm32f10x.h"
#include "config.h"
#include "pid_controller.h"
#include "motor_driver.h"
#include "sensor_fusion.h"
#include "bsp_uart.h"
#include "bsp_key.h"
#include "bsp_oled.h"
#include "Delay.h"
#include "line_track.h"
#include <stdio.h>
#include <string.h>

/* ========== System State ========== */

static SystemState_t g_sysState = SYS_STOP;
static ControlMode_t g_mode = MODE_STRAIGHT;
static DualLoopState_t g_pid;
static Encoder_Data_t g_encoder;
static IMU_Data_t g_imu;
static LineSensor_Data_t g_line;

static volatile uint32_t g_tickMs = 0;
static volatile uint8_t g_controlFlag = 0;
static uint32_t g_runStartTick = 0;
static uint32_t g_lastTelemetryTick = 0;
static uint32_t g_lastControlTick = 0;
static uint32_t g_lastImuTick = 0;
static uint32_t g_lastDisplayTick = 0;
static uint8_t g_displayDirty = 1;
static float g_fastYawRate = 0.0f;

/* ========== System Timer (TIM4 1ms) ========== */

static void Timer_Init(void)
{
    TIM_TimeBaseInitTypeDef tb;
    NVIC_InitTypeDef n;

    RCC_APB1PeriphClockCmd(SYS_TIM_RCC, ENABLE);

    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = SYS_TIM_PERIOD - 1;
    tb.TIM_Prescaler = SYS_TIM_PRESCALER - 1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(SYS_TIM, &tb);

    TIM_ITConfig(SYS_TIM, TIM_IT_Update, ENABLE);
    TIM_ClearITPendingBit(SYS_TIM, TIM_IT_Update);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    n.NVIC_IRQChannel = SYS_TIM_IRQn;
    n.NVIC_IRQChannelCmd = ENABLE;
    n.NVIC_IRQChannelPreemptionPriority = 2;
    n.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&n);

    g_tickMs = 0;
    TIM_Cmd(SYS_TIM, ENABLE);
}

void Main_TimerTickISR(void)
{
    static uint8_t divider = 0;
    g_tickMs++;
    divider++;
    if (divider >= CONTROL_PERIOD_MS)
    {
        divider = 0;
        g_controlFlag = 1;
    }
    BspKey_Tick(g_tickMs);
}

/* ========== State Machine Transitions ========== */

static uint8_t is_running(void)
{
    return (g_sysState == SYS_STRAIGHT || g_sysState == SYS_TRACKING) ? 1u : 0u;
}

static void transition_start(void)
{
    if (is_running())
        return;

    BNO085_ResetAttitude(&g_imu);
    memset(&g_encoder, 0, sizeof(g_encoder));
    Encoder_Reset();

    /* Only reset integrals & state — PID gains are already set by
       #MODE= command (config.h defaults) and optionally overridden by
       serial #AKP=/#AKD= etc.  Do NOT reload defaults here. */
    DualLoop_ResetAll(&g_pid);
    if (g_mode == MODE_STRAIGHT)
    {
        g_sysState = SYS_STRAIGHT;
    }
    else
    {
        g_sysState = SYS_TRACKING;
        LineTrack_Start(TRACK_DEFAULT_CROSSINGS);
    }
    g_pid.targetYaw = 0.0f;
    g_pid.speedRampTarget = SPEED_ENTRY;

    g_fastYawRate = 0.0f;
    MotorDriver_Enable();
    g_runStartTick = g_tickMs;
    g_lastImuTick = g_tickMs;
    g_displayDirty = 1;
}

static void transition_stop(void)
{
    MotorDriver_Stop();
    MotorDriver_Disable();
    LineTrack_Stop();
    DualLoop_ResetAll(&g_pid);
    g_sysState = SYS_STOP;
    g_displayDirty = 1;
}

static void transition_toggle_mode(void)
{
    if (is_running())
        return;
    g_mode = (g_mode == MODE_STRAIGHT) ? MODE_TRACK : MODE_STRAIGHT;
    if (g_mode == MODE_STRAIGHT)
        DualLoop_LoadStraightDefaults(&g_pid);
    else
        DualLoop_LoadTrackDefaults(&g_pid);
    g_displayDirty = 1;
}

/* ========== Key Event Handler ========== */

static void process_key_event(void)
{
    KeyEvent_t evt = BspKey_Read();
    if (evt == KEY_EVENT_NONE)
        return;

    if (evt == KEY_EVENT_SHORT_PRESS)
    {
        if (is_running())
            transition_stop();
        else
            transition_start();
    }
    else if (evt == KEY_EVENT_LONG_PRESS)
    {
        transition_toggle_mode();
    }
}

/* ========== Command Parsing ========== */

static uint8_t cmd_parse_float(const char *s, float *out)
{
    if (sscanf(s, "%f", out) == 1)
        return 1u;
    /* Keil microlib sscanf may fail on "0", "0.0", etc. */
    {
        const char *p = s;
        if (*p == '-' || *p == '+')
            p++;
        if (*p >= '0' && *p <= '9')
        {
            *out = 0.0f;
            return 1u;
        }
    }
    return 0u;
}

static void handle_command(const char *cmd)
{
    float fval;

    if (strcmp(cmd, "#RUN!") == 0)
    {
        transition_start();
        BspUart_SendString("OK:RUN\r\n");
        return;
    }
    if (strcmp(cmd, "#STOP!") == 0)
    {
        transition_stop();
        BspUart_SendString("OK:STOP\r\n");
        return;
    }
    if (strcmp(cmd, "#MODE=STRAIGHT!") == 0)
    {
        if (!is_running())
        {
            g_mode = MODE_STRAIGHT;
            DualLoop_LoadStraightDefaults(&g_pid);
            g_displayDirty = 1;
            BspUart_SendString("OK:MODE=STRAIGHT\r\n");
        }
        return;
    }
    if (strcmp(cmd, "#MODE=TRACK!") == 0)
    {
        if (!is_running())
        {
            g_mode = MODE_TRACK;
            DualLoop_LoadTrackDefaults(&g_pid);
            g_displayDirty = 1;
            BspUart_SendString("OK:MODE=TRACK\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#SPD=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_pid.targetSpeed = fval;
            BspUart_SendString("OK:SPD\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#SKP=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_pid.speedPID.kp = fval;
            BspUart_SendString("OK:SKP\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#SKI=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_pid.speedPID.ki = fval;
            BspUart_SendString("OK:SKI\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#SKD=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_pid.speedPID.kd = fval;
            BspUart_SendString("OK:SKD\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#AKP=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_pid.headingPID.kp = fval;
            BspUart_SendString("OK:AKP\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#AKI=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_pid.headingPID.ki = fval;
            BspUart_SendString("OK:AKI\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#AKD=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_pid.headingPID.kd = fval;
            BspUart_SendString("OK:AKD\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#LKP=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_lineTrack.kp = fval;
            BspUart_SendString("OK:LKP\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#LKD=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_lineTrack.kd = fval;
            BspUart_SendString("OK:LKD\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#SFF=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_pid.feedforwardGain = fval;
            BspUart_SendString("OK:SFF\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#HTR=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            g_pid.headingTrim = fval;
            BspUart_SendString("OK:HTR\r\n");
        }
        return;
    }
    if (strcmp(cmd, "#STAT!") == 0)
    {
        BspUart_SendStat(g_sysState, g_mode,
                         g_pid.speedPID.kp, g_pid.speedPID.ki, g_pid.speedPID.kd,
                         g_pid.headingPID.kp, g_pid.headingPID.ki, g_pid.headingPID.kd,
                         g_lineTrack.kp, 0.0f, g_lineTrack.kd,
                         g_pid.targetSpeed);
        return;
    }
    if (strcmp(cmd, "#CAL!") == 0)
    {
        BspUart_SendString("OK:CAL\r\n");
        return;
    }
    BspUart_SendString("ERR\r\n");
}

/* ========== Control Loop ========== */

static void run_control(uint32_t now)
{
    float dt, avgSpeed, dt_norm;

    dt = (float)(now - g_lastControlTick) * 0.001f;
    if (dt <= 0.0f)
        dt = 0.01f;
    if (dt > 0.5f)
        dt = 0.5f;
    g_lastControlTick = now;

    Encoder_Update(&g_encoder);

    dt_norm = dt * 100.0f;
    if (dt_norm < 0.5f)
        dt_norm = 0.5f;
    {
        float nl = (float)g_encoder.leftSpeed / dt_norm;
        float nr = (float)g_encoder.rightSpeed / dt_norm;
        g_encoder.filteredLeftSpeed = ENC_SPEED_LPF_ALPHA * nl + (1.0f - ENC_SPEED_LPF_ALPHA) * g_encoder.filteredLeftSpeed;
        g_encoder.filteredRightSpeed = ENC_SPEED_LPF_ALPHA * nr + (1.0f - ENC_SPEED_LPF_ALPHA) * g_encoder.filteredRightSpeed;
    }

    if (!is_running())
        return;

    if (g_sysState == SYS_STRAIGHT)
    {
        avgSpeed = (g_encoder.filteredLeftSpeed + g_encoder.filteredRightSpeed) * 0.5f;
        DualLoop_ComputeStraight(&g_pid, avgSpeed, g_imu.yaw, g_imu.yawRate, g_fastYawRate, dt);
        MotorDriver_SetCoreDiff(g_pid.pwmCore, g_pid.headingDiffPWM);
        g_pid.leftPWM  = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();
    }
    else if (g_sysState == SYS_TRACKING)
    {
        /* Speed loop (encoder feedback → pwmCore as base speed) */
        avgSpeed = (g_encoder.filteredLeftSpeed + g_encoder.filteredRightSpeed) * 0.5f;
        DualLoop_ComputeSpeed(&g_pid, avgSpeed, dt);

        /* Line-following loop (sensor → PD → differential on top of pwmCore) */
        LineTrack_Update(now, g_pid.pwmCore, g_imu.yaw);

        /* 阻塞式转弯结束后重置速度PID，防止积分残留导致速度跳变 */
        if (g_lineTrack.cornerDone)
        {
            g_lineTrack.cornerDone = 0;
            PID_Reset(&g_pid.speedPID);
            g_pid.speedRampTarget = SPEED_ENTRY;
        }

        g_pid.leftPWM  = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();
        /* Check if track auto-stopped (crossing count reached / overrun) */
        if (!LineTrack_IsRunning())
        {
            transition_stop();
        }
    }
}

static void run_imu_update(void)
{
    float dt;
    uint32_t now = g_tickMs;
    dt = (float)(now - g_lastImuTick) * 0.001f;
    if (dt <= 0.0f || dt > 0.5f)
        dt = 0.2f;
    BNO085_ReadAll(&g_imu);
    BNO085_UpdateYaw(&g_imu, dt);
    /* Update gyro LPF ONLY when fresh IMU data arrives.
       Negate gyroZf: BNO085 Z-axis is inverted relative to
       game rotation vector yaw convention (Z pointing down).
       d(yaw)/dt > 0 = left turn, but gyroZf < 0 for left turn. */
    g_fastYawRate += 0.5f * (-g_imu.gyroZf - g_fastYawRate);
    g_lastImuTick = g_tickMs;
}

/* ========== Telemetry ========== */

static void send_telemetry(void)
{
    uint32_t t = g_tickMs - g_runStartTick;
    uint8_t run = is_running();
    float acuteYawDelta = 0.0f;
    uint32_t acuteRearmTick = 0;

    if (g_sysState == SYS_TRACKING)
    {
        if (g_lineTrack.acuteState == LT_ACUTE_TURNING
            || g_lineTrack.acuteState == LT_ACUTE_RECOVER)
        {
            acuteYawDelta = g_imu.yaw - g_lineTrack.acuteStartYaw;
            if (acuteYawDelta > 180.0f)  acuteYawDelta -= 360.0f;
            if (acuteYawDelta < -180.0f) acuteYawDelta += 360.0f;
        }
        if (g_lineTrack.acuteRearmTick > g_runStartTick)
            acuteRearmTick = g_lineTrack.acuteRearmTick - g_runStartTick;

        BspUart_SendTelemetryTrack(t, run,
                                   g_encoder.leftSpeed, g_encoder.rightSpeed,
                                   g_imu.yaw, g_imu.yawRate,
                                   g_pid.pwmCore, g_lineTrack.devSpeed,
                                   g_pid.leftPWM, g_pid.rightPWM,
                                   g_lineTrack.sensorBits,
                                   (float)g_lineTrack.weightedPos,
                                   g_lineTrack.acuteState, acuteYawDelta,
                                   acuteRearmTick,
                                   g_lineTrack.dbgTrackState,
                                   g_lineTrack.dbgCornerDir,
                                   g_lineTrack.dbgCornerYawDelta,
                                   g_lineTrack.dbgCornerBits,
                                   g_lineTrack.dbgCornerAcceptMask,
                                   g_lineTrack.dbgCornerYawReady,
                                   g_lineTrack.dbgCornerAcceptHit);
    }
    else
    {
        BspUart_SendTelemetryStraight(t, run,
                                      g_encoder.leftSpeed, g_encoder.rightSpeed,
                                      g_imu.yaw, g_fastYawRate,
                                      g_pid.pwmCore, g_pid.headingDiffPWM,
                                      g_pid.dTermPostDZ,
                                      g_pid.leftPWM, g_pid.rightPWM,
                                      g_pid.lastHi);
    }
}

/* ========== Display (event-driven, not every frame) ========== */

static void update_display(uint32_t now)
{
    /* 运行中跳过OLED更新: I2C写阻塞20-30ms会导致控制循环错过tick,
     * 造成速度环输出周期性骤降(约每1.1s一次) */
    if (is_running())
        return;

    if (!g_displayDirty && (now - g_lastDisplayTick) < 200u)
        return;
    g_lastDisplayTick = now;
    g_displayDirty = 0;

    BspOled_ShowStatus(g_sysState, g_mode,
                       g_imu.yaw,
                       (float)g_encoder.leftSpeed,
                       (float)g_encoder.rightSpeed);
}

/* ========== Main Entry ========== */

int main(void)
{
    char cmdBuf[64];
    uint32_t now;

    /* Phase 1: BSP init */
    BspOled_Init();
    BspKey_Init();
    BspUart_Init();

    /* Phase 2: Drivers */
    MotorDriver_Init();
    Encoder_Init();
    BNO085_Init();
    LineSensor_Init();

    /* Phase 3: Control */
    DualLoop_Init(&g_pid);
    LineTrack_Init();
    memset(&g_encoder, 0, sizeof(g_encoder));
    memset(&g_imu, 0, sizeof(g_imu));
    memset(&g_line, 0, sizeof(g_line));

    /* Phase 4: System timer (starts interrupts) */
    Timer_Init();

    g_displayDirty = 1;

    while (1)
    {
        now = g_tickMs;

        /* 1. Command parsing */
        if (BspUart_TakeCommand(cmdBuf, sizeof(cmdBuf)))
        {
            handle_command(cmdBuf);
        }

        /* 2. IMU update FIRST so control always uses fresh data */
        if ((now - g_lastImuTick) >= IMU_READ_PERIOD_MS)
        {
            run_imu_update();
        }

        /* 3. Control loop (10ms period) */
        if (g_controlFlag)
        {
            g_controlFlag = 0;
            run_control(now);
        }

        /* 3. Telemetry */
        if ((now - g_lastTelemetryTick) >= TELEMETRY_PERIOD_MS)
        {
            g_lastTelemetryTick = now;
            send_telemetry();
        }

        /* 4. Display (200ms or on state change) */
        update_display(now);

        /* 5. Key events */
        process_key_event();
    }
}
