#ifndef __APP_INPUT_H
#define __APP_INPUT_H

#include "stm32f10x.h"
#include "system_data.h"
#include "error_code.h"

/*
 * 文件职责：
 * 1. 统一采集编码器旋转增量和按键状态。
 * 2. 为 InputTask 提供输入数据初始化和周期更新接口。
 * 3. 避免多个模块直接读取 Encoder_GetDelta() 导致输入事件被抢先消费。
 */


const char *AppInputStatusName(AppInput_Status_t status);
AppInput_Status_t App_Input_Init(System_InputTaskData_t *input_data);
AppInput_Status_t App_Input_Update(System_InputTaskData_t *input_data);


#endif /* __APP_INPUT_H */
