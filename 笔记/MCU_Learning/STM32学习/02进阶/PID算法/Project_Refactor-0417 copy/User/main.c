/*
 * 主控调度中心:
 * 1. 负责系统状态机、按键/串口命令、控制周期调度、遥测和显示刷新。
 * 2. 设计上把“业务编排”放在这里，把“底层驱动”和“闭环控制”下沉到各自模块。
 * 3. 读这个文件时建议按顺序看:
 *    - 全局状态
 *    - 启停状态机
 *    - 命令解析
 *    - run_control / run_imu_update / run_display
 */
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
#include "tune_params.h"
#include "stm32f10x_it.h"
#include <stdio.h>
#include <string.h>

/* ========== System State ==========
 * 这里集中保存主循环共享的运行态数据。
 * 原则上:
 * - 纯驱动内部状态留在各自模块内部
 * - 需要跨多个调度阶段共享的状态才放到这里
 */

static SystemState_t g_sysState = SYS_STOP;
static ControlMode_t g_mode = MODE_STRAIGHT;
static DualLoopState_t g_pid;
static Encoder_Data_t g_encoder;
static IMU_Data_t g_imu;

static volatile uint32_t g_tickMs = 0;
static volatile uint8_t g_controlFlag = 0;
static uint32_t g_runStartTick = 0;
static uint32_t g_lastTelemetryTick = 0;
static uint32_t g_lastControlTick = 0;
static uint32_t g_lastImuTick = 0;
static uint32_t g_lastDisplayTick = 0;
static uint8_t g_displayDirty = 1;
static float g_fastYawRate = 0.0f;
static uint32_t g_experimentId = 0;
static uint8_t g_experimentActive = 0;
static uint32_t g_experimentHostSyncUntilTick = 0;

typedef enum
{
    EXP_TRIGGER_AUTO = 0,
    EXP_TRIGGER_KEY,
    EXP_TRIGGER_UART
} ExperimentTrigger_t;

/* ========== System Timer (TIM4 1ms) ========== */

static void Timer_Init(void)
{
    TIM_TimeBaseInitTypeDef tb;
    NVIC_InitTypeDef n;

    /* TIM4 以 1ms 为基本节拍，控制周期则由 Main_TimerTickISR 内的分频器决定。 */
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
    /* 中断里只做“打点”和轻量轮询，不做复杂控制运算。 */
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
    /* 只有直线和循迹两种状态算“控制器正在驱动车轮”。 */
    return (g_sysState == SYS_STRAIGHT || g_sysState == SYS_TRACKING) ? 1u : 0u;
}

static uint8_t experiment_host_sync_active(void)
{
    return (g_experimentHostSyncUntilTick != 0u && g_tickMs < g_experimentHostSyncUntilTick) ? 1u : 0u;
}

static const char *experiment_trigger_str(ExperimentTrigger_t trigger)
{
    switch (trigger)
    {
    case EXP_TRIGGER_KEY:  return "KEY";
    case EXP_TRIGGER_UART: return "UART";
    default:               return "AUTO";
    }
}

static void report_experiment_start(ExperimentTrigger_t trigger,
                                    uint8_t skipStartupSecondTurn)
{
    char buf[96];

    sprintf(buf,
            "EVT:EXP_START,id=%lu,src=%s,mode=%c,skip=%u\r\n",
            (unsigned long)g_experimentId,
            experiment_trigger_str(trigger),
            (g_mode == MODE_TRACK) ? 'T' : 'S',
             (unsigned)skipStartupSecondTurn);
    BspUart_SendString(buf);
}

static void report_experiment_stop(ExperimentTrigger_t trigger, uint32_t durationMs)
{
    char buf[96];

    sprintf(buf,
            "EVT:EXP_STOP,id=%lu,src=%s,mode=%c,dur=%lu\r\n",
            (unsigned long)g_experimentId,
            experiment_trigger_str(trigger),
            (g_mode == MODE_TRACK) ? 'T' : 'S',
            (unsigned long)durationMs);
    BspUart_SendString(buf);
}

