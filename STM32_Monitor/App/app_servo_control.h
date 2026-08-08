#ifndef __APP_SERVO_CONTROL_H
#define __APP_SERVO_CONTROL_H

#include "stm32f10x.h"
#include "error_code.h"
#include "system_data.h"

/*
 * 文件职责：
 * 1. 声明手动模式下舵机角度控制的应用层接口。
 * 2. 从 app_input.* 输入快照中消费编码器旋转增量，并转换为舵机角度控制。
 * 3. 对上层屏蔽输入累计、角度限幅和舵机回中策略。
 *
 * 当前状态：
 * - 支持编码器旋转调整舵机角度。
 * - 编码器初始化、按键点击和页面切换由 app_input.* / app_system.* / app_menu.* 分工处理。
 */

const char *AppServoControlStatusName(AppServoControl_Status_t status);
AppServoControl_Status_t App_ServoControl_Init(System_ServoData_t *servo_data);
void App_ServoControl_ResetInputBaseline(void);
AppServoControl_Status_t App_ServoControl_Update(System_ServoData_t *servo_data, int32_t current_encoder_total);

#endif /* __APP_SERVO_CONTROL_H */
