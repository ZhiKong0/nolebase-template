#ifndef __FUN_H_
#define __FUN_H_

extern volatile float Target_left,Target_right;

int16_t Encoderleft_Get(void);
int16_t Encoderright_Get(void);
void Motorleft_Setpwm(int16_t Speed);
void Motorright_Setpwm(int16_t Speed);
void Motor_StopAll(void);
void track(void);
void track_reset(void);
void track_cycle_speed(void);
uint8_t track_get_speed_level(void);
uint8_t track_get_raw_bits(void);
float track_get_error(void);
uint8_t key_get(void);
void Serial_Printf(const char *format,...);
void led_mode0(void);
void led_mode1(void);
void led_mode2(void);

#endif