static void transition_start(ExperimentTrigger_t trigger)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float startSpeed;

    if (is_running())
        return;

    if (experiment_host_sync_active())
        g_experimentId++;
    g_experimentActive = 1u;
    BNO085_ResetAttitude(&g_imu);
    memset(&g_encoder, 0, sizeof(g_encoder));
    Encoder_Reset();

    /* 起跑只清空运行状态，不重载 PID 默认值。
       这样串口在线调过的参数不会在每次启停时被偷偷覆盖掉。 */
    DualLoop_ResetAll(&g_pid);
    if (g_mode == MODE_STRAIGHT)
    {
        g_sysState = SYS_STRAIGHT;
        g_pid.targetYaw = 0.0f;
    }
    else
    {
        g_sysState = SYS_TRACKING;
        LineTrack_Start(g_tickMs, g_imu.yaw);
        g_pid.targetYaw = LineTrack_GetTargetYaw();
    }
    /* start_speed 单独控制“实验起步那一拍”的内部斜坡起点。
       设为 0 时仍然保持直接按目标速度起跑；设为较小值则可模拟真实跑到 S 弯前的入弯速度。 */
    startSpeed = tune->common.startSpeed;
    if (startSpeed > 0.0f && startSpeed < g_pid.targetSpeed)
        g_pid.speedRampTarget = startSpeed;
    else
        g_pid.speedRampTarget = g_pid.targetSpeed;

    g_fastYawRate = 0.0f;
    g_runStartTick = g_tickMs;
    g_lastImuTick = g_tickMs;
    report_experiment_start(trigger, 0u);
    BspOled_ShowExperimentId(g_experimentId);
    MotorDriver_Enable();
    g_displayDirty = 1;
}

static void transition_stop(ExperimentTrigger_t trigger)
{
    uint32_t durationMs = 0u;

    if (g_experimentActive)
    {
        durationMs = g_tickMs - g_runStartTick;
        report_experiment_stop(trigger, durationMs);
    }

    MotorDriver_Stop();
    MotorDriver_Disable();
    LineTrack_Stop();
    DualLoop_ResetAll(&g_pid);
    g_sysState = SYS_STOP;
    g_experimentActive = 0u;
    BspOled_ShowExperimentId(g_experimentId);
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
    LineTrack_RefreshTune();
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
        {
            transition_stop(EXP_TRIGGER_KEY);
        }
        else
        {
            transition_start(EXP_TRIGGER_KEY);
        }
    }
    else if (evt == KEY_EVENT_LONG_PRESS)
    {
        transition_toggle_mode();
    }
}

/* ========== Command Parsing ========== */

static uint8_t cmd_parse_u32(const char *s, uint32_t *out)
{
    uint32_t value = 0u;
    uint8_t digits = 0u;
    const char *p = s;

    if (*p == '+')
        p++;
    while (*p >= '0' && *p <= '9')
    {
        value = (value * 10u) + (uint32_t)(*p - '0');
        digits = 1u;
        p++;
    }
    if (!digits)
        return 0u;
    if (*p != '\0' && *p != '!')
        return 0u;
    *out = value;
    return 1u;
}

static uint8_t cmd_copy_token(const char *src, char *out, uint8_t outSize)
{
    uint8_t i = 0u;

    if (!src || !out || outSize == 0u)
        return 0u;

    while (*src != '\0' && *src != '!' && i < (uint8_t)(outSize - 1u))
    {
        out[i++] = *src++;
    }
    out[i] = '\0';
    return (i > 0u) ? 1u : 0u;
}

static void refresh_runtime_tune(void)
{
    /* TuneParams 只拥有“当前参数值”，真正参与闭环的是 g_pid / g_lineTrack。
       所以每次串口写参后，都要把最新参数同步到当前运行态。 */
    DualLoop_RefreshTune(&g_pid, g_mode);
    LineTrack_RefreshTune();
    g_displayDirty = 1u;
}

