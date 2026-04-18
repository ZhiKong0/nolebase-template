#ifndef FUZZY_PID_H
#define FUZZY_PID_H
#include <stdint.h>

// 定点数配置：Q12格式（12位小数）
// 范围：-512 ~ 511（实际值），精度：1/4096 = 0.00024
#define Q12_SHIFT    12
#define Q12_SCALE    (1 << Q12_SHIFT)  // 4096

// 将float转换为Q12定点数
#define FLOAT_TO_Q12(f)    ((int32_t)((f) * Q12_SCALE))
// 将Q12定点数转换为float
#define Q12_TO_FLOAT(q)    ((float)(q) / Q12_SCALE)
// Q12乘法（结果右移12位，使用64位中间值防止溢出）
#define Q12_MUL(a, b)      (((int64_t)(a) * (int64_t)(b)) >> Q12_SHIFT)
// Q12除法（左移12位）
#define Q12_DIV(a, b)      (((int64_t)(a) << Q12_SHIFT) / (b))

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
    // PID参数（Q12格式，使用int32_t增加范围和精度）
    int32_t Kp;
    int32_t Ki;
    int32_t Kd;
    
    // 误差和误差变化率的论域范围（Q12格式）
    int32_t e_min, e_max;
    int32_t ec_min, ec_max;
    
    // PID参数输出范围（Q12格式）
    int32_t kp_min, kp_max;
    int32_t ki_min, ki_max;
    int32_t kd_min, kd_max;

    // 归一化后的值（Q12格式，范围-3~3对应-12288~12288）
    int32_t e_normalized;
    int32_t ec_normalized;

    // 模糊规则表 (7x7)，值范围-3~3
    int8_t rule_kp[7][7];
    int8_t rule_ki[7][7];
    int8_t rule_kd[7][7];

    // 误差历史（Q12格式）
    int32_t error_history[3];
    
    // 积分项（Q12格式，防止积分饱和）
    int32_t integral;

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
