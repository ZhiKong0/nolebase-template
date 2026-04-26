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

typedef enum
{
    EXP_TRIGGER_AUTO = 0,
    EXP_TRIGGER_KEY,
    EXP_TRIGGER_UART
} ExperimentTrigger_t;

typedef struct
{
    uint8_t active;
    uint8_t dir;
    uint32_t startTick;
    int32_t startLeftCount;
    int32_t startRightCount;
} TurnbackState_t;

static SystemState_t g_sysState = SYS_STOP;
static ControlMode_t g_mode = MODE_TRACK;
static DualLoopState_t g_pid;
static Encoder_Data_t g_encoder;
static IMU_Data_t g_imu;
static TurnbackState_t g_turnback;

static volatile uint32_t g_tickMs = 0u;
static volatile uint8_t g_controlFlag = 0u;

static uint32_t g_runStartTick = 0u;
static uint32_t g_lastTelemetryTick = 0u;
static uint32_t g_lastControlTick = 0u;
static uint32_t g_lastImuTick = 0u;
static uint32_t g_lastDisplayTick = 0u;
static uint32_t g_experimentId = 0u;
static uint32_t g_experimentHostSyncUntilTick = 0u;
static uint32_t g_stopAlertUntilTick = 0u;
static uint32_t g_buzzerUntilTick = 0u;

static uint8_t g_experimentActive = 0u;
static uint8_t g_displayDirty = 1u;
static uint8_t g_stopAlertActive = 0u;
static float g_fastYawRate = 0.0f;

static void buzzer_set(uint8_t on)
{
#if BUZZER_ACTIVE_HIGH
    GPIO_WriteBit(BUZZER_PORT, BUZZER_PIN, on ? Bit_SET : Bit_RESET);
#else
    GPIO_WriteBit(BUZZER_PORT, BUZZER_PIN, on ? Bit_RESET : Bit_SET);
#endif
}

static void Buzzer_Init(void)
{
    GPIO_InitTypeDef g;

    RCC_APB2PeriphClockCmd(BUZZER_GPIO_RCC, ENABLE);
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Pin = BUZZER_PIN;
    GPIO_Init(BUZZER_PORT, &g);
    buzzer_set(0u);
}

static void stop_alert_start(void)
{
    g_buzzerUntilTick = g_tickMs + STOP_ALERT_BEEP_MS;
    g_stopAlertUntilTick = g_tickMs + STOP_ALERT_OLED_MS;
    g_stopAlertActive = 1u;
    buzzer_set(1u);
    BspOled_SetFullOn(1u);
    g_displayDirty = 1u;
}

static void stop_alert_cancel(void)
{
    buzzer_set(0u);
    g_buzzerUntilTick = 0u;
    g_stopAlertUntilTick = 0u;
    if (g_stopAlertActive) {
        BspOled_SetFullOn(0u);
    }
    g_stopAlertActive = 0u;
    g_displayDirty = 1u;
}

static void stop_alert_update(uint32_t now)
{
    if (g_buzzerUntilTick != 0u && now >= g_buzzerUntilTick) {
        buzzer_set(0u);
        g_buzzerUntilTick = 0u;
    }

    if (g_stopAlertActive && now >= g_stopAlertUntilTick) {
        BspOled_SetFullOn(0u);
        g_stopAlertActive = 0u;
        g_stopAlertUntilTick = 0u;
        g_displayDirty = 1u;
    }
}

static void Timer_Init(void)
{
    TIM_TimeBaseInitTypeDef tb;
    NVIC_InitTypeDef n;

    RCC_APB1PeriphClockCmd(SYS_TIM_RCC, ENABLE);

    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = SYS_TIM_PERIOD - 1u;
    tb.TIM_Prescaler = SYS_TIM_PRESCALER - 1u;
    tb.TIM_RepetitionCounter = 0u;
    TIM_TimeBaseInit(SYS_TIM, &tb);

    TIM_ITConfig(SYS_TIM, TIM_IT_Update, ENABLE);
    TIM_ClearITPendingBit(SYS_TIM, TIM_IT_Update);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    n.NVIC_IRQChannel = SYS_TIM_IRQn;
    n.NVIC_IRQChannelCmd = ENABLE;
    n.NVIC_IRQChannelPreemptionPriority = 2u;
    n.NVIC_IRQChannelSubPriority = 0u;
    NVIC_Init(&n);

    g_tickMs = 0u;
    TIM_Cmd(SYS_TIM, ENABLE);
}

