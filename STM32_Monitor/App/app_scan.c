#include "app_scan.h"
#include "servo.h"
#include "app_sensor.h"
#include "app_detect.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include "stm32f10x.h"

/*
 * 文件职责：
 * 1. 实现应用层固定角度扫描逻辑。
 * 2. 调用 Servo_SetAngle() 控制舵机转向。
 * 3. 舵机稳定后调用 app_sensor 方向采集接口读取 HC-SR04 和 AMG8833。
 * 4. 保存当前角度对应的距离、热阵列摘要和人员检测结果。
 * 5. 扫描过程中只发布过程快照，整轮完成后才发布 All 汇总结果。
 */

#define APP_SCAN_STABLE_DELAY_MS             1000

static const uint8_t s_scan_angles[SYSTEM_SCAN_POINT_COUNT] = {30, 60, 90, 120, 150};
static uint8_t s_scan_index;
/* 当前正在执行的扫描点序号，用于跨 WAIT_STABLE 周期保存目标点。 */
static uint8_t s_scan_active_index;
/* App_Scan_Update() 内部非阻塞状态机当前状态。 */
static AppScan_State_t s_scan_state;
/* 进入 WAIT_STABLE 状态时的 tick，用于判断舵机稳定等待是否完成。 */
static TickType_t s_scan_wait_start_tick;


const char *AppScanStatusName(AppScan_Status_t status)
{
    switch(status)
    {
        case APP_SCAN_OK:
            return "OK";
        case APP_SCAN_ERROR_PARAM:
            return "PARAM";
        case APP_SCAN_ERROR_INIT:
            return "INIT";
        case APP_SCAN_ERROR_SERVO:
            return "SERVO";
        case APP_SCAN_ERROR_SENSOR_READ:
            return "SENSOR_READ";
        default:
            return "UNKNOWN";
    }
}

/*
 * @brief   根据所有扫描点刷新多角度汇总人员检测结果。
 * @param   scan_data: 自动扫描数据指针。
 * @retval  None
 * @note    只有整轮扫描结束时才调用本函数。任意有效扫描点检测到人员，
 *          则整轮 All 结果为有人；至少存在一个有效点时 scan_data->valid 才置 1。
 */
static void App_Scan_UpdateSummary(System_ScanTaskData_t *scan_data)
{
    uint8_t index;
    uint8_t valid_count;

    valid_count = 0;
    scan_data->detect.human_detected = 0;

    for(index = 0; index < SYSTEM_SCAN_POINT_COUNT; index++)
    {
        if(scan_data->points[index].point_valid)
        {
            valid_count++;
            if(scan_data->points[index].point_detected.human_detected)
            {
                scan_data->detect.human_detected = 1;
            }
        }
    }
    
    scan_data->valid = (valid_count > 0) ? 1 : 0;
}

/*
 * @brief   开始新一轮自动扫描前清空整轮汇总状态。
 * @param   scan_data: 自动扫描数据指针。
 * @retval  None
 * @note    新一轮开始后，OLED 中 All 应显示 "--"，直到 SUMMARY 快照发布。
 *          单个点的距离/热阵列数据会在后续 READ_SENSOR 阶段重新写入。
 */
static void App_Scan_BeginRound(System_ScanTaskData_t *scan_data)
{
    uint8_t index;

    scan_data->valid = 0;
    scan_data->detect.human_detected = 0;

    for(index = 0; index < SYSTEM_SCAN_POINT_COUNT; index++)
    {
        scan_data->points[index].point_valid = 0;
        scan_data->points[index].point_detected.human_detected = 0;
    }
}


/*
 * @brief   初始化舵机扫描模块。
 * @param   system_scan_data: 自动扫描数据指针。
 * @retval  AppScan_Status_t:
 *          - APP_SCAN_OK: 初始化成功。
 *          - APP_SCAN_ERROR_PARAM: 参数为空。
 *          - APP_SCAN_ERROR_INIT: 舵机、HC-SR04 或 AMG8833 初始化失败。
 * @note    初始化扫描相关硬件：舵机和方向类传感器。
 */
AppScan_Status_t App_Scan_Init(System_ScanTaskData_t *system_scan_data)
{

    if(system_scan_data == 0)
    {
        return APP_SCAN_ERROR_PARAM;
    }

    memset(system_scan_data, 0, sizeof(*system_scan_data));

    s_scan_index = 0;
    s_scan_active_index = 0;
    s_scan_state = APP_SCAN_STATE_SET_ANGLE;
    s_scan_wait_start_tick = 0;

    if(Servo_Init() != SERVO_OK)
    {
        system_scan_data->servo.valid = 0;
        return APP_SCAN_ERROR_INIT;
    }

    if(App_Sensor_DirectionInit(&system_scan_data->direction_sensor_status) != APP_SENSOR_OK)
    {
        return APP_SCAN_ERROR_INIT;
    }

    system_scan_data->servo.current_angle = Servo_GetAngle();
    system_scan_data->servo.current_index = s_scan_index;
    system_scan_data->servo.valid = 1;

    return APP_SCAN_OK;
}

