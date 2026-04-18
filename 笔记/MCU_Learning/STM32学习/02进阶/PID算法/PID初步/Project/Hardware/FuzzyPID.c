#include "FuzzyPID.h"
#include "string.h"

// 定点数归一化：将实际值映射到论域[-3, 3]（Q12格式：-12288 ~ 12288）
static int32_t Normalize_Q12(int32_t value, int32_t min, int32_t max)
{
    if (max <= min) return 0;
    int64_t range = (int64_t)(max - min);
    int64_t offset = (int64_t)(value - min);
    int64_t normalized = ((offset * 6 * Q12_SCALE) / range) - (3 * Q12_SCALE);
    if (normalized < -12288) return -12288;
    if (normalized > 12288) return 12288;
    return (int32_t)normalized;
}

// 定点数反归一化
static int32_t Denormalize_Q12(int32_t qvalue, int32_t min, int32_t max)
{
    int64_t range = (int64_t)(max - min);
    int64_t value = (range * ((int64_t)qvalue + (3 * Q12_SCALE))) / 6 + min;
    if (value < min) return min;
    if (value > max) return max;
    return (int32_t)value;
}

// 定点数三角形隶属度计算（Q12格式）
static void CalculateMemberships_Q12(int32_t normalized, int32_t *membership)
{
    for (int i = 0; i < 7; i++) membership[i] = 0;
    if (normalized <= -12288) { membership[NB] = 4096; return; }
    if (normalized >= 12288) { membership[PL] = 4096; return; }

    if (normalized >= -12288 && normalized < -8192) {
        membership[NB] = (-8192 - normalized);
        membership[NM] = (normalized + 12288);
    } else if (normalized >= -8192 && normalized < -4096) {
        membership[NM] = (-4096 - normalized);
        membership[NS] = (normalized + 8192);
    } else if (normalized >= -4096 && normalized < 0) {
        membership[NS] = (0 - normalized);
        membership[ZO] = (normalized + 4096);
    } else if (normalized >= 0 && normalized < 4096) {
        membership[ZO] = (4096 - normalized);
        membership[PS] = normalized;
    } else if (normalized >= 4096 && normalized < 8192) {
        membership[PS] = (8192 - normalized);
        membership[PM] = (normalized - 4096);
    } else if (normalized >= 8192 && normalized < 12288) {
        membership[PM] = (12288 - normalized);
        membership[PL] = (normalized - 8192);
    }
}

static int32_t Q12_Min(int32_t a, int32_t b) { return (a < b) ? a : b; }
static int32_t Q12_Max(int32_t a, int32_t b) { return (a > b) ? a : b; }

void FuzzyPID_Init(FuzzyPIDController *fpid)
{
    memset(fpid, 0, sizeof(FuzzyPIDController));
    fpid->e_min = FLOAT_TO_Q12(-180.0f);
    fpid->e_max = FLOAT_TO_Q12(180.0f);
    fpid->ec_min = FLOAT_TO_Q12(-50.0f);
    fpid->ec_max = FLOAT_TO_Q12(50.0f);
    fpid->kp_min = FLOAT_TO_Q12(0.0f);
    fpid->kp_max = FLOAT_TO_Q12(10.0f);
    fpid->ki_min = FLOAT_TO_Q12(0.0f);
    fpid->ki_max = FLOAT_TO_Q12(5.0f);
    fpid->kd_min = FLOAT_TO_Q12(0.0f);
    fpid->kd_max = FLOAT_TO_Q12(2.0f);
    fpid->Kp = FLOAT_TO_Q12(2.0f);
    fpid->Ki = FLOAT_TO_Q12(0.5f);
    fpid->Kd = FLOAT_TO_Q12(0.1f);

    int8_t kp_rules[7][7] = {{3,3,2,2,1,0,0},{3,3,2,1,1,0,-1},{2,2,2,1,0,-1,-1},{2,2,1,0,-1,-2,-2},{1,1,0,-1,-1,-2,-2},{1,0,-1,-2,-2,-2,-3},{0,0,-1,-2,-2,-3,-3}};
    int8_t ki_rules[7][7] = {{-3,-3,-2,-2,-1,0,0},{-3,-3,-2,-1,-1,0,0},{-3,-2,-1,-1,0,1,1},{-2,-2,-1,0,1,2,2},{-2,-1,0,1,1,2,3},{0,0,1,1,2,3,3},{0,0,1,2,2,3,3}};
    int8_t kd_rules[7][7] = {{1,-1,-3,-3,-3,-2,1},{1,-1,-3,-2,-2,-1,0},{0,-1,-2,-2,-1,-1,0},{0,-1,-1,-1,-1,-1,0},{0,0,0,0,0,0,0},{3,-1,1,1,1,1,3},{3,2,2,2,1,1,3}};
    memcpy(fpid->rule_kp, kp_rules, sizeof(kp_rules));
    memcpy(fpid->rule_ki, ki_rules, sizeof(ki_rules));
    memcpy(fpid->rule_kd, kd_rules, sizeof(kd_rules));
}

