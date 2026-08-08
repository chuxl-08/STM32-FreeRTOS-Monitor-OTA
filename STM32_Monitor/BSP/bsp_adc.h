#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明 ADC1 初始化和单通道采样接口。
 * 2. 当前主要用于 PC0 / ADC1_IN10 读取光敏模块 AO。
 * 3. 对上层 Driver 屏蔽 ADC 时钟、GPIO、校准、EOC 等底层细节。
 *
 * 当前状态：
 * - 使用 ADC1 单通道软件触发采样。
 * - 初始化函数返回 BSP_ADC_Status_t 状态码。
 * - 读取函数通过指针输出 ADC 原始值，函数返回硬件访问状态。
 */


 BSP_ADC_Status_t BSP_ADC_Init(void);
 BSP_ADC_Status_t BSP_ADC_Read(uint16_t* adc_value);

#endif /* __BSP_ADC_H */
