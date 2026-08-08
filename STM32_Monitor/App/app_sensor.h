#ifndef __APP_SENSOR_H
#define __APP_SENSOR_H
#include "error_code.h"
#include "system_data.h"

/*
 * 文件职责：
 * 1. 声明应用层传感器采集调度接口。
 * 2. 将传感器按数据语义拆分为环境采集和方向采集。
 * 3. 环境采集负责 DHT11 和光敏传感器。
 * 4. 方向采集负责 HC-SR04 和 AMG8833。
 * 5. 对上层屏蔽具体驱动读取顺序、有效性判断和错误码处理。
 *
 * 当前状态：
 * - 提供环境采集接口。
 * - 提供方向采集接口。
 */

const char *AppSensorStatusName(AppSensor_Status_t status);
AppSensor_Status_t App_Sensor_EnvInit(System_EnvSensorStatus_t *env_sensor_status);
AppSensor_Status_t App_Sensor_EnvUpdate(System_EnvTaskData_t *env);
AppSensor_Status_t App_Sensor_DirectionInit(System_DirectionSensorStatus_t *direction_sensor_status);
AppSensor_Status_t App_Sensor_DirectionUpdate_Manual(System_ManualTaskData_t *manual_data);
AppSensor_Status_t App_Sensor_DirectionUpdate_Scan(System_ScanPoint_t *scanpoint, System_DirectionSensorStatus_t *direction_sensor_status);

#endif /* __APP_SENSOR_H */
