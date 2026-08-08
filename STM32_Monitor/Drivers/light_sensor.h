#ifndef __LIGHT_SENSOR_H
#define __LIGHT_SENSOR_H
#include "stm32f10x.h"
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明光敏电阻模块驱动接口。
 * 2. 对上层提供光照原始值、相对百分比和离散等级。
 * 3. 当前只使用 AO 模拟输出，不读取 DO 数字阈值输出。
 * 4. 底层 ADC 采样由 BSP/bsp_adc.* 完成，本驱动只负责传感器语义转换。
 *
 * 当前状态：
 * - 支持光敏模块初始化。
 * - 支持读取 ADC raw、percent 和 level。
 * - 光照百分比为相对值，不等同于物理 lux。
 */

 /*
  * 光照等级定义：
  * - DARK：低光照或遮挡状态。
  * - DIM：偏暗环境。
  * - NORMAL：普通环境光。
  * - BRIGHT：强光或明亮环境。
  *
  * 等级由 LightSensor_Read() 根据 percent 字段划分，
  * 上层 App 可直接使用 level 进行显示、告警或上传。
  */
 typedef enum
 {
    LIGHT_SENSOR_LEVEL_DARK = 0,
    LIGHT_SENSOR_LEVEL_DIM,
    LIGHT_SENSOR_LEVEL_NORMAL,
    LIGHT_SENSOR_LEVEL_BRIGHT
 } LightSensor_Level_t;


 /*
 * @brief   LightSensor_Data_t: lightsensor光照数据
 * @note
 *          - percent: 相对光照百分比，范围 0~100
 *          - level: 离散光照等级
 *          - raw： ADC 原始值，范围为 0~4095
 *          - valid: 数据有效性
 *              1 ： 有效
 *              0 ： 无效
 */
typedef struct
{
    uint8_t percent;
    LightSensor_Level_t level;
    uint16_t raw;
    uint8_t valid;
} LightSensor_Data_t;


LightSensor_Status_t LightSensor_Init(void);
LightSensor_Status_t LightSensor_Read(LightSensor_Data_t *data);


#endif /* __LIGHT_SENSOR_H */
