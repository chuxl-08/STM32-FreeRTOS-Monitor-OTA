#ifndef __APP_DETECT_H
#define __APP_DETECT_H
#include "error_code.h"
#include "system_data.h"

/*
 * 文件职责：
 * 1. 声明人员检测算法相关接口。
 * 2. 基于 AMG8833 热阵列摘要和 HC-SR04 距离结果判断当前方向是否有人。
 * 3. 为 ScanTask 和 ManualTask 提供统一的规则检测入口。
 *
 * 当前状态：
 * - 已提供 App_Detect_Update() 规则检测接口。
 */



const char *AppDetectStatusName(AppDetect_Status_t status);
AppDetect_Status_t App_Detect_Update(System_DistanceData_t *distance_data, System_ThermalData_t *thermal_data, System_DetectData_t *detect_data);

#endif /* __APP_DETECT_H */
