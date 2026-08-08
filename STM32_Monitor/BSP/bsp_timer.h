#ifndef __BSP_TIMER_H
#define __BSP_TIMER_H
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明基础定时器、输入捕获和编码器模式相关接口。
 * 2. 当前 TIM2 用于 HC-SR04 Echo 输入捕获，PA1 对应 TIM2_CH2。
 * 3. 当前 TIM3 用于旋转编码器 A/B 相计数，PC6/PC7 对应 TIM3_CH1/CH2 完全重映射。
 * 4. 向上层 Driver 提供微秒级高电平宽度捕获能力和编码器计数能力。
 *
 * 当前状态：
 * - TIM2 输入捕获已用于测量 HC-SR04 Echo 高电平时间。
 * - TIM3 编码器模式用于读取旋转编码器计数。
 * - 初始化函数返回 BSP_TIMER_Status_t 状态码。
 * - 捕获函数通过指针输出高电平时间，单位 us。
 */


BSP_TIMER_Status_t BSP_Timer_Init(void);
BSP_TIMER_Status_t BSP_TIM2_Capture_GetHighTimeUs(uint32_t *hightime);
BSP_TIMER_Status_t BSP_TIM3_Encoder_Init(void);
BSP_TIMER_Status_t BSP_TIM3_Encoder_GetCount(int16_t *count);
BSP_TIMER_Status_t BSP_TIM3_Encoder_SetCount(int16_t count);
BSP_TIMER_Status_t BSP_TIM3_Encoder_ClearCount(void);

#endif /* __BSP_TIMER_H */