static void send_param_list(const char *group)
{
    char buf[256];
    uint16_t i;
    uint16_t count = 0u;

    for (i = 0u; i < TuneParams_Count(); i++)
    {
        if (!TuneParams_GroupMatches(i, group))
            continue;
        if (TuneParams_FormatListLine(i, buf, sizeof(buf)))
        {
            BspUart_SendString(buf);
            count++;
        }
    }

    sprintf(buf, "OK:PLIST,count=%u\r\n", (unsigned)count);
    BspUart_SendString(buf);
}

static void send_param_value(const char *key)
{
    char buf[256];

    if (TuneParams_FormatValueLine(key, buf, sizeof(buf)))
        BspUart_SendString(buf);
    else
        BspUart_SendString("ERR:PGET\r\n");
}

static void apply_param_write(const char *key, const char *valueText)
{
    char buf[192];

    if (!key || !valueText)
    {
        BspUart_SendString("ERR:PSET\r\n");
        return;
    }

    if (!TuneParams_SetByText(key, valueText, buf, sizeof(buf)))
    {
        BspUart_SendString("ERR:PSET\r\n");
        return;
    }

    refresh_runtime_tune();
    BspUart_SendString(buf);
    send_param_value(key);
}

static uint8_t try_handle_legacy_param_write(const char *legacyKey, const char *valueText)
{
    const char *mappedKey = TuneParams_MapLegacyKey(g_mode, legacyKey);

    if (!mappedKey)
        return 0u;

    apply_param_write(mappedKey, valueText);
    return 1u;
}

static void send_experiment_id_response(void)
{
    char buf[32];

    sprintf(buf, "OK:EXP=%lu\r\n", (unsigned long)g_experimentId);
    BspUart_SendString(buf);
}

static void send_track_snapshot_response(void)
{
    LineTrack_Snapshot_t snapshot;
    char buf[192];

    /* 待机查询和待机心跳共用同一份快照，避免脚本看到的 sb/sc/ca
       和人工手查 `#TRACKSNAP?` 不是同一套判断口径。 */
    LineTrack_CollectIdleSnapshot(g_imu.yaw, g_fastYawRate, &snapshot);
    sprintf(buf,
            "SNAP:sb=%u,cnt=%u,ld=%u,ca=%u,sc=%u,"
            "lp=%.2f,tp=%.2f,pe=%.2f,yc=%.2f,ty=%.2f,ss=%.2f,cf=%.2f,dr=%.2f,yl=%.1f,ks=%.2f\r\n",
            (unsigned)snapshot.sensorBits,
            (unsigned)snapshot.sensorCount,
            (unsigned)snapshot.lineDetected,
            (unsigned)snapshot.captureActive,
            (unsigned)snapshot.sCurveActive,
            (double)snapshot.linePosition,
            (double)snapshot.targetLinePosition,
            (double)snapshot.positionError,
            (double)snapshot.yawCommand,
            (double)snapshot.targetYaw,
            (double)snapshot.speedScale,
            (double)snapshot.captureAuthorityScale,
            (double)snapshot.headingDiffRatio,
            (double)snapshot.yawLimit,
            (double)snapshot.lineKpScale);
    BspUart_SendString(buf);
}

static void sync_experiment_id(uint32_t experimentId)
{
    g_experimentId = experimentId;
    BspOled_ShowExperimentId(g_experimentId);
    g_displayDirty = 1u;
}

