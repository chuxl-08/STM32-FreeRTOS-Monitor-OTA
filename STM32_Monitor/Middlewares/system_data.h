#ifndef __SYSTEM_DATA_H
#define __SYSTEM_DATA_H
#include "stm32f10x.h"
#include "dht11.h"
#include "light_sensor.h"
#include "hcsr04.h"
#include "amg8833.h"
#include "encoder.h"
#include "error_code.h"


#define SYSTEM_SCAN_POINT_COUNT        5

/*
 * 文件职责：
 * 1. 声明系统统一数据结构。
 * 2. 统一保存环境数据、当前方向实时数据、多角度扫描数据、舵机状态、传感器状态和运行状态。
 * 3. FreeRTOS 阶段通过队列发布快照，避免多个任务直接共享可变数据。
 *
 * 当前状态：
 * - distance / thermal / detect 表示当前方向实时工作区。
 * - scan.points[] 表示从当前方向实时工作区保存出来的多角度扫描快照。
 */

 /*
 * @brief   System_EnvData_t: 环境数据
 * @note
 *          - dht11_data: dht11温湿度数据
 *          - LightSensor_Data_t: lightsensor光照数据
 */
 typedef struct
 {
    DHT11_Data_t dht11_data;
    LightSensor_Data_t lightsensor_data;
 } System_EnvData_t;

 /*
 * @brief   System_DistanceData_t: 当前方向距离数据
 * @note
 *          - hcsr04_data: HC-SR04 最近一次方向采集结果。
 */
 typedef struct
 {
    HCSR04_Data_t hcsr04_data;
 } System_DistanceData_t;

 /*
 * @brief   System_ThermalData_t: 当前方向热阵列数据
 * @note
 *          - amg8833_data: AMG8833 最近一次方向采集结果。
 */
 typedef struct
 {
    AMG8833_Data_t amg8833_data;
 } System_ThermalData_t;

 /*
 * @brief   System_DetectData_t: 当前方向人员检测结果
 * @note
 *          - human_detected: 基于当前方向 distance 和 thermal 得到的人员检测结果。
 *              1 ： 检出
 *              0 ： 未检出
 */
typedef struct
{
    uint8_t human_detected;
} System_DetectData_t;

/*
 * @brief   System_ServoData_t: 舵机数据。
 * @note
 *          - target_angle: 舵机目标调整角度，单位度。
 *          - current_angle: 当前舵机角度，单位度。
 *          - current_index: 当前扫描角度序号，自动扫描模式下有效。
 *          - valid: 舵机数据是否有效。
 *          - servo_status: 舵机最近一次状态。
 */
typedef struct
{
   uint8_t target_angle;
   uint8_t current_angle;
   uint8_t current_index;
   uint8_t valid;
   Servo_Status_t servo_status;
} System_ServoData_t;

/*
 * @brief   System_ScanPoint_t: 单个扫描角度的检测结果快照。
 * @note
 *          - point_angle: 当前点对应的扫描角度，单位度。
 *          - point_distance: 该扫描角度保存时的距离数据。
 *          - point_thermal: 该扫描角度保存时的热阵列数据。
 *          - point_detected: 该扫描角度保存时的人员检测结果
 *          - point_valid: 当前扫描点快照数据有效性
 */
typedef struct
{
    uint8_t point_angle;
    System_DistanceData_t point_distance;
    System_ThermalData_t point_thermal;
    System_DetectData_t point_detected;
    uint8_t point_valid;
} System_ScanPoint_t;

/*
 * @brief   System_DirectionSensorStatus_t: 方向类传感器最近一次状态。
 * @note
 *          - hcsr04_status: HC-SR04 最近一次状态。
 *          - amg8833_status: AMG8833 最近一次状态。
 */
typedef struct
{
   HCSR04_Status_t hcsr04_status;
   AMG8833_Status_t amg8833_status;
} System_DirectionSensorStatus_t;

/*
 * @brief   System_ScanData_t: 多角度扫描结果
 * @note
 *          - points: 多角度扫描数据集合
 *          - servo: 舵机实时数据
 *          - detect: 多角度汇总人员检测结果
 *          - direction_sensor_status: 方向类传感器状态（HCSR04、AMG8833）
 *          - valid: 至少有一个扫描点数据有效
 */
typedef struct
{
   System_ScanPoint_t points[SYSTEM_SCAN_POINT_COUNT];
   System_ServoData_t servo;
   System_DetectData_t detect;
   System_DirectionSensorStatus_t  direction_sensor_status;
   uint8_t valid;
} System_ScanTaskData_t;

