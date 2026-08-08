#include "app_detect.h"

/*
 * 文件职责：
 * 1. 实现人员检测算法。
 * 2. 使用 max_temp - avg_temp、hot_pixel_count 和 distance_cm 进行规则判断。
 */

// 最高温和平均温的差值门槛，单位 0.01 C，500 表示 5.00 C。
#define DETECT_TEMP_THRESHOLD                 500
#define DETECT_TEMP_HOT_COUNT_THRESHOLD       6
#define DETECT_DISTANCE_MIN                   20
#define DETECT_DISTANCE_MAX                   150


/*
 * @brief   将 app_detect.* 状态码转换为可读字符串。
 * @param   status: AppDetect_Status_t 状态码。
 * @retval  const char *: 状态名称字符串。
 */
const char *AppDetectStatusName(AppDetect_Status_t status)
{
    switch(status)
    {
        case APP_DETECT_OK:
            return "OK";
        case APP_DETECT_ERROR_PARAM:
            return "PARAM";
        case APP_DETECT_ERROR_SENSOR_INVALID:
            return "SENSOR_INVALID_ERR";
        default:
            return "UNKNOWN";
    }
}

/**
  * @brief  根据系统数据执行一次人员检测。
  * @param  system_data: 系统统一数据结构，读取传感器数据并写入检测结果。
  * @retval AppDetect_Status_t:
  *         - APP_DETECT_OK：检测完成。
  *         - APP_DETECT_ERROR_PARAM：输入参数为空。
  *         - APP_DETECT_ERROR_SENSOR_INVALID: 传感器数据无效
  * @note   检测到人 以下三个条件都需满足：
  *             - 最大和平均温度差值大于等于阈值 max_temp - avg_temp >= DETECT_TEMP_THRESHOLD
  *             - 热区像素数量大于等于阈值 hot_pixel_count >= DETECT_TEMP_HOT_COUNT_THRESHOLD
  *             - 物体距离在检测区间内 DETECT_DISTANCE_MIN <= distance_cm <= DETECT_DISTANCE_MAX
  */
AppDetect_Status_t App_Detect_Update(System_DistanceData_t *distance_data, System_ThermalData_t *thermal_data, System_DetectData_t *detect_data)
{
    uint8_t hcsr04_data_valid;
    uint8_t amg8833_data_valid;
    uint8_t temp_difference_result;
    uint8_t hot_pixel_count_result;
    uint8_t distance_result;
    int16_t temp_difference;
    uint16_t distance;

    if(distance_data == 0 || thermal_data == 0 || detect_data == 0)
    {
        return APP_DETECT_ERROR_PARAM;
    }

    detect_data->human_detected = 0;
    
    hcsr04_data_valid = distance_data->hcsr04_data.valid;
    amg8833_data_valid = thermal_data->amg8833_data.valid;
    if((hcsr04_data_valid && amg8833_data_valid) != 1)
    {
        return APP_DETECT_ERROR_SENSOR_INVALID;
    }

    temp_difference_result = 0;
    hot_pixel_count_result = 0;
    distance_result = 0;

    temp_difference = thermal_data->amg8833_data.temp_summary.max_temp_x100 - thermal_data->amg8833_data.temp_summary.avg_temp_x100;
    if(temp_difference >= DETECT_TEMP_THRESHOLD)
    {
        temp_difference_result = 1;
    }
    if(thermal_data->amg8833_data.temp_summary.hot_pixel_count >= DETECT_TEMP_HOT_COUNT_THRESHOLD)
    {
        hot_pixel_count_result = 1;
    }

    distance = distance_data->hcsr04_data.distance_cm;
    if((distance >= DETECT_DISTANCE_MIN) && (distance <= DETECT_DISTANCE_MAX))
    {
        distance_result = 1;
    }

    if(temp_difference_result && hot_pixel_count_result && distance_result)
    {
        detect_data->human_detected = 1;
    }
    else
    {
        detect_data->human_detected = 0;
    }

    return APP_DETECT_OK;
}
