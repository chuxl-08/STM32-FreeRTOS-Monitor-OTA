#ifndef __HCSR04_H
#define __HCSR04_H
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明 HC-SR04 超声波测距驱动接口。
 * 2. 对上层屏蔽 Trig 时序、Echo 捕获和距离换算细节。
 * 3. Echo 必须先经过硬件分压再接入 PA1 / TIM2_CH2，不能直接接 5 V。
 *
 * 当前状态：
 * - 提供初始化接口和厘米级距离读取接口。
 */

 /*
 * @brief   HCSR04_Data_t: HCSR04距离数据
 * @note
 *          - distance_cm: 距离，单位 cm
 *          - valid: 数据有效性
 *              1 ： 有效
 *              0 ： 无效
 */
 typedef struct
 {
    uint16_t distance_cm;
    uint8_t valid;
 } HCSR04_Data_t;


  HCSR04_Status_t HCSR04_Init(void);
  HCSR04_Status_t HCSR04_Read(HCSR04_Data_t *data);

#endif /* __HCSR04_H */