/*
 * @brief   非阻塞推进一次固定角度扫描状态机。
 * @param   system_scan_data: 自动扫描数据指针。
 * @param   snapshot_ready: 输出参数，取值为 APP_SCAN_SNAPSHOT_*。
 * @retval  AppScan_Status_t:
 *          - APP_SCAN_OK: 状态机推进成功，或当前仍处于舵机稳定等待阶段。
 *          - APP_SCAN_ERROR_PARAM: 参数为空。
 *          - APP_SCAN_ERROR_SERVO: 舵机角度设置失败。
 *          - APP_SCAN_ERROR_SENSOR_READ: HC-SR04 或 AMG8833 读取失败。
 * @note    调用方周期调用即可推进 30 -> 60 -> 90 -> 120 -> 150 的单轮扫描。
 *          WAIT_STABLE 状态通过 tick 判断等待时间，不再使用 Delay_ms() 阻塞 SystemTask。
 *          PROGRESS 快照表示扫描仍在进行，All 汇总保持无效；
 *          SUMMARY 快照表示整轮扫描完成，All 汇总已经可用。
 *          两轮扫描之间的等待由 ScanTask 根据 SCAN_TASK_SCAN_INTERVAL_MS 处理。
 */
AppScan_Status_t App_Scan_Update(System_ScanTaskData_t *system_scan_data, uint8_t *snapshot_ready)
{
    uint8_t angle;
    AppSensor_Status_t direction_status;
    AppDetect_Status_t detect_status;
    AppScan_Status_t scan_status;

    if(system_scan_data == 0 || snapshot_ready == 0)
    {
        return APP_SCAN_ERROR_PARAM;
    }

    scan_status = APP_SCAN_OK;
    *snapshot_ready = APP_SCAN_SNAPSHOT_NONE;

    switch (s_scan_state)
    {
        case APP_SCAN_STATE_SET_ANGLE:
        {
            s_scan_active_index = s_scan_index;
            angle = s_scan_angles[s_scan_active_index];

            if(s_scan_index == 0)
            {
                App_Scan_BeginRound(system_scan_data);
                *snapshot_ready = APP_SCAN_SNAPSHOT_PROGRESS;
            }

            if(Servo_SetAngle(angle) != SERVO_OK)
            {
                system_scan_data->servo.valid = 0;
                return APP_SCAN_ERROR_SERVO;
            }

            s_scan_wait_start_tick = xTaskGetTickCount();

            system_scan_data->servo.current_angle = Servo_GetAngle();
            system_scan_data->servo.current_index = s_scan_index;
            system_scan_data->servo.valid = 1;
            system_scan_data->points[s_scan_active_index].point_angle = system_scan_data->servo.current_angle;
            system_scan_data->points[s_scan_active_index].point_valid = 0;

            s_scan_state = APP_SCAN_STATE_WAIT_STABLE;
        }
        /* fall through */

        case APP_SCAN_STATE_WAIT_STABLE:
        {
            if((xTaskGetTickCount() - s_scan_wait_start_tick) >= pdMS_TO_TICKS(APP_SCAN_STABLE_DELAY_MS))
            {
                s_scan_state = APP_SCAN_STATE_READ_SENSOR;
            }
            else
            {
                return APP_SCAN_OK;
            }
        }
        /* fall through */

        case APP_SCAN_STATE_READ_SENSOR:
        {
            system_scan_data->points[s_scan_index].point_angle = system_scan_data->servo.current_angle;

            direction_status = App_Sensor_DirectionUpdate_Scan(
                &system_scan_data->points[s_scan_index], 
                &system_scan_data->direction_sensor_status);

            detect_status = App_Detect_Update(
                &system_scan_data->points[s_scan_index].point_distance, 
                &system_scan_data->points[s_scan_index].point_thermal,
                &system_scan_data->points[s_scan_index].point_detected);

            if((direction_status != APP_SENSOR_OK) || (detect_status != APP_DETECT_OK))
            {
                scan_status = APP_SCAN_ERROR_SENSOR_READ;
                system_scan_data->points[s_scan_index].point_valid = 0;
            }
            else
            {
                system_scan_data->points[s_scan_index].point_valid = 1;
            }

            s_scan_index++;
            if(s_scan_index >= SYSTEM_SCAN_POINT_COUNT)
            {
                s_scan_index = 0;
                s_scan_state = APP_SCAN_STATE_SUMMARY;
            }
            else
            {
               *snapshot_ready = APP_SCAN_SNAPSHOT_PROGRESS;
               s_scan_state = APP_SCAN_STATE_SET_ANGLE;
               return scan_status; 
            }
        }
        /* fall through */

        case APP_SCAN_STATE_SUMMARY:
        {
            App_Scan_UpdateSummary(system_scan_data);
            *snapshot_ready = APP_SCAN_SNAPSHOT_SUMMARY;
            s_scan_state = APP_SCAN_STATE_SET_ANGLE;
            return scan_status;
        }
        default:
        {
            s_scan_state = APP_SCAN_STATE_SET_ANGLE;
            return APP_SCAN_OK;
        }
    }
}
