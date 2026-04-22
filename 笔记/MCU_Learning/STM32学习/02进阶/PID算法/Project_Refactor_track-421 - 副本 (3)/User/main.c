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
#include "stm32f10x_it.h"
#include <stdio.h>
#include <string.h>

static SystemState_t g_sysState = SYS_STOP;
static ControlMode_t g_mode = MODE_STRAIGHT;
static DualLoopState_t g_pid;
static Encoder_Data_t g_encoder;
static IMU_Data_t g_imu;

static volatile uint32_t g_tickMs = 0u;
static volatile uint8_t g_controlFlag = 0u;
static uint32_t g_runStartTick = 0u;
static uint32_t g_lastTelemetryTick = 0u;
static uint32_t g_lastControlTick = 0u;
static uint32_t g_lastImuTick = 0u;
static uint32_t g_lastDisplayTick = 0u;
static uint8_t g_displayDirty = 1u;
static float g_fastYawRate = 0.0f;
static uint32_t g_experimentId = 0u;
static uint8_t g_experimentActive = 0u;
static uint32_t g_experimentHostSyncUntilTick = 0u;

typedef enum
{
    EXP_TRIGGER_AUTO = 0,
    EXP_TRIGGER_KEY,
    EXP_TRIGGER_UART
} ExperimentTrigger_t;

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

    g_tickMs = 0u;
    TIM_Cmd(SYS_TIM, ENABLE);
}

void Main_TimerTickISR(void)
{
    static uint8_t divider = 0u;

    g_tickMs++;
    divider++;
    if (divider >= CONTROL_PERIOD_MS)
    {
        divider = 0u;
        g_controlFlag = 1u;
    }
    BspKey_Tick(g_tickMs);
}