void Main_TimerTickISR(void)
{
    static uint8_t divider = 0u;

    g_tickMs++;
    divider++;
    if (divider >= CONTROL_PERIOD_MS) {
        divider = 0u;
        g_controlFlag = 1u;
    }

    BspKey_Tick(g_tickMs);
}

static uint8_t is_running(void)
{
    return (g_sysState != SYS_STOP) ? 1u : 0u;
}

static uint8_t turnback_is_active(void)
{
    return g_turnback.active;
}

static uint8_t experiment_host_sync_active(void)
{
    return (g_experimentHostSyncUntilTick != 0u &&
            g_tickMs < g_experimentHostSyncUntilTick) ? 1u : 0u;
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

static const char *experiment_trigger_str(ExperimentTrigger_t trigger)
{
    switch (trigger)
    {
    case EXP_TRIGGER_KEY:  return "KEY";
    case EXP_TRIGGER_UART: return "UART";
    default:               return "AUTO";
    }
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

static void load_mode_defaults(ControlMode_t mode)
{
    if (mode == MODE_STRAIGHT) {
        DualLoop_LoadStraightDefaults(&g_pid);
    } else if (mode == MODE_TRACK) {
        DualLoop_LoadTrackDefaults(&g_pid);
    } else {
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

static void transition_toggle_mode(void)
{
    if (g_mode == MODE_STRAIGHT) {
        set_mode(MODE_TRACK);
    } else if (g_mode == MODE_TRACK) {
        set_mode(MODE_SPIN);
    } else {
        set_mode(MODE_STRAIGHT);
    }
}

static void sync_experiment_id(uint32_t experimentId)
{
    g_experimentId = experimentId;
    g_displayDirty = 1u;
    if (BNO085_IsReady()) {
        BspOled_ShowExperimentId(g_experimentId);
    }
}

static void send_experiment_id_response(void)
{
    char buf[40];

    sprintf(buf, "OK:EXP=%lu\r\n", (unsigned long)g_experimentId);
    BspUart_SendString(buf);
}

static void transition_start(ExperimentTrigger_t trigger)
{
    if (is_running() || turnback_is_active()) {
        return;
    }

    if (experiment_host_sync_active()) {
        g_experimentId++;
    }

    stop_alert_cancel();
    g_experimentActive = 1u;
    g_runStartTick = g_tickMs;
    memset(&g_encoder, 0, sizeof(g_encoder));
    Encoder_Reset();
    DualLoop_ResetAll(&g_pid);
    LineTrack_Stop();

#if IMU_ENABLE
    BNO085_ResetAttitude(&g_imu);
#endif

    if (g_mode == MODE_STRAIGHT) {
        g_sysState = SYS_STRAIGHT;
    } else if (g_mode == MODE_TRACK) {
        g_sysState = SYS_TRACKING;
        LineTrack_Start(TRACK_DEFAULT_CROSSINGS);
    } else {
        g_sysState = SYS_SPINNING;
    }

    g_pid.targetYaw = 0.0f;
    g_pid.speedRampTarget = SPEED_ENTRY;
    g_fastYawRate = 0.0f;
    report_experiment_start(trigger);
    MotorDriver_Enable();
    g_displayDirty = 1u;
}

static void transition_stop(ExperimentTrigger_t trigger)
{
    uint32_t durationMs = 0u;

    if (g_experimentActive) {
        durationMs = g_tickMs - g_runStartTick;
        report_experiment_stop(trigger, durationMs);
    }

    MotorDriver_Stop();
    MotorDriver_Disable();
    LineTrack_Stop();
    DualLoop_ResetAll(&g_pid);
    g_sysState = SYS_STOP;
    g_experimentActive = 0u;
    stop_alert_start();
    g_displayDirty = 1u;
}

static void transition_finish_hold(ExperimentTrigger_t trigger)
{
    uint32_t durationMs = 0u;

    if (g_experimentActive) {
        durationMs = g_tickMs - g_runStartTick;
        report_experiment_stop(trigger, durationMs);
    }

    g_pid.targetSpeed = 0.0f;
    g_pid.speedRampTarget = 0.0f;
    g_pid.currentSpeed = 0.0f;
    g_pid.pwmCore = 0;
    g_pid.headingDiffPWM = 0;
    g_pid.leftPWM = 0;
    g_pid.rightPWM = 0;
    PID_Reset(&g_pid.speedPID);
    PID_Reset(&g_pid.headingPID);
    LineTrack_Stop();
    g_sysState = SYS_STOP;
    g_experimentActive = 0u;
    stop_alert_start();
    g_displayDirty = 1u;
    MotorDriver_ActiveBrake();
}

static void turnback_clear(void)
{
    memset(&g_turnback, 0, sizeof(g_turnback));
}

static void turnback_abort_silent(void)
{
    if (!g_turnback.active) {
        return;
    }

    MotorDriver_Stop();
    turnback_clear();
}

static int32_t turnback_distance_counts(void)
{
    int32_t leftDelta = g_encoder.leftCount - g_turnback.startLeftCount;
    int32_t rightDelta = g_encoder.rightCount - g_turnback.startRightCount;

    if (leftDelta < 0) {
        leftDelta = -leftDelta;
    }
    if (rightDelta < 0) {
        rightDelta = -rightDelta;
    }

    return (leftDelta + rightDelta) / 2;
}

static void turnback_start(void)
{
    if (is_running()) {
        BspUart_SendString("ERR:TURNBACK=RUNNING\r\n");
        return;
    }
    if (turnback_is_active()) {
        BspUart_SendString("ERR:TURNBACK=BUSY\r\n");
        return;
    }

    MotorDriver_Enable();
    memset(&g_encoder, 0, sizeof(g_encoder));
    Encoder_Reset();

    g_turnback.active = 1u;
    g_turnback.dir = (g_lineTrack.lastTurnDir == LT_DIR_RIGHT) ? LT_DIR_RIGHT : LT_DIR_LEFT;
    g_turnback.startTick = g_tickMs;
    g_turnback.startLeftCount = 0;
    g_turnback.startRightCount = 0;

    BspUart_SendString("OK:TURNBACK\r\n");
    BspUart_SendString("EVT:TURNBACK,START\r\n");
}

static void run_turnback(uint32_t now)
{
    int32_t distance;

    if (!g_turnback.active) {
        return;
    }

    distance = turnback_distance_counts();
    if ((now - g_turnback.startTick) > TURNBACK_TIMEOUT_MS ||
        distance >= TURNBACK_TARGET_COUNTS) {
        MotorDriver_Stop();
        BspUart_SendString("EVT:TURNBACK,END\r\n");
        turnback_clear();
        return;
    }

    if (g_turnback.dir == LT_DIR_RIGHT) {
        MotorDriver_SetTurnPWM(TURNBACK_PWM_FAST, -TURNBACK_PWM_SLOW);
    } else {
        MotorDriver_SetTurnPWM(-TURNBACK_PWM_SLOW, TURNBACK_PWM_FAST);
    }
}

static void process_key_event(void)
{
    KeyEvent_t evt = BspKey_Read();

    if (evt == KEY_EVENT_NONE) {
        return;
    }

    if (evt == KEY_EVENT_SHORT_PRESS) {
        if (turnback_is_active()) {
            turnback_abort_silent();
        } else if (is_running()) {
            transition_stop(EXP_TRIGGER_KEY);
        } else {
            transition_start(EXP_TRIGGER_KEY);
        }
    } else if (evt == KEY_EVENT_LONG_PRESS) {
        if (!is_running() && !turnback_is_active()) {
            transition_toggle_mode();
        }
    }
}

static uint8_t cmd_parse_float(const char *s, float *out)
{
    const char *p = s;
    float value = 0.0f;
    float scale = 0.1f;
    uint8_t hasDigit = 0u;
    uint8_t neg = 0u;

    if (!s || !out) {
        return 0u;
    }

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (*p == '-' || *p == '+') {
        neg = (*p == '-') ? 1u : 0u;
        p++;
    }

    while (*p >= '0' && *p <= '9') {
        value = value * 10.0f + (float)(*p - '0');
        hasDigit = 1u;
        p++;
    }

    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            value += (float)(*p - '0') * scale;
            scale *= 0.1f;
            hasDigit = 1u;
            p++;
        }
    }

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (!hasDigit) {
        return 0u;
    }
    if (*p != '\0' && *p != '!' && *p != '\r' && *p != '\n') {
        return 0u;
    }

    *out = neg ? -value : value;
    return 1u;
}

static uint8_t cmd_parse_u32(const char *s, uint32_t *out)
{
    uint32_t value = 0u;
    uint8_t digits = 0u;
    const char *p = s;

    if (!s || !out) {
        return 0u;
    }

    if (*p == '+') {
        p++;
    }

    while (*p >= '0' && *p <= '9') {
        value = value * 10u + (uint32_t)(*p - '0');
        digits = 1u;
        p++;
    }

    if (!digits) {
        return 0u;
    }
    if (*p != '\0' && *p != '!') {
        return 0u;
    }

    *out = value;
    return 1u;
}

static uint8_t cmd_copy_token(const char *src, char *out, uint8_t outSize, const char **next)
{
    uint8_t len = 0u;

    if (!src || !out || outSize == 0u) {
        return 0u;
    }

    while (*src == ' ') {
        src++;
    }

    while (*src != '\0' && *src != ' ' && *src != '!' && len < (uint8_t)(outSize - 1u)) {
        out[len++] = *src++;
    }
    out[len] = '\0';

    while (*src == ' ') {
        src++;
    }

    if (next) {
        *next = src;
    }
    return (len > 0u) ? 1u : 0u;
}

static void handle_tcfg_command(const char *cmd)
{
    char key[40];
    char listBuf[640];
    char out[160];
    const char *payload;
    const char *next;
    float fval;
    float applied;

    if (strcmp(cmd, "#TCFG PING!") == 0) {
        BspUart_SendString("OK:TCFG PING\r\n");
        return;
    }

    if (strcmp(cmd, "#TCFG LIST!") == 0) {
        LineTrack_ParamList(listBuf, (uint16_t)sizeof(listBuf));
        snprintf(out, sizeof(out), "OK:TCFG LIST track.speed_target,%s\r\n", listBuf);
        BspUart_SendString(out);
        return;
    }

    if (strcmp(cmd, "#TCFG LOAD_DEFAULTS!") == 0) {
        load_mode_defaults(g_mode);
        LineTrack_SetPID(PID_TRACK_LINE_KP, PID_TRACK_LINE_KD);
        LineTrack_ResetRuntimeConfig();
        BspUart_SendString("OK:TCFG LOAD_DEFAULTS\r\n");
        return;
    }

    if (strncmp(cmd, "#TCFG GET ", 10) == 0) {
        payload = cmd + 10;
        if (!cmd_copy_token(payload, key, (uint8_t)sizeof(key), 0)) {
            BspUart_SendString("ERR:TCFG GET\r\n");
            return;
        }

        if (strcmp(key, "track.speed_target") == 0) {
            snprintf(out, sizeof(out), "OK:TCFG GET %s=%.3f\r\n", key, (double)g_pid.targetSpeed);
            BspUart_SendString(out);
            return;
        }

        if (LineTrack_ParamGet(key, &fval)) {
            snprintf(out, sizeof(out), "OK:TCFG GET %s=%.3f\r\n", key, (double)fval);
            BspUart_SendString(out);
            return;
        }

        BspUart_SendString("ERR:TCFG GET\r\n");
        return;
    }

    if (strncmp(cmd, "#TCFG SET ", 10) == 0) {
        payload = cmd + 10;
        if (!cmd_copy_token(payload, key, (uint8_t)sizeof(key), &next) || !cmd_parse_float(next, &fval)) {
            BspUart_SendString("ERR:TCFG SET\r\n");
            return;
        }

        if (strcmp(key, "track.speed_target") == 0) {
            g_pid.targetSpeed = fval;
            snprintf(out, sizeof(out), "OK:TCFG SET %s=%.3f\r\n", key, (double)g_pid.targetSpeed);
            BspUart_SendString(out);
            return;
        }

        if (LineTrack_ParamSet(key, fval, &applied)) {
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
    char key[32];
    char out[48];
    const char *p;

    if (!cmd || strncmp(cmd, "#TS", 3) != 0) {
        return 0u;
    }

    p = cmd + 3;
    if (*p < '1' || *p > '9') {
        return 0u;
    }

    sensorIndex = 0u;
    while (*p >= '0' && *p <= '9') {
        sensorIndex = (uint8_t)(sensorIndex * 10u + (uint8_t)(*p - '0'));
        p++;
    }

    if (sensorIndex == 0u || sensorIndex > LINE_SENSOR_COUNT) {
        return 0u;
    }

    snprintf(key, sizeof(key), "track.sensor_scale%u", (unsigned)sensorIndex);

    if (strcmp(p, "?!") == 0) {
        if (LineTrack_ParamGet(key, &fval)) {
            int16_t milli = (int16_t)(fval * 1000.0f + 0.5f);
            snprintf(out, sizeof(out), "OK:TS%u=%d\r\n", (unsigned)sensorIndex, (int)milli);
            BspUart_SendString(out);
        } else {
            BspUart_SendString("ERR:TS\r\n");
        }
        return 1u;
    }

    if (*p == '=' && cmd_parse_float(p + 1, &fval)) {
        if (fval > 10.0f || fval < -10.0f) {
            fval *= 0.001f;
        }
        if (LineTrack_ParamSet(key, fval, &applied)) {
            int16_t milli = (int16_t)(applied * 1000.0f + 0.5f);
            snprintf(out, sizeof(out), "OK:TS%u=%d\r\n", (unsigned)sensorIndex, (int)milli);
            BspUart_SendString(out);
        } else {
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
    char out[48];
    size_t tagLen;

    if (!cmd || !tag || !key) {
        return 0u;
    }

    tagLen = strlen(tag);
    if (strncmp(cmd, tag, tagLen) != 0) {
        return 0u;
    }

    if (strcmp(cmd + tagLen, "?!") == 0) {
        if (LineTrack_ParamGet(key, &fval)) {
            snprintf(out, sizeof(out), "OK:%s=%.3f\r\n", tag + 1, (double)fval);
            BspUart_SendString(out);
        } else {
            snprintf(out, sizeof(out), "ERR:%s\r\n", tag + 1);
            BspUart_SendString(out);
        }
        return 1u;
    }

    if (cmd[tagLen] == '=' && cmd_parse_float(cmd + tagLen + 1u, &fval)) {
        if (LineTrack_ParamSet(key, fval, &applied)) {
            snprintf(out, sizeof(out), "OK:%s=%.3f\r\n", tag + 1, (double)applied);
            BspUart_SendString(out);
        } else {
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
    char out[120];

    if (strcmp(cmd, "#RUN!") == 0) {
        transition_start(EXP_TRIGGER_UART);
        BspUart_SendString("OK:RUN\r\n");
        return;
    }

    if (strcmp(cmd, "#STOP!") == 0) {
        turnback_abort_silent();
        transition_stop(EXP_TRIGGER_UART);
        BspUart_SendString("OK:STOP\r\n");
        return;
    }

    if (strcmp(cmd, "#TURNBACK!") == 0) {
        turnback_start();
        return;
    }

    if (strcmp(cmd, "#EXP?!") == 0) {
        send_experiment_id_response();
        return;
    }

    if (strcmp(cmd, "#EXPHOST=OFF!") == 0) {
        g_experimentHostSyncUntilTick = 0u;
        BspUart_SendString("OK:EXPHOST=OFF\r\n");
        return;
    }

    if (strncmp(cmd, "#EXPHOST=", 9) == 0) {
        if (cmd_parse_u32(cmd + 9, &u32val)) {
            g_experimentHostSyncUntilTick = g_tickMs + EXP_HOST_SYNC_TIMEOUT_MS;
            if (!is_running()) {
                sync_experiment_id(u32val);
            }
        }
        return;
    }

    if (strncmp(cmd, "#EXP=", 5) == 0) {
        if (!is_running() && cmd_parse_u32(cmd + 5, &u32val)) {
            sync_experiment_id(u32val);
            send_experiment_id_response();
        } else if (is_running()) {
            BspUart_SendString("ERR:EXP=RUNNING\r\n");
        }
        return;
    }

    if (strcmp(cmd, "#MODE=STRAIGHT!") == 0) {
        if (!is_running() && !turnback_is_active()) {
            set_mode(MODE_STRAIGHT);
            BspUart_SendString("OK:MODE=STRAIGHT\r\n");
        }
        return;
    }

    if (strcmp(cmd, "#MODE=TRACK!") == 0) {
        if (!is_running() && !turnback_is_active()) {
            set_mode(MODE_TRACK);
            BspUart_SendString("OK:MODE=TRACK\r\n");
        }
        return;
    }

    if (strcmp(cmd, "#MODE=SPIN!") == 0) {
        if (!is_running() && !turnback_is_active()) {
            set_mode(MODE_SPIN);
            BspUart_SendString("OK:MODE=SPIN\r\n");
        }
        return;
    }

    if (handle_sensor_scale_command(cmd)) return;
    if (handle_track_short_param_command(cmd, "#TDR", "track.dev_ratio")) return;
    if (handle_track_short_param_command(cmd, "#STB", "track.static_bias")) return;
    if (handle_track_short_param_command(cmd, "#TDB", "track.deadband")) return;
    if (handle_track_short_param_command(cmd, "#PLF", "track.pos_lpf")) return;
    if (handle_track_short_param_command(cmd, "#DLF", "track.d_lpf")) return;
    if (handle_track_short_param_command(cmd, "#RCT", "track.recover_ticks")) return;
    if (handle_track_short_param_command(cmd, "#STF", "track.search_turn_fast")) return;
    if (handle_track_short_param_command(cmd, "#STS", "track.search_turn_slow")) return;
    if (handle_track_short_param_command(cmd, "#STO", "track.search_timeout")) return;

    if (strncmp(cmd, "#TCFG ", 6) == 0) {
        handle_tcfg_command(cmd);
        return;
    }

    if (strncmp(cmd, "#SPD=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            g_pid.targetSpeed = fval;
            snprintf(out, sizeof(out), "OK:SPD=%.3f\r\n", (double)g_pid.targetSpeed);
            BspUart_SendString(out);
        }
        return;
    }

    if (strcmp(cmd, "#SPD?!") == 0) {
        snprintf(out, sizeof(out), "OK:SPD=%.3f\r\n", (double)g_pid.targetSpeed);
        BspUart_SendString(out);
        return;
    }

    if (strncmp(cmd, "#SKP=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            g_pid.speedPID.kp = fval;
            BspUart_SendString("OK:SKP\r\n");
        }
        return;
    }

    if (strncmp(cmd, "#SKI=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            g_pid.speedPID.ki = fval;
            BspUart_SendString("OK:SKI\r\n");
        }
        return;
    }

    if (strncmp(cmd, "#SKD=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            g_pid.speedPID.kd = fval;
            BspUart_SendString("OK:SKD\r\n");
        }
        return;
    }

    if (strncmp(cmd, "#AKP=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            g_pid.headingPID.kp = fval;
            BspUart_SendString("OK:AKP\r\n");
        }
        return;
    }

    if (strncmp(cmd, "#AKI=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            g_pid.headingPID.ki = fval;
            BspUart_SendString("OK:AKI\r\n");
        }
        return;
    }

    if (strncmp(cmd, "#AKD=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            g_pid.headingPID.kd = fval;
            BspUart_SendString("OK:AKD\r\n");
        }
        return;
    }

    if (strncmp(cmd, "#LKP=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            LineTrack_SetPID(fval, g_lineTrack.kd);
            BspUart_SendString("OK:LKP\r\n");
        }
        return;
    }

    if (strcmp(cmd, "#LKP?!") == 0) {
        snprintf(out, sizeof(out), "OK:LKP=%.3f\r\n", (double)g_lineTrack.kp);
        BspUart_SendString(out);
        return;
    }

    if (strncmp(cmd, "#LKD=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            LineTrack_SetPID(g_lineTrack.kp, fval);
            BspUart_SendString("OK:LKD\r\n");
        }
        return;
    }

    if (strcmp(cmd, "#LKD?!") == 0) {
        snprintf(out, sizeof(out), "OK:LKD=%.3f\r\n", (double)g_lineTrack.kd);
        BspUart_SendString(out);
        return;
    }

    if (strncmp(cmd, "#SFF=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            g_pid.feedforwardGain = fval;
            BspUart_SendString("OK:SFF\r\n");
        }
        return;
    }

    if (strncmp(cmd, "#HTR=", 5) == 0) {
        if (cmd_parse_float(cmd + 5, &fval)) {
            g_pid.headingTrim = fval;
            BspUart_SendString("OK:HTR\r\n");
        }
        return;
    }

    if (strcmp(cmd, "#STAT!") == 0) {
        BspUart_SendStat(g_sysState, g_mode,
                         g_pid.speedPID.kp, g_pid.speedPID.ki, g_pid.speedPID.kd,
                         g_pid.headingPID.kp, g_pid.headingPID.ki, g_pid.headingPID.kd,
                         g_lineTrack.kp, 0.0f, g_lineTrack.kd,
                         g_pid.targetSpeed);
        return;
    }

    if (strcmp(cmd, "#IMU?!") == 0) {
        snprintf(out, sizeof(out),
                 "OK:IMU=%u,addr=%02X,fail=%u,ready=%u,rx=%u,ch=%u,rid=%u,len=%u\r\n",
                 (unsigned)BNO085_GetInitStage(),
                 (unsigned)BNO085_GetI2CAddr(),
                 (unsigned)BNO085_GetInitFailCode(),
                 (unsigned)BNO085_IsReady(),
                 (unsigned)BNO085_GetLastRxFailCode(),
                 (unsigned)BNO085_GetLastChannel(),
                 (unsigned)BNO085_GetLastReportId(),
                 (unsigned)BNO085_GetLastPayloadLen());
        BspUart_SendString(out);
        return;
    }

    if (strcmp(cmd, "#CAL!") == 0) {
        BspUart_SendString("OK:CAL\r\n");
        return;
    }

    BspUart_SendString("ERR\r\n");
}

static void run_imu_update(void)
{
#if !IMU_ENABLE
    memset(&g_imu, 0, sizeof(g_imu));
    g_fastYawRate = 0.0f;
    g_lastImuTick = g_tickMs;
#else
    float dt = (float)(g_tickMs - g_lastImuTick) * 0.001f;

    if (dt <= 0.0f || dt > 0.5f) {
        dt = 0.02f;
    }

    BNO085_ReadAll(&g_imu);
    BNO085_UpdateYaw(&g_imu, dt);
    g_fastYawRate += 0.5f * (-g_imu.gyroZf - g_fastYawRate);
    g_lastImuTick = g_tickMs;
#endif
}

static void update_filtered_speed(float dt)
{
    float dtNorm = dt * 100.0f;
    float leftNow;
    float rightNow;

    if (dtNorm < 0.5f) {
        dtNorm = 0.5f;
    }

    leftNow = (float)g_encoder.leftSpeed / dtNorm;
    rightNow = (float)g_encoder.rightSpeed / dtNorm;

    g_encoder.filteredLeftSpeed =
        ENC_SPEED_LPF_ALPHA * leftNow +
        (1.0f - ENC_SPEED_LPF_ALPHA) * g_encoder.filteredLeftSpeed;
    g_encoder.filteredRightSpeed =
        ENC_SPEED_LPF_ALPHA * rightNow +
        (1.0f - ENC_SPEED_LPF_ALPHA) * g_encoder.filteredRightSpeed;
}

static void run_control(uint32_t now)
{
    float dt;
    float avgSpeed;

    dt = (float)(now - g_lastControlTick) * 0.001f;
    if (dt <= 0.0f) {
        dt = 0.01f;
    }
    if (dt > 0.5f) {
        dt = 0.5f;
    }
    g_lastControlTick = now;

    Encoder_Update(&g_encoder);
    update_filtered_speed(dt);

    if (turnback_is_active()) {
        run_turnback(now);
        g_pid.leftPWM = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();
        return;
    }

    if (!is_running()) {
        return;
    }

    avgSpeed = (g_encoder.filteredLeftSpeed + g_encoder.filteredRightSpeed) * 0.5f;

    if (g_sysState == SYS_STRAIGHT) {
        DualLoop_ComputeStraight(&g_pid, avgSpeed, g_imu.yaw, g_imu.yawRate, g_fastYawRate, dt);
        MotorDriver_SetCoreDiff(g_pid.pwmCore, g_pid.headingDiffPWM);
        g_pid.leftPWM = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();
    } else if (g_sysState == SYS_TRACKING) {
        DualLoop_ComputeSpeed(&g_pid, avgSpeed, dt);
        LineTrack_Update(now, g_pid.pwmCore, g_fastYawRate);

        g_pid.leftPWM = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();

        if (!LineTrack_IsRunning()) {
            transition_stop(EXP_TRIGGER_AUTO);
        }
    } else if (g_sysState == SYS_SPINNING) {
        MotorDriver_Enable();
#if SPIN_PLACEHOLDER_PWM > 0
        MotorDriver_SetTurnPWM(SPIN_PLACEHOLDER_PWM, (int16_t)(-SPIN_PLACEHOLDER_PWM));
#else
        MotorDriver_SetTurnPWM(0, 0);
#endif
        g_pid.leftPWM = MotorDriver_GetActualL();
        g_pid.rightPWM = MotorDriver_GetActualR();
    }
}

static void send_telemetry(void)
{
    uint32_t tMs = turnback_is_active() ? (g_tickMs - g_turnback.startTick) : (g_tickMs - g_runStartTick);
    uint8_t run = is_running();

    if (turnback_is_active()) {
        BspUart_SendTelemetrySpin(tMs, g_experimentId, 0u,
                                  g_encoder.leftSpeed, g_encoder.rightSpeed,
                                  g_encoder.leftCount, g_encoder.rightCount,
                                  g_imu.yaw, g_fastYawRate,
                                  g_pid.leftPWM, g_pid.rightPWM);
        return;
    }

    if (g_sysState == SYS_TRACKING) {
        BspUart_SendTelemetryTrack(tMs, g_experimentId, run,
                                   g_encoder.leftSpeed, g_encoder.rightSpeed,
                                   g_encoder.leftCount, g_encoder.rightCount,
                                   g_imu.yaw, g_imu.yawRate,
                                   g_pid.pwmCore, g_lineTrack.devSpeed,
                                   g_pid.leftPWM, g_pid.rightPWM,
                                   g_lineTrack.sensorData, g_lineTrack.linePos,
                                   g_lineTrack.bearingDev, g_lineTrack.crossCount,
                                   g_lineTrack.dbgTrackState, g_lineTrack.dbgTurnDir,
                                   g_lineTrack.dbgCrossActive, g_lineTrack.crossState,
                                   g_lineTrack.dbgTelemState, g_lineTrack.dbgScoreEnabled,
                                   g_lineTrack.dbgTelemFlags,
                                   g_lineTrack.gainStage, g_lineTrack.searchPhase,
                                   g_lineTrack.searchDir, g_lineTrack.cornerLatchDir,
                                   g_lineTrack.cornerLatchTicks, g_lineTrack.recoverTicks,
                                   g_lineTrack.dbgActiveCount, g_lineTrack.dbgSearchLost,
                                   g_lineTrack.dbgSearchReacquired, g_lineTrack.dbgCornerCandidateDir,
                                   g_lineTrack.lastData, g_lineTrack.lastTurnDir,
                                   g_lineTrack.dbgResolvedSearchDir, g_lineTrack.dbgResolvedSource);
        return;
    }

    if (g_sysState == SYS_SPINNING) {
        BspUart_SendTelemetrySpin(tMs, g_experimentId, run,
                                  g_encoder.leftSpeed, g_encoder.rightSpeed,
                                  g_encoder.leftCount, g_encoder.rightCount,
                                  g_imu.yaw, g_fastYawRate,
                                  g_pid.leftPWM, g_pid.rightPWM);
        return;
    }

    BspUart_SendTelemetryStraight(tMs, g_experimentId, run,
                                  g_encoder.leftSpeed, g_encoder.rightSpeed,
                                  g_encoder.leftCount, g_encoder.rightCount,
                                  g_imu.yaw, g_fastYawRate,
                                  g_pid.pwmCore, g_pid.headingDiffPWM,
                                  g_pid.dTermPostDZ,
                                  g_pid.leftPWM, g_pid.rightPWM,
                                  g_pid.lastHi);
}

static void update_display(uint32_t now)
{
    if (is_running() || turnback_is_active()) {
        return;
    }

    if (!g_displayDirty && (now - g_lastDisplayTick) < 200u) {
        return;
    }

    g_lastDisplayTick = now;
    g_displayDirty = 0u;

    BspOled_ShowStatus(g_sysState, g_mode,
                       g_imu.yaw,
                       (float)g_encoder.leftSpeed,
                       (float)g_encoder.rightSpeed,
                       g_experimentId);

    if (IMU_ENABLE && !BNO085_IsReady()) {
        BspOled_ShowIMUInit(BNO085_GetInitStage(), BNO085_GetI2CAddr(), BNO085_GetInitFailCode());
    }
}

int main(void)
{
    char cmdBuf[96];
    uint32_t now;
    uint32_t telemetryPeriod;

    BspOled_Init();
    BspKey_Init();
    BspUart_Init();
    Buzzer_Init();
    BspOled_ShowFaultCode(FaultTrace_GetAndClearCode());

    Timer_Init();

    MotorDriver_Init();
    Encoder_Init();
    LineSensor_Init();

#if IMU_ENABLE
    BNO085_Init();
    BspOled_ShowIMUInit(BNO085_GetInitStage(), BNO085_GetI2CAddr(), BNO085_GetInitFailCode());
#endif

    DualLoop_Init(&g_pid);
    LineTrack_Init();
    memset(&g_encoder, 0, sizeof(g_encoder));
    memset(&g_imu, 0, sizeof(g_imu));
    turnback_clear();
    set_mode(MODE_TRACK);

    g_displayDirty = 1u;

    while (1)
    {
        now = g_tickMs;

        if (BspUart_TakeCommand(cmdBuf, sizeof(cmdBuf))) {
            handle_command(cmdBuf);
        }

        if (IMU_ENABLE && ((now - g_lastImuTick) >= IMU_READ_PERIOD_MS)) {
            run_imu_update();
        }

        stop_alert_update(now);

        if (g_sysState == SYS_TRACKING && LineTrack_PollFullBlackStop(now)) {
            transition_finish_hold(EXP_TRIGGER_AUTO);
            continue;
        }

        if (g_controlFlag) {
            g_controlFlag = 0u;
            run_control(now);
        }

        telemetryPeriod = is_running() ? TELEMETRY_PERIOD_MS : TELEMETRY_IDLE_PERIOD_MS;
        if ((now - g_lastTelemetryTick) >= telemetryPeriod) {
            g_lastTelemetryTick = now;
            send_telemetry();
        }

        update_display(now);
        process_key_event();
    }
}
