#ifndef __SERVO_H
#define __SERVO_H
#include "error_code.h"
#include "stm32f10x.h"

/*
 * 文件职责：
 * 1. 声明 SG90 舵机驱动接口。
 * 2. 负责角度到 PWM 脉宽的转换。
 *
 * 当前状态：
 * - 已提供初始化、角度设置、当前角度读取和当前 PWM 脉宽读取接口。
 */


Servo_Status_t Servo_Init(void);
Servo_Status_t Servo_SetAngle(uint8_t angle);
uint8_t Servo_GetAngle(void);
uint16_t Servo_GetPulseUs(void);

#endif /* __SERVO_H */