static uint8_t is_running(void)
{
    return (g_sysState != SYS_STOP) ? 1u : 0u;
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

static char mode_to_char(ControlMode_t mode)
{
    switch (mode)
    {
    case MODE_TRACK: return 'T';
    case MODE_SPIN:  return 'P';
    default:         return 'S';
    }
}

static void load_mode_defaults(ControlMode_t mode)
{
    if (mode == MODE_STRAIGHT)
    {
        DualLoop_LoadStraightDefaults(&g_pid);
    }
    else if (mode == MODE_TRACK)
    {
        DualLoop_LoadTrackDefaults(&g_pid);
    }
    else
    {
        DualLoop_LoadStraightDefaults(&g_pid);
        g_pid.targetSpeed = 0.0f;
    }
}

static void set_mode(ControlMode_t mode)
{
    g_mode = mode;
    load_mode_defaults(mode);
    g_displayDirty = 1u;
}

static void report_experiment_start(ExperimentTrigger_t trigger)
{
    char buf[80];

    sprintf(buf,
            "EVT:EXP_START,id=%lu,src=%s,mode=%c\r\n",
            (unsigned long)g_experimentId,
            experiment_trigger_str(trigger),
            mode_to_char(g_mode));
    BspUart_SendString(buf);
}

static void report_experiment_stop(ExperimentTrigger_t trigger, uint32_t durationMs)
{
    char buf[96];

    sprintf(buf,
            "EVT:EXP_STOP,id=%lu,src=%s,mode=%c,dur=%lu\r\n",
            (unsigned long)g_experimentId,
            experiment_trigger_str(trigger),
            mode_to_char(g_mode),
            (unsigned long)durationMs);
    BspUart_SendString(buf);
}

static void transition_start(ExperimentTrigger_t trigger)
{
    if (is_running())
        return;

    if (experiment_host_sync_active())
        g_experimentId++;

    g_experimentActive = 1u;
    BNO085_ResetAttitude(&g_imu);
    memset(&g_encoder, 0, sizeof(g_encoder));
    Encoder_Reset();
    DualLoop_ResetAll(&g_pid);
    LineTrack_Stop();

    if (g_mode == MODE_STRAIGHT)
    {
        g_sysState = SYS_STRAIGHT;
        g_pid.speedRampTarget = SPEED_ENTRY;
    }
    else if (g_mode == MODE_TRACK)
    {
        g_sysState = SYS_TRACKING;
        g_pid.speedRampTarget = SPEED_ENTRY;
        LineTrack_Start(TRACK_DEFAULT_CROSSINGS);
    }
    else
    {
        g_sysState = SYS_SPINNING;
        g_pid.speedRampTarget = 0.0f;
    }

    g_pid.targetYaw = 0.0f;
    g_fastYawRate = 0.0f;
    g_runStartTick = g_tickMs;
    g_lastImuTick = g_tickMs;
    report_experiment_start(trigger);
    BspOled_ShowExperimentId(g_experimentId);
    MotorDriver_Enable();
    g_displayDirty = 1u;
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
    g_displayDirty = 1u;
}

static void transition_toggle_mode(void)
{
    if (is_running())
        return;

    if (g_mode == MODE_STRAIGHT)
        set_mode(MODE_TRACK);
    else if (g_mode == MODE_TRACK)
        set_mode(MODE_SPIN);
    else
        set_mode(MODE_STRAIGHT);
}

static void process_key_event(void)
{
    KeyEvent_t evt = BspKey_Read();

    if (evt == KEY_EVENT_NONE)
        return;

    if (evt == KEY_EVENT_SHORT_PRESS)
    {
        if (is_running())
            transition_stop(EXP_TRIGGER_KEY);
        else
            transition_start(EXP_TRIGGER_KEY);
    }
    else if (evt == KEY_EVENT_LONG_PRESS)
    {
        if (!is_running())
            transition_toggle_mode();
    }
}

static uint8_t cmd_parse_float(const char *s, float *out)
{
    if (sscanf(s, "%f", out) == 1)
        return 1u;

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

static void send_experiment_id_response(void)
{
    char buf[32];

    sprintf(buf, "OK:EXP=%lu\r\n", (unsigned long)g_experimentId);
    BspUart_SendString(buf);
}

static void sync_experiment_id(uint32_t experimentId)
{
    g_experimentId = experimentId;
    BspOled_ShowExperimentId(g_experimentId);
    g_displayDirty = 1u;
}

static uint8_t cmd_copy_token(const char *src, char *out, uint8_t outSize, const char **next)
{
    uint8_t len = 0u;

    if (src == 0 || out == 0 || outSize == 0u)
        return 0u;

    while (*src == ' ')
        src++;

    while (*src != '\0' && *src != ' ' && *src != '!' && len < (uint8_t)(outSize - 1u))
    {
        out[len++] = *src++;
    }
    out[len] = '\0';

    while (*src == ' ')
        src++;

    if (next != 0)
        *next = src;
    return (len > 0u) ? 1u : 0u;
}

static void handle_tcfg_command(const char *cmd)
{
    char key[40];
    char listBuf[512];
    const char *payload;
    const char *next;
    float fval;
    float applied;

    if (strcmp(cmd, "#TCFG PING!") == 0)
    {
        BspUart_SendString("OK:TCFG PING\r\n");
        return;
    }

    if (strcmp(cmd, "#TCFG LIST!") == 0)
    {
        LineTrack_ParamList(listBuf, (uint16_t)sizeof(listBuf));
        {
            char out[640];
            snprintf(out, sizeof(out), "OK:TCFG LIST track.speed_target,%s\r\n", listBuf);
            BspUart_SendString(out);
        }
        return;
    }

    if (strcmp(cmd, "#TCFG LOAD_DEFAULTS!") == 0)
    {
        g_pid.targetSpeed = PID_TRACK_SPEED_TARGET;
        LineTrack_SetPID(PID_TRACK_LINE_KP, PID_TRACK_LINE_KD);
        LineTrack_ResetRuntimeConfig();
        BspUart_SendString("OK:TCFG LOAD_DEFAULTS\r\n");
        return;
    }

    if (strncmp(cmd, "#TCFG GET ", 10) == 0)
    {
        char out[128];
        payload = cmd + 10;
        if (!cmd_copy_token(payload, key, (uint8_t)sizeof(key), 0))
        {
            BspUart_SendString("ERR:TCFG GET\r\n");
            return;
        }

        if (strcmp(key, "track.speed_target") == 0)
        {
            snprintf(out, sizeof(out), "OK:TCFG GET %s=%.3f\r\n", key, (double)g_pid.targetSpeed);
            BspUart_SendString(out);
            return;
        }

        if (LineTrack_ParamGet(key, &fval))
        {
            snprintf(out, sizeof(out), "OK:TCFG GET %s=%.3f\r\n", key, (double)fval);
            BspUart_SendString(out);
            return;
        }

        BspUart_SendString("ERR:TCFG GET\r\n");
        return;
    }

    if (strncmp(cmd, "#TCFG SET ", 10) == 0)
    {
        char out[144];
        payload = cmd + 10;
        if (!cmd_copy_token(payload, key, (uint8_t)sizeof(key), &next))
        {
            BspUart_SendString("ERR:TCFG SET\r\n");
            return;
        }
        if (!cmd_parse_float(next, &fval))
        {
            BspUart_SendString("ERR:TCFG SET\r\n");
            return;
        }

        if (strcmp(key, "track.speed_target") == 0)
        {
            g_pid.targetSpeed = fval;
            snprintf(out, sizeof(out), "OK:TCFG SET %s=%.3f\r\n", key, (double)g_pid.targetSpeed);
            BspUart_SendString(out);
            return;
        }

        if (LineTrack_ParamSet(key, fval, &applied))
        {
            snprintf(out, sizeof(out), "OK:TCFG SET %s=%.3f\r\n", key, (double)applied);
            BspUart_SendString(out);
            return;
        }

        BspUart_SendString("ERR:TCFG SET\r\n");
        return;
    }

    BspUart_SendString("ERR:TCFG\r\n");
}

static uint8_t handle_sensor_scale_command(const char *cmd)
{
    uint8_t sensorIndex;
    float fval;
    float applied;
    char key[24];
    char out[40];

    if (cmd == 0 || cmd[0] != '#' || cmd[1] != 'T' || cmd[2] != 'S')
        return 0u;
    if (cmd[3] < '1' || cmd[3] > '8')
        return 0u;

    sensorIndex = (uint8_t)(cmd[3] - '0');
    snprintf(key, sizeof(key), "track.sensor_scale%u", (unsigned)sensorIndex);

    if (strcmp(cmd + 4, "?!") == 0)
    {
        if (LineTrack_ParamGet(key, &fval))
        {
            int16_t milli = (int16_t)(fval * 1000.0f + 0.5f);
            snprintf(out, sizeof(out), "OK:TS%u=%d\r\n", (unsigned)sensorIndex, (int)milli);
            BspUart_SendString(out);
        }
        else
        {
            BspUart_SendString("ERR:TS\r\n");
        }
        return 1u;
    }

    if (cmd[4] == '=' && cmd_parse_float(cmd + 5, &fval))
    {
        if (LineTrack_ParamSet(key, fval, &applied))
        {
            int16_t milli = (int16_t)(applied * 1000.0f + 0.5f);
            snprintf(out, sizeof(out), "OK:TS%u=%d\r\n", (unsigned)sensorIndex, (int)milli);
            BspUart_SendString(out);
        }
        else
        {
            BspUart_SendString("ERR:TS\r\n");
        }
        return 1u;
    }

    return 0u;
}

static uint8_t handle_track_short_param_command(const char *cmd, const char *tag, const char *key)
{
    float fval;
    float applied;
    char out[40];
    size_t tagLen;

    if (cmd == 0 || tag == 0 || key == 0)
        return 0u;

    tagLen = strlen(tag);
    if (strncmp(cmd, tag, tagLen) != 0)
        return 0u;

    if (strcmp(cmd + tagLen, "?!") == 0)
    {
        if (LineTrack_ParamGet(key, &fval))
        {
            snprintf(out, sizeof(out), "OK:%s=%.3f\r\n", tag + 1, (double)fval);
            BspUart_SendString(out);
        }
        else
        {
            snprintf(out, sizeof(out), "ERR:%s\r\n", tag + 1);
            BspUart_SendString(out);
        }
        return 1u;
    }

    if (cmd[tagLen] == '=' && cmd_parse_float(cmd + tagLen + 1u, &fval))
    {
        if (LineTrack_ParamSet(key, fval, &applied))
        {
            snprintf(out, sizeof(out), "OK:%s=%.3f\r\n", tag + 1, (double)applied);
            BspUart_SendString(out);
        }
        else
        {
            snprintf(out, sizeof(out), "ERR:%s\r\n", tag + 1);
            BspUart_SendString(out);
        }
        return 1u;
    }

    return 0u;
}

static void handle_command(const char *cmd)
{
    float fval;
    uint32_t u32val;

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
    if (strcmp(cmd, "#MODE=STRAIGHT!") == 0)
    {
        if (!is_running())
        {
            set_mode(MODE_STRAIGHT);
            BspUart_SendString("OK:MODE=STRAIGHT\r\n");
        }
        return;
    }
    if (strcmp(cmd, "#MODE=TRACK!") == 0)
    {
        if (!is_running())
        {
            set_mode(MODE_TRACK);
            BspUart_SendString("OK:MODE=TRACK\r\n");
        }
        return;
    }
    if (strcmp(cmd, "#MODE=SPIN!") == 0)
    {
        if (!is_running())
        {
            set_mode(MODE_SPIN);
            BspUart_SendString("OK:MODE=SPIN\r\n");
        }
        return;
    }
    if (handle_sensor_scale_command(cmd))
    {
        return;
    }
    if (handle_track_short_param_command(cmd, "#CSR", "track.center_small_ratio")) return;
    if (handle_track_short_param_command(cmd, "#CSM", "track.center_small_min")) return;
    if (handle_track_short_param_command(cmd, "#CMR", "track.center_mid_ratio")) return;
    if (handle_track_short_param_command(cmd, "#CMM", "track.center_mid_min")) return;
    if (handle_track_short_param_command(cmd, "#EDR", "track.edge_ratio")) return;
    if (handle_track_short_param_command(cmd, "#EDM", "track.edge_min")) return;
    if (handle_track_short_param_command(cmd, "#RCD", "track.recenter_decay")) return;
    if (handle_track_short_param_command(cmd, "#CDB", "track.center_deadband")) return;
    if (handle_track_short_param_command(cmd, "#PLF", "track.pos_lpf")) return;
    if (handle_track_short_param_command(cmd, "#DLF", "track.d_lpf")) return;
    if (handle_track_short_param_command(cmd, "#OCB", "track.offcenter_boost")) return;
    if (handle_track_short_param_command(cmd, "#CHT", "track.center_hold_ticks")) return;
    if (handle_track_short_param_command(cmd, "#RCT", "track.recover_ticks")) return;
    if (handle_track_short_param_command(cmd, "#STF", "track.search_turn_fast")) return;
    if (handle_track_short_param_command(cmd, "#STS", "track.search_turn_slow")) return;
    if (handle_track_short_param_command(cmd, "#STO", "track.search_timeout")) return;
    if (strncmp(cmd, "#TCFG ", 6) == 0)
    {
        handle_tcfg_command(cmd);
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
            LineTrack_SetPID(fval, g_lineTrack.kd);
            BspUart_SendString("OK:LKP\r\n");
        }
        return;
    }
    if (strncmp(cmd, "#LKD=", 5) == 0)
    {
        if (cmd_parse_float(cmd + 5, &fval))
        {
            LineTrack_SetPID(g_lineTrack.kp, fval);
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

static void run_control(uint32_t now)
{
    float dt;
    float avgSpeed;
    float dtNorm;

    dt = (float)(now - g_lastControlTick) * 0.001f;
    if (dt <= 0.0f)
        dt = 0.01f;
    if (dt > 0.5f)
        dt = 0.5f;
    g_lastControlTick = now;

    Encoder_Update(&g_encoder);

    dtNorm = dt * 100.0f;
    if (dtNorm < 0.5f)
        dtNorm = 0.5f;

    {
        float nl = (float)g_encoder.leftSpeed / dtNorm;
        float nr = (float)g_encoder.rightSpeed / dtNorm;
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
        g_pid.leftPWM = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();
    }
    else if (g_sysState == SYS_TRACKING)
    {
        avgSpeed = (g_encoder.filteredLeftSpeed + g_encoder.filteredRightSpeed) * 0.5f;
        DualLoop_ComputeSpeed(&g_pid, avgSpeed, dt);
        LineTrack_Update(now, g_pid.pwmCore, g_imu.yaw);

        if (g_lineTrack.cornerDone)
        {
            float resumeSpeedTarget;

            g_lineTrack.cornerDone = 0u;
            PID_Reset(&g_pid.speedPID);
            resumeSpeedTarget = g_pid.currentSpeed;
            if (resumeSpeedTarget < SPEED_ENTRY)
                resumeSpeedTarget = SPEED_ENTRY;
            if (resumeSpeedTarget > TRACK_RESUME_SPEED_MAX)
                resumeSpeedTarget = TRACK_RESUME_SPEED_MAX;
            if (resumeSpeedTarget > g_pid.targetSpeed)
                resumeSpeedTarget = g_pid.targetSpeed;
            g_pid.speedRampTarget = resumeSpeedTarget;
        }

        g_pid.leftPWM = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();

        if (!LineTrack_IsRunning())
            transition_stop(EXP_TRIGGER_AUTO);
    }
    else if (g_sysState == SYS_SPINNING)
    {
        g_pid.pwmCore = 0;
        g_pid.headingDiffPWM = 0;

#if SPIN_PLACEHOLDER_PWM > 0
        MotorDriver_Enable();
        MotorDriver_SetTurnPWM(SPIN_PLACEHOLDER_PWM, (int16_t)(-SPIN_PLACEHOLDER_PWM));
#else
        MotorDriver_Enable();
        MotorDriver_SetTurnPWM(0, 0);
#endif
        g_pid.leftPWM = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();
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
    g_fastYawRate += 0.5f * (-g_imu.gyroZf - g_fastYawRate);
    g_lastImuTick = g_tickMs;
}

static void send_telemetry(void)
{
    uint32_t t = g_tickMs - g_runStartTick;
    uint8_t run = is_running();

    if (g_sysState == SYS_TRACKING)
    {
        BspUart_SendTelemetryTrack(t, g_experimentId, run,
                                   g_encoder.leftSpeed, g_encoder.rightSpeed,
                                   g_imu.yaw, g_imu.yawRate,
                                   g_pid.pwmCore, g_lineTrack.devSpeed,
                                   g_pid.leftPWM, g_pid.rightPWM,
                                   g_lineTrack.sensorData, g_lineTrack.linePos,
                                   g_lineTrack.bearingDev, g_lineTrack.crossCount,
                                   g_lineTrack.dbgTrackState, g_lineTrack.dbgTurnDir,
                                   g_lineTrack.dbgCrossActive,
                                   g_lineTrack.dbgTelemState, g_lineTrack.dbgTelemFlags,
                                   g_lineTrack.gainStage, g_lineTrack.searchPhase,
                                   g_lineTrack.recoverTicks);
    }
    else if (g_sysState == SYS_SPINNING)
    {
        BspUart_SendTelemetrySpin(t, g_experimentId, run,
                                  g_encoder.leftSpeed, g_encoder.rightSpeed,
                                  g_imu.yaw, g_fastYawRate,
                                  g_pid.leftPWM, g_pid.rightPWM);
    }
    else
    {
        BspUart_SendTelemetryStraight(t, g_experimentId, run,
                                      g_encoder.leftSpeed, g_encoder.rightSpeed,
                                      g_imu.yaw, g_fastYawRate,
                                      g_pid.pwmCore, g_pid.headingDiffPWM,
                                      g_pid.dTermPostDZ,
                                      g_pid.leftPWM, g_pid.rightPWM,
                                      g_pid.lastHi);
    }
}

static void update_display(uint32_t now)
{
    if (is_running())
        return;

    if (!g_displayDirty && (now - g_lastDisplayTick) < 200u)
        return;

    g_lastDisplayTick = now;
    g_displayDirty = 0u;

    BspOled_ShowStatus(g_sysState, g_mode,
                       g_imu.yaw,
                       (float)g_encoder.leftSpeed,
                       (float)g_encoder.rightSpeed,
                       g_experimentId);
}

int main(void)
{
    char cmdBuf[64];
    uint32_t now;
    uint32_t telemetryPeriod;

    BspOled_Init();
    BspKey_Init();
    BspUart_Init();
    BspOled_ShowFaultCode(FaultTrace_GetAndClearCode());

    Timer_Init();
    BspOled_ShowIMUInit(BNO085_GetInitStage(), BNO085_GetI2CAddr());

    MotorDriver_Init();
    Encoder_Init();
    BNO085_Init();
    BspOled_ShowIMUInit(BNO085_GetInitStage(), BNO085_GetI2CAddr());
    LineSensor_Init();

    DualLoop_Init(&g_pid);
    LineTrack_Init();
    memset(&g_encoder, 0, sizeof(g_encoder));
    memset(&g_imu, 0, sizeof(g_imu));

    g_displayDirty = 1u;

    while (1)
    {
        now = g_tickMs;

        if (BspUart_TakeCommand(cmdBuf, sizeof(cmdBuf)))
            handle_command(cmdBuf);

        if ((now - g_lastImuTick) >= IMU_READ_PERIOD_MS)
            run_imu_update();

        if (g_controlFlag)
        {
            g_controlFlag = 0u;
            run_control(now);
        }

        telemetryPeriod = is_running() ? TELEMETRY_PERIOD_MS : TELEMETRY_IDLE_PERIOD_MS;
        if ((now - g_lastTelemetryTick) >= telemetryPeriod)
        {
            g_lastTelemetryTick = now;
            send_telemetry();
        }

        update_display(now);
        process_key_event();
    }
}