static void handle_command(const char *cmd)
{
    uint32_t u32val;
    char keyBuf[48];
    char valBuf[24];
    char buf[128];
    const char *comma;
    uint8_t i;
    uint16_t resetCount;

    if (strcmp(cmd, "#RUN!") == 0)
    {
        transition_start(EXP_TRIGGER_UART);
        BspUart_SendString("OK:RUN\r\n");
        return;
    }
    if (strcmp(cmd, "#STOP!") == 0)
    {
        transition_stop(EXP_TRIGGER_UART);
        BspUart_SendString("OK:STOP\r\n");
        return;
    }
    if (strcmp(cmd, "#EXP?!") == 0)
    {
        send_experiment_id_response();
        return;
    }
    if (strcmp(cmd, "#EXPHOST=OFF!") == 0)
    {
        g_experimentHostSyncUntilTick = 0u;
        BspUart_SendString("OK:EXPHOST=OFF\r\n");
        return;
    }
    if (strncmp(cmd, "#EXPHOST=", 9) == 0)
    {
        if (cmd_parse_u32(cmd + 9, &u32val))
        {
            g_experimentHostSyncUntilTick = g_tickMs + EXP_HOST_SYNC_TIMEOUT_MS;
            if (!is_running() && g_experimentId < u32val)
                sync_experiment_id(u32val);
        }
        return;
    }
    if (strncmp(cmd, "#EXP=", 5) == 0)
    {
        if (!is_running() && cmd_parse_u32(cmd + 5, &u32val))
        {
            sync_experiment_id(u32val);
            send_experiment_id_response();
        }
        else if (is_running())
        {
            BspUart_SendString("ERR:EXP=RUNNING\r\n");
        }
        return;
    }
    if (strcmp(cmd, "#PING!") == 0)
    {
        BspUart_SendString("OK:PONG\r\n");
        return;
    }
    if (strcmp(cmd, "#TRACKSNAP?!") == 0)
    {
        send_track_snapshot_response();
        return;
    }
    /* 通用参数协议从这里进入:
       - LIST / GET / GET_GROUP 负责可枚举和可观测
       - SET / DEFAULTS 负责可复现实验
       后面的旧命令只作为别名保留。 */
    if (strcmp(cmd, "#PLIST!") == 0)
    {
        send_param_list(0);
        return;
    }
    if (strncmp(cmd, "#PGET=", 6) == 0)
    {
        if (cmd_copy_token(cmd + 6, keyBuf, sizeof(keyBuf)))
            send_param_value(keyBuf);
        else
            BspUart_SendString("ERR:PGET\r\n");
        return;
    }
    if (strncmp(cmd, "#PGETG=", 7) == 0)
    {
        if (cmd_copy_token(cmd + 7, keyBuf, sizeof(keyBuf)))
            send_param_list(keyBuf);
        else
            BspUart_SendString("ERR:PGETG\r\n");
        return;
    }
    if (strcmp(cmd, "#PDEF!") == 0)
    {
        resetCount = TuneParams_LoadDefaults(0);
        refresh_runtime_tune();
        sprintf(buf, "OK:PDEF,count=%u\r\n", (unsigned)resetCount);
        BspUart_SendString(buf);
        return;
    }
    if (strncmp(cmd, "#PDEF=", 6) == 0)
    {
        if (!cmd_copy_token(cmd + 6, keyBuf, sizeof(keyBuf)))
        {
            BspUart_SendString("ERR:PDEF\r\n");
            return;
        }
        resetCount = TuneParams_LoadDefaults(keyBuf);
        if (resetCount == 0u)
        {
            BspUart_SendString("ERR:PDEF\r\n");
            return;
        }
        refresh_runtime_tune();
        sprintf(buf, "OK:PDEF,group=%s,count=%u\r\n", keyBuf, (unsigned)resetCount);
        BspUart_SendString(buf);
        return;
    }
    if (strncmp(cmd, "#PSET=", 6) == 0)
    {
        comma = strchr(cmd + 6, ',');
        if (!comma)
        {
            BspUart_SendString("ERR:PSET\r\n");
            return;
        }

        i = 0u;
        while ((cmd + 6 + i) < comma && i < (uint8_t)(sizeof(keyBuf) - 1u))
        {
            keyBuf[i] = cmd[6 + i];
            i++;
        }
        keyBuf[i] = '\0';
        if (i == 0u || !cmd_copy_token(comma + 1, valBuf, sizeof(valBuf)))
        {
            BspUart_SendString("ERR:PSET\r\n");
            return;
        }
        apply_param_write(keyBuf, valBuf);
        return;
    }
    if (strcmp(cmd, "#SAVE!") == 0)
    {
        BspUart_SendString("ERR:SAVE=UNSUPPORTED\r\n");
        return;
    }
    if (strncmp(cmd, "#MARK=", 6) == 0)
    {
        if (!cmd_copy_token(cmd + 6, keyBuf, sizeof(keyBuf)))
        {
            BspUart_SendString("ERR:MARK\r\n");
            return;
        }
        sprintf(buf,
                "EVT:MARK,name=%s,exp=%lu,t=%lu\r\n",
                keyBuf,
                (unsigned long)g_experimentId,
                (unsigned long)g_tickMs);
        BspUart_SendString(buf);
        return;
    }
    if (strcmp(cmd, "#MODE=STRAIGHT!") == 0)
    {
        if (!is_running())
        {
            g_mode = MODE_STRAIGHT;
            DualLoop_LoadStraightDefaults(&g_pid);
            LineTrack_RefreshTune();
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
            LineTrack_RefreshTune();
            g_displayDirty = 1;
            BspUart_SendString("OK:MODE=TRACK\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#SPD=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("SPD", cmd + 5))
            BspUart_SendString("ERR:SPD\r\n");
        return;
    }
    if (strncmp(cmd, "#SKP=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("SKP", cmd + 5))
            BspUart_SendString("ERR:SKP\r\n");
        return;
    }
    if (strncmp(cmd, "#SKI=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("SKI", cmd + 5))
            BspUart_SendString("ERR:SKI\r\n");
        return;
    }
    if (strncmp(cmd, "#SKD=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("SKD", cmd + 5))
            BspUart_SendString("ERR:SKD\r\n");
        return;
    }
    if (strncmp(cmd, "#AKP=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("AKP", cmd + 5))
            BspUart_SendString("ERR:AKP\r\n");
        return;
    }
    if (strncmp(cmd, "#AKI=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("AKI", cmd + 5))
            BspUart_SendString("ERR:AKI\r\n");
        return;
    }
    if (strncmp(cmd, "#AKD=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("AKD", cmd + 5))
            BspUart_SendString("ERR:AKD\r\n");
        return;
    }
    if (strncmp(cmd, "#LKP=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("LKP", cmd + 5))
            BspUart_SendString("ERR:LKP\r\n");
        return;
    }
    if (strncmp(cmd, "#LKD=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("LKD", cmd + 5))
            BspUart_SendString("ERR:LKD\r\n");
        return;
    }
    if (strncmp(cmd, "#SFF=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("SFF", cmd + 5))
            BspUart_SendString("ERR:SFF\r\n");
        return;
    }
    if (strncmp(cmd, "#HTR=", 5) == 0)
    {
        if (!try_handle_legacy_param_write("HTR", cmd + 5))
            BspUart_SendString("ERR:HTR\r\n");
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
    const TuneRuntime_t *tune = TuneParams_Get();
    float dt, avgSpeed, dt_norm;
    float trackTargetSpeed, scaledTrackTarget, trackSpeedScale;
    uint8_t trackSpeedConstraintActive;

    dt = (float)(now - g_lastControlTick) * 0.001f;
    if (dt <= 0.0f)
        dt = 0.01f;
    if (dt > 0.5f)
        dt = 0.5f;
    g_lastControlTick = now;

    /* 控制周期的第一步永远是刷新编码器观测。 */
    Encoder_Update(&g_encoder);

    dt_norm = dt * 100.0f;
    if (dt_norm < 0.5f)
        dt_norm = 0.5f;
    {
        /* 原始编码器增量会受控制周期抖动影响，这里用 dt 归一化后再做低通。 */
        float nl = (float)g_encoder.leftSpeed / dt_norm;
        float nr = (float)g_encoder.rightSpeed / dt_norm;
        g_encoder.filteredLeftSpeed = tune->common.encoderSpeedLpfAlpha * nl
                                    + (1.0f - tune->common.encoderSpeedLpfAlpha) * g_encoder.filteredLeftSpeed;
        g_encoder.filteredRightSpeed = tune->common.encoderSpeedLpfAlpha * nr
                                     + (1.0f - tune->common.encoderSpeedLpfAlpha) * g_encoder.filteredRightSpeed;
    }

    if (!is_running())
        return;

    if (g_sysState == SYS_STRAIGHT)
    {
        avgSpeed = (g_encoder.filteredLeftSpeed + g_encoder.filteredRightSpeed) * 0.5f;
        DualLoop_ComputeStraight(&g_pid, avgSpeed, g_imu.yaw, g_fastYawRate, dt);
        MotorDriver_SetCoreDiff(g_pid.pwmCore, g_pid.headingDiffPWM);
        g_pid.leftPWM  = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();
    }
    else if (g_sysState == SYS_TRACKING)
    {
        avgSpeed = (g_encoder.filteredLeftSpeed + g_encoder.filteredRightSpeed) * 0.5f;
        LineTrack_Update(now, g_imu.yaw, g_fastYawRate);

        if (!LineTrack_IsRunning())
        {
            transition_stop(EXP_TRIGGER_AUTO);
            return;
        }

        trackTargetSpeed = g_pid.targetSpeed;
        trackSpeedScale = LineTrack_GetSpeedScale();
        trackSpeedConstraintActive = LineTrack_IsSpeedConstraintActive();
        scaledTrackTarget = trackTargetSpeed * trackSpeedScale;
        /* 这里只临时压低本拍有效目标速度，不改用户设定的原始目标速度。 */
        if (trackTargetSpeed > tune->common.speedEntry && scaledTrackTarget < tune->common.speedEntry)
            scaledTrackTarget = tune->common.speedEntry;
        /* 不在这里把 speedRampTarget 一拍硬砍到缩放目标。
           真正的减速快慢统一交给 DualLoop_ApplySpeedRamp 的上下斜坡参数处理，
           这样短暂的 speedScale 波动不会把 sr 直接打穿。 */
        g_pid.targetSpeed = scaledTrackTarget;
        DualLoop_ComputeTrack(&g_pid, avgSpeed, dt,
                              LineTrack_GetYawCommand(),
                              LineTrack_GetYawLimit(),
                              LineTrack_GetHeadingDiffRatio(),
                              LineTrack_GetHeadingDiffMin(),
                              trackSpeedScale,
                              trackSpeedConstraintActive);
        g_pid.targetSpeed = trackTargetSpeed;
        MotorDriver_SetCoreDiff(g_pid.pwmCore, g_pid.headingDiffPWM);
        g_pid.leftPWM  = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();
    }
}

static void run_imu_update(void)
{
    const TuneRuntime_t *tune = TuneParams_Get();
    float dt;
    uint32_t now = g_tickMs;
    /* IMU 更新时间戳独立维护，避免控制周期偶发抖动直接污染角速度估计。 */
    dt = (float)(now - g_lastImuTick) * 0.001f;
    if (dt <= 0.0f || dt > 0.5f)
        dt = 0.2f;
    BNO085_ReadAll(&g_imu);
    BNO085_UpdateYaw(&g_imu, dt);
    /* Update gyro LPF ONLY when fresh IMU data arrives.
       Negate gyroZf: BNO085 Z-axis is inverted relative to
       game rotation vector yaw convention (Z pointing down).
       d(yaw)/dt > 0 = left turn, but gyroZf < 0 for left turn. */
    g_fastYawRate += tune->common.gyroFastLpfAlpha * (-g_imu.gyroZf - g_fastYawRate);
    g_lastImuTick = g_tickMs;
}

/* ========== Telemetry ========== */

static void send_telemetry(void)
{
    uint32_t t = g_tickMs - g_runStartTick;
    uint8_t run = is_running();
    LineTrack_Snapshot_t trackSnapshot;

    if (g_sysState == SYS_TRACKING)
    {
        /* exp0409 的循迹遥测只保留核心前端量:
           pe  看前端误差是否被捕获态放大；
           yc  看目标航向生成是否够激进；
           ss  看弯中降速是否介入；
           ca  看是否真的进入了边缘捕获态；
           sc/dr/yl/ks 用来确认当前是不是已经切进 S 弯态以及拿到了多大的权限；
           cs 用来确认当前 capture 是否正处于“反向换边软接管”；
           rc 用来确认“重新看到中间灯后”回中重锁有没有真正介入。 */
        BspUart_SendTelemetryTrack(t, g_experimentId, run,
                                   g_encoder.leftSpeed, g_encoder.rightSpeed,
                                   g_imu.yaw, g_fastYawRate,
                                   g_pid.pwmCore, g_pid.headingDiffPWM,
                                   g_pid.leftPWM, g_pid.rightPWM,
                                   g_lineTrack.sensorBits,
                                   g_lineTrack.filteredPosition,
                                   g_lineTrack.targetLinePosition,
                                   g_lineTrack.effectiveError,
                                   g_lineTrack.yawCommand,
                                   g_lineTrack.targetYaw,
                                   g_lineTrack.lineDetected,
                                   g_pid.targetSpeed,
                                   g_pid.speedRampTarget,
                                   g_lineTrack.speedScale,
                                   g_lineTrack.captureActive,
                                   LineTrack_GetCaptureAuthorityScale(),
                                   g_lineTrack.captureSwitchActive,
                                   LineTrack_GetCaptureRateReliefScale(),
                                   LineTrack_GetRecenterScale(),
                                   LineTrack_IsSCurveActive(),
                                   LineTrack_GetHeadingDiffRatio(),
                                   LineTrack_GetYawLimit(),
                                   LineTrack_GetLineKpScale());
    }
    else if (g_mode == MODE_TRACK)
    {
        /* 只要当前模式已经切到 TRACK，就在待机心跳里发一份空载快照。
           这样 experiment_logger 在发 #RUN! 之前就能等到 sb/sc/ca/ld，
           不再只能盲等“用户说已经摆回起点”。 */
        LineTrack_CollectIdleSnapshot(g_imu.yaw, g_fastYawRate, &trackSnapshot);
        BspUart_SendTelemetryTrack(t, g_experimentId, run,
                                   g_encoder.leftSpeed, g_encoder.rightSpeed,
                                   g_imu.yaw, g_fastYawRate,
                                   0, 0,
                                   0, 0,
                                   trackSnapshot.sensorBits,
                                   trackSnapshot.linePosition,
                                   trackSnapshot.targetLinePosition,
                                   trackSnapshot.effectiveError,
                                   trackSnapshot.yawCommand,
                                   trackSnapshot.targetYaw,
                                   trackSnapshot.lineDetected,
                                   g_pid.targetSpeed,
                                   g_pid.speedRampTarget,
                                   trackSnapshot.speedScale,
                                   trackSnapshot.captureActive,
                                   trackSnapshot.captureAuthorityScale,
                                   trackSnapshot.captureSwitchActive,
                                   trackSnapshot.captureRateReliefScale,
                                   trackSnapshot.recenterScale,
                                   trackSnapshot.sCurveActive,
                                   trackSnapshot.headingDiffRatio,
                                   trackSnapshot.yawLimit,
                                   trackSnapshot.lineKpScale);
    }
    else
    {
        BspUart_SendTelemetryStraight(t, g_experimentId, run,
                                      g_encoder.leftSpeed, g_encoder.rightSpeed,
                                      g_imu.yaw, g_fastYawRate,
                                      g_pid.pwmCore, g_pid.headingDiffPWM,
                                      g_pid.dTermPostDZ,
                                      g_pid.leftPWM, g_pid.rightPWM,
                                      g_pid.lastHi,
                                      g_pid.targetSpeed,
                                      g_pid.speedRampTarget);
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
                       (float)g_encoder.rightSpeed,
                       g_experimentId);
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
    BspOled_ShowFaultCode(FaultTrace_GetAndClearCode());

    /* Phase 2: System timer first, so boot diagnostics still advance if IMU init blocks */
    Timer_Init();
    BspOled_ShowIMUInit(BNO085_GetInitStage(), BNO085_GetI2CAddr());

    /* Phase 3: Drivers */
    MotorDriver_Init();
    Encoder_Init();
    BNO085_Init();
    BspOled_ShowIMUInit(BNO085_GetInitStage(), BNO085_GetI2CAddr());
    LineSensor_Init();

    /* Phase 4: Control */
    TuneParams_Init();
    DualLoop_Init(&g_pid);
    LineTrack_Init();
    memset(&g_encoder, 0, sizeof(g_encoder));
    memset(&g_imu, 0, sizeof(g_imu));

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
