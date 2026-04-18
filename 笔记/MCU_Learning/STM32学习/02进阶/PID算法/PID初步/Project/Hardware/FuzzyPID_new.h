#ifndef FUZZY_PID_H
#define FUZZY_PID_H
#include <stdint.h>

// 定点数配置：Q8格式（8位小数）
// 范围：-128 ~ 127，精度：1/256 = 0.0039
#define Q8_SHIFT    8
#define Q8_SCALE    (1 << Q8_SHIFT)  // 256

// 将float转换为Q8定点数
#define FLOAT_TO_Q8(f)    ((int16_t)((f) * Q8_SCALE))
// 将Q8定点数转换为float
#define Q8_TO_FLOAT(q)    ((float)(q) / Q8_SCALE)
// Q8乘法（结果右移8位）
#define Q8_MUL(a, b)      (((int32_t)(a) * (int32_t)(b)) >> Q8_SHIFT)

// 模糊语言值枚举（索引）
typedef enum
{
    NB = 0,  // Negative Big
    NM = 1,  // Negative Medium
    NS = 2,  // Negative Small
    ZO = 3,  // Zero
    PS = 4,  // Positive Small
    PM = 5,  // Positive Medium
    PL = 6   // Positive Large
} FuzzySetIndex;

// 定点数模糊PID控制器
typedef struct
{
    // PID参数（Q8格式）
    int16_t Kp;
    int16_t Ki;
    int16_t Kd;
    
    // 误差和误差变化率的论域范围（Q8格式，实际值×256）
    int16_t e_min, e_max;
    int16_t ec_min, ec_max;
    
    // PID参数输出范围（Q8格式）
    int16_t kp_min, kp_max;
    int16_t ki_min, ki_max;
    int16_t kd_min, kd_max;

    // 归一化后的值（Q8格式，范围-3~3对应-768~768）
    int16_t e_normalized;
    int16_t ec_normalized;

    // 模糊规则表 (7x7)，值范围-3~3
    int8_t rule_kp[7][7];
    int8_t rule_ki[7][7];
    int8_t rule_kd[7][7];

    // 误差历史（Q8格式）
    int16_t error_history[3];
    
    // 积分项（Q8格式，防止积分饱和）
    int16_t integral;

} FuzzyPIDController;

// 初始化模糊PID（使用默认参数）
void FuzzyPID_Init(FuzzyPIDController *fpid);

// 设置模糊PID参数范围（传入float，内部转为Q8）
void FuzzyPID_SetParameters(FuzzyPIDController *fpid, 
                            float Kp, float Ki, float Kd,
                            float e_min, float e_max, 
                            float ec_min, float ec_max,
                            float kp_min, float kp_max, 
                            float ki_min, float ki_max,
                            float kd_min, float kd_max);

// 模糊PID计算（输入：设定值、实际值，均为float；输出：控制量float）
float FuzzyPID_Compute(FuzzyPIDController *fpid, float setpoint, float actual);

// 重置模糊PID
void FuzzyPID_Reset(FuzzyPIDController *fpid);

#endif
