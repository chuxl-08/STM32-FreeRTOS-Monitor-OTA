#ifndef __ENCODER_H
#define __ENCODER_H
#include "stm32f10x.h"
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明旋转编码器驱动接口。
 * 2. 负责 A/B 相累计计数读取和相邻两次读取的增量计算。
 * 3. 底层使用 BSP TIM3 编码器模式，Driver 层不直接操作 TIM3 寄存器。
 * 4. PC4 / SW 使用 EXTI4 下降沿中断触发，Driver 层提供按下和点击事件接口。
 *
 * 当前状态：
 * - 支持初始化、累计计数读取、增量读取、计数清零和 SW 点击事件。
 */

/*
 * @brief   Encoder_Data_t: 编码器数据。
 * @note
 *          - encoder_delta: 编码器旋转增量。
 *          - button_pressed: 当前按键电平状态。
 *          - button_clicked: 是否发生过点击事件。
 */
typedef struct
{
   int16_t encoder_delta;
   uint8_t button_pressed;
   uint8_t button_clicked;
} Encoder_Data_t;

Encoder_Status_t Encoder_Init(void);
Encoder_Status_t Encoder_GetCount(int16_t *count);
Encoder_Status_t Encoder_GetDelta(int16_t *delta);
Encoder_Status_t Encoder_Clear(void);
Encoder_Status_t Encoder_ButtonUpdate(void);
uint8_t Encoder_ButtonIsPressed(void);
uint8_t Encoder_ButtonWasClicked(void);

#endif /* __ENCODER_H */
