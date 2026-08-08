#include "light_sensor.h"
#include "stm32f10x.h"                  // Device header
#include "bsp_adc.h"

/*
 * 文件职责：
 * 1. 实现光敏模块驱动。
 * 2. 通过 BSP/bsp_adc.* 读取 ADC 原始采样值。
 * 3. 将 ADC 原始值转换为光照百分比和光照等级。
 * 4. 本层不直接操作 ADC 寄存器，不负责 OLED 显示、串口打印或上传逻辑。
 */

 static uint8_t s_lightsensor_init_status = 0;

/**
  * @brief  初始化光敏模块，主要配置 ADC1。
  * @param  None
  * @retval LightSensor_Status_t:
  *         - LIGHT_SENSOR_OK：底层 ADC 初始化成功。
  *         - LIGHT_SENSOR_ERROR_ADC：底层 ADC 初始化或校准失败。
  * @note   当前光敏模块 AO 接 PC0 / ADC1_IN10。
  */
LightSensor_Status_t LightSensor_Init(void)
{
    if(s_lightsensor_init_status)
    {
        return LIGHT_SENSOR_OK;
    }

    if(BSP_ADC_Init() == BSP_ADC_OK)
    {
        s_lightsensor_init_status = 1;
        return LIGHT_SENSOR_OK;
    }
    else return LIGHT_SENSOR_ERROR_ADC;
}

/*
 * @brief  读取光敏模块数据。
 * @param  data: 指向 LightSensor_Data_t 结构体的指针，用于存储读取到的数据。
 * @retval LightSensor_Status_t:
 *         - LIGHT_SENSOR_OK：读取成功，raw、percent、level 均有效。
 *         - LIGHT_SENSOR_ERROR_PARAM：data 为空指针。
 *         - LIGHT_SENSOR_ERROR_ADC：底层 ADC 读取失败。
 * @note   数据处理流程：
 *         1. 调用 BSP_ADC_Read() 获取 ADC 原始值 raw。
 *         2. 根据实测方向将 raw 转换为 0~100 的相对光照百分比。
 *         3. 根据百分比划分 DARK、DIM、NORMAL、BRIGHT 四个等级。
 *         percent 只表示当前模块和接线条件下的相对光照强度。
 */
LightSensor_Status_t LightSensor_Read(LightSensor_Data_t *data)
{
    uint16_t adc_value;
    if(data == 0)
    {
        return LIGHT_SENSOR_ERROR_PARAM;
    }

    data->raw = 0;
    data->percent = 0;
    data->level = LIGHT_SENSOR_LEVEL_DARK;
    data->valid = 0;

    if(BSP_ADC_Read(&adc_value) != BSP_ADC_OK)
    {
        return LIGHT_SENSOR_ERROR_ADC;
    }

    data->raw = adc_value;

    data->percent = 100 - (adc_value * 100 / 4095);

    if(data->percent <= 25)
    {
        data->level = LIGHT_SENSOR_LEVEL_DARK;
    }
    else if(data->percent <= 50)
    {
        data->level = LIGHT_SENSOR_LEVEL_DIM;
    }
    else if(data->percent <= 75)
    {
        data->level = LIGHT_SENSOR_LEVEL_NORMAL;
    }
    else if(data->percent <= 100)
    {
        data->level = LIGHT_SENSOR_LEVEL_BRIGHT;
    }

    data->valid = 1;

    return LIGHT_SENSOR_OK;
}