/*
 * @brief   System_EnvSensorStatus_t: 环境类传感器最近一次状态。
 * @note
 *          - dht11_status: DHT11 最近一次状态。
 *          - lightsensor_status: 光敏传感器最近一次读取状态。
 */
typedef struct
{
    DHT11_Status_t dht11_status;
    LightSensor_Status_t lightsensor_status;
} System_EnvSensorStatus_t;

/*
 * @brief   System_EnvTaskData_t: 环境检测结果。
 * @note
 *          - env: DHT11 环境数据。
 *          - env_sensor_status: 环境类传感器最近一次状态。
 *          - valid：环境检测结果有效性。
 *               valid = dht11_valid || lightsensor_valid
 */
typedef struct
{
    System_EnvData_t env_data;
    System_EnvSensorStatus_t env_sensor_status;
    uint8_t valid;
} System_EnvTaskData_t;

/*
 * @brief   System_SensorStatus_t: 传感器最近一次状态。
 * @note
 *          - env_sensor_status: 环境类传感器最近一次状态。
 *          - dir_sensor_status: 方向类传感器最近一次状态。
 */
typedef struct
{
   System_EnvSensorStatus_t env_sensor_status;
   System_DirectionSensorStatus_t dir_sensor_status;
} System_SensorStatus_t;


typedef enum
{
   UPLOAD_STATUS_OK = 0,
   UPLOAD_STATUS_FAIL
} UploadStatus_t;

 /*
 * @brief   System_UploadStatus_t: 数据上传状态
 * @note
 *          - wifi_connected: 连接WIFI
 *          - tcp_connected: 连接TCP
 *          - last_upload_status: 上次上传状态
 *          - upload_count: 上传成功次数
 *          - upload_fail_count: 上传失败次数
 */
typedef struct
{
   UploadStatus_t        wifi_connected;
   UploadStatus_t        tcp_connected;
   UploadStatus_t        last_upload_status;
   uint16_t              upload_count;
   uint16_t              upload_fail_count;
} System_UploadStatus_t;

/*
 * @brief   System_Mode_t: 系统运行模式
 * @note
 *          - SYSTEM_MODE_AUTO_SCAN: 自动固定角度扫描
 *          - SYSTEM_MODE_MANUAL_SERVO: 编码器手动控制舵机
 */
typedef enum
{
   SYSTEM_MODE_AUTO_SCAN = 0,
   SYSTEM_MODE_MANUAL_SERVO
} System_Mode_t;

/*
 * @brief   System_RuntimeData_t: 系统运行状态
 * @note
 *          - mode: 当前系统运行模式
 *          - mode_valid: 模式状态有效性
 *              1 ： 有效
 *              0 ： 无效
 *          - mode_changed: 本次运行态发布是否发生模式切换。
 *          - last_systemstatus: 上一次系统状态
 */
typedef struct
{
   System_Mode_t mode;
   uint8_t mode_valid;
   uint8_t mode_changed;
   AppSystem_Status_t last_systemstatus;
} System_RuntimeData_t;

/*
 * @brief   System_InputData_t: 编码器输入数据。
 * @note
 *          - encoder_data: 本轮编码器瞬时增量与按键状态。
 *          - encoder_total: InputTask 启动后累计旋转量，用于消费者按差值恢复完整输入。
 *          - encoder_status: 最近一次 Encoder 驱动状态。
 *          - valid: 输入数据是否有效。
 */
typedef struct
{
   Encoder_Data_t encoder_data;
   int32_t encoder_total;
   Encoder_Status_t encoder_status;
   uint8_t valid;
} System_InputTaskData_t;

/*
 * @brief   System_ManualTaskData_t: 手动控制任务数据。
 * @note
 *          - servo: 舵机数据。
 *          - distance: 距离数据。
 *          - thermal：热阵列数据。
 *          - detect: 人员检测结果。
 *          - direction_sensor_status：方向类传感器状态。
 *          - valid: 数据有效性。
 */
typedef struct
{
   System_ServoData_t servo;
   System_DistanceData_t distance;
   System_ThermalData_t thermal;
   System_DetectData_t detect;
   System_DirectionSensorStatus_t direction_sensor_status;
   uint8_t valid;
} System_ManualTaskData_t;


 /*
 * @brief   SystemTask_Data_t: SystemTask 运行态数据。
 * @note
 *          - runtime: 系统运行模式和运行状态。
 */
 typedef struct
 {
    System_RuntimeData_t runtime;
 } SystemTask_Data_t;




#endif /* __SYSTEM_DATA_H */
