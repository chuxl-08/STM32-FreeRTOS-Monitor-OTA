#ifndef __BSP_PWM_H
#define __BSP_PWM_H
#include "error_code.h"
#include "stm32f10x.h"

/*
 * 文件职责：
 * 1. 声明 SG90 舵机 PWM 底层初始化和脉宽设置接口。
 * 2. 当前 TIM4_CH3 / PB8 用于输出 50 Hz 舵机控制信号。
 * 3. 对上层 servo.* 屏蔽 TIM4、GPIO 和脉宽范围检查细节。
 *
 * 当前状态：
 * - 已提供 PB8 / TIM4_CH3 的 SG90 舵机 PWM 初始化和脉宽设置接口。
 */


BSP_PWM_Status_t BSP_PWM_Servo_Init(void);
BSP_PWM_Status_t BSP_PWM_Servo_SetPulseUs(uint16_t pulse_us);

#endif /* __BSP_PWM_H */