void FuzzyPID_SetParameters(FuzzyPIDController *fpid, float Kp, float Ki, float Kd,
                            float e_min, float e_max, float ec_min, float ec_max,
                            float kp_min, float kp_max, float ki_min, float ki_max,
                            float kd_min, float kd_max)
{
    fpid->Kp = FLOAT_TO_Q12(Kp); fpid->Ki = FLOAT_TO_Q12(Ki); fpid->Kd = FLOAT_TO_Q12(Kd);
    fpid->e_min = FLOAT_TO_Q12(e_min); fpid->e_max = FLOAT_TO_Q12(e_max);
    fpid->ec_min = FLOAT_TO_Q12(ec_min); fpid->ec_max = FLOAT_TO_Q12(ec_max);
    fpid->kp_min = FLOAT_TO_Q12(kp_min); fpid->kp_max = FLOAT_TO_Q12(kp_max);
    fpid->ki_min = FLOAT_TO_Q12(ki_min); fpid->ki_max = FLOAT_TO_Q12(ki_max);
    fpid->kd_min = FLOAT_TO_Q12(kd_min); fpid->kd_max = FLOAT_TO_Q12(kd_max);
}

static void FuzzyInference_Q12(FuzzyPIDController *fpid, int32_t *e_memship, int32_t *ec_memship,
                              int32_t *kp_act, int32_t *ki_act, int32_t *kd_act)
{
    for (int i = 0; i < 7; i++) { kp_act[i] = 0; ki_act[i] = 0; kd_act[i] = 0; }
    for (int e_idx = 0; e_idx < 7; e_idx++) {
        for (int ec_idx = 0; ec_idx < 7; ec_idx++) {
            int32_t activation = Q12_Min(e_memship[e_idx], ec_memship[ec_idx]);
            if (activation > 40) {
                int kp_output_idx = fpid->rule_kp[e_idx][ec_idx] + 3;
                int ki_output_idx = fpid->rule_ki[e_idx][ec_idx] + 3;
                int kd_output_idx = fpid->rule_kd[e_idx][ec_idx] + 3;
                if (kp_output_idx < 0) kp_output_idx = 0; if (kp_output_idx > 6) kp_output_idx = 6;
                if (ki_output_idx < 0) ki_output_idx = 0; if (ki_output_idx > 6) ki_output_idx = 6;
                if (kd_output_idx < 0) kd_output_idx = 0; if (kd_output_idx > 6) kd_output_idx = 6;
                kp_act[kp_output_idx] = Q12_Max(kp_act[kp_output_idx], activation);
                ki_act[ki_output_idx] = Q12_Max(ki_act[ki_output_idx], activation);
                kd_act[kd_output_idx] = Q12_Max(kd_act[kd_output_idx], activation);
            }
        }
    }
}

static void Defuzzification_Q12(int32_t *act, int32_t *output)
{
    int64_t numerator = 0, denominator = 0;
    for (int i = 0; i < 7; i++) {
        int32_t value = (i - 3) * 4096;
        numerator += (int64_t)act[i] * value;
        denominator += act[i];
    }
    *output = (denominator > 0) ? (int32_t)(numerator / denominator) : 0;
}

float FuzzyPID_Compute(FuzzyPIDController *fpid, float setpoint, float actual)
{
    int32_t sp_q12 = FLOAT_TO_Q12(setpoint);
    int32_t pv_q12 = FLOAT_TO_Q12(actual);
    int32_t error = sp_q12 - pv_q12;
    fpid->error_history[2] = fpid->error_history[1];
    fpid->error_history[1] = fpid->error_history[0];
    fpid->error_history[0] = error;
    int32_t error_change = error - fpid->error_history[1];
    fpid->e_normalized = Normalize_Q12(error, fpid->e_min, fpid->e_max);
    fpid->ec_normalized = Normalize_Q12(error_change, fpid->ec_min, fpid->ec_max);
    int32_t e_memship[7], ec_memship[7];
    CalculateMemberships_Q12(fpid->e_normalized, e_memship);
    CalculateMemberships_Q12(fpid->ec_normalized, ec_memship);
    int32_t kp_act[7], ki_act[7], kd_act[7];
    FuzzyInference_Q12(fpid, e_memship, ec_memship, kp_act, ki_act, kd_act);
    int32_t kp_output, ki_output, kd_output;
    Defuzzification_Q12(kp_act, &kp_output);
    Defuzzification_Q12(ki_act, &ki_output);
    Defuzzification_Q12(kd_act, &kd_output);
    fpid->Kp = Denormalize_Q12(kp_output, fpid->kp_min, fpid->kp_max);
    fpid->Ki = Denormalize_Q12(ki_output, fpid->ki_min, fpid->ki_max);
    fpid->Kd = Denormalize_Q12(kd_output, fpid->kd_min, fpid->kd_max);
    int32_t e = error, e_prev = fpid->error_history[1], e_prev_prev = fpid->error_history[2];
    int32_t p_term = Q12_MUL(fpid->Kp, (e - e_prev));
    int32_t i_term = Q12_MUL(fpid->Ki, e);
    int32_t d_term = Q12_MUL(fpid->Kd, (e - 2*e_prev + e_prev_prev));
    fpid->integral += i_term;
    if (fpid->integral > 2147483647/4) fpid->integral = 2147483647/4;
    if (fpid->integral < -2147483647/4) fpid->integral = -2147483647/4;
    int32_t output_q12 = p_term + fpid->integral + d_term;
    return Q12_TO_FLOAT(output_q12);
}

void FuzzyPID_Reset(FuzzyPIDController *fpid)
{
    fpid->error_history[0] = 0; fpid->error_history[1] = 0; fpid->error_history[2] = 0;
    fpid->e_normalized = 0; fpid->ec_normalized = 0; fpid->integral = 0;
}
