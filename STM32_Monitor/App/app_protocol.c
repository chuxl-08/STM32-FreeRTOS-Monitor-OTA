#include "app_protocol.h"
#include <stdio.h>
#include "stm32f10x.h"

/*
 * 文件职责：
 * 1. 实现应用层上传数据和 OTA report 封装。
 * 2. 初期使用轻量键值字符串，便于 TCP Server 和串口日志直接观察。
 * 3. 后期如资源允许，可切换为简化 JSON 格式。
 *
 * 当前状态：
 * - 已实现 AppUpload_Data_t 到轻量键值上传字符串的打包。
 */

/*
 * @brief  将光照等级枚举转换为上传用字符串。
 * @param  level: 光照等级枚举值。
 * @retval const char *: 等级字符串。
 */
static const char *App_Protocol_LightLevelToString(LightSensor_Level_t level)
{
    switch(level)
    {
        case LIGHT_SENSOR_LEVEL_DARK:
            return "DARK";
        case LIGHT_SENSOR_LEVEL_DIM:
            return "DIM";
        case LIGHT_SENSOR_LEVEL_NORMAL:
            return "NORMAL";
        case LIGHT_SENSOR_LEVEL_BRIGHT:
            return "BRIGHT";
        default:
            return "UNKNOWN";
    }
}

/*
 * @brief  将系统运行模式转换为上传用字符串。
 * @param  mode: 系统运行模式。
 * @retval const char *: 模式字符串。
 */
static const char *App_Protocol_ModeToString(System_Mode_t mode)
{
    switch (mode)
    {
    case SYSTEM_MODE_AUTO_SCAN:
        return "AUTO_SCAN";
    case SYSTEM_MODE_MANUAL_SERVO:
        return "MANUAL";
    default:
        return "UNKNOWN";
    }
}

/*
 * @brief   OTA report 中的 Config 状态转上传字符串。
 * @param   report: OTA 上报快照。
 * @retval  const char *: Config 状态字符串；Config 不可用时返回 INVALID。
 */
static const char *App_Protocol_OtaStateString(const AppOtaDownload_Report_t *report)
{
    if((report == 0) || (report->cfg_valid == 0U))
    {
        return "INVALID";
    }

    return UpgradState_String(report->cfg_state);
}

/*
 * @brief   OTA report 中的路径转上传字符串。
 * @param   report: OTA 上报快照。
 * @retval  const char *: 最近请求路径；无路径时返回 DEFAULT。
 */
static const char *App_Protocol_OtaPathString(const AppOtaDownload_Report_t *report)
{
    if((report != 0) && (report->has_http_path != 0U))
    {
        return report->http_path;
    }

    return "DEFAULT";
}

/*
 * @brief   OTA report 中的目标 slot 转上传字符串。
 * @param   report: OTA 上报快照。
 * @retval  const char *: A/B/INVALID。
 */
static const char *App_Protocol_OtaTargetString(const AppOtaDownload_Report_t *report)
{
    if((report != 0) && (report->has_target_slot != 0U))
    {
        return AppOtaDownload_ReportSlotName(report->target_slot);
    }

    return "INVALID";
}

/*
 * @brief  将系统数据打包为 WiFi/TCP 上传字符串。
 * @param  upload_data: UploadTask 组合后的上传数据指针。
 * @param  buffer: 输出字符串缓冲区。
 * @param  buffer_size: 输出缓冲区大小，单位字节。
 * @retval AppProtocol_Status_t:
 *         - APP_PROTOCOL_OK: 打包成功。
 *         - APP_PROTOCOL_ERROR_PARAM: 输入参数错误。
 *         - APP_PROTOCOL_ERROR_BUFFER_SMALL: 输出缓冲区不足。
 * @note   不因单个传感器 invalid 而拒绝打包，而是上传对应 VALID 字段。
 *         当前上传内容包含：
 *         - MODE: 系统运行模式，用于区分自动扫描和手动单点检测数据。
 *         - TEMP/HUMI/LIGHT: 环境数据。
 *         - DIST/TMAX/TAVG/HOT/HUMAN: 当前方向检测数据。
 *         - ANGLE/SCAN_INDEX/SCAN_* / POINT_*: 自动扫描当前点和扫描汇总数据。
 *         - OTA_*: Bootloader Config 摘要、最近一次 OTA 请求和下载结果。
 */
AppProtocol_Status_t App_Protocol_BuildUploadString(const AppUpload_Data_t *upload_data,
                                                    char *buffer,
                                                    uint16_t buffer_size)
{
    int len;
    const DHT11_Data_t *dht11_data;
    const LightSensor_Data_t *light_data;
    const HCSR04_Data_t *hcsr04_data;
    const AMG8833_Data_t *amg8833_data;
    const System_RuntimeData_t *runtime_data;
    const AppOtaDownload_Report_t *ota_report;
    const AMG8833_Summary_t *amg8833_summary;
    const System_ScanPoint_t *scan_point = 0;
    const char *light_level_str;
    const char *system_mode_str;
    uint8_t current_human;
    uint8_t scan_angle;
    uint8_t scan_index;
    uint8_t scan_human;
    uint8_t scan_valid;
    

    if(upload_data == 0 || buffer == 0)
    {
        return APP_PROTOCOL_ERROR_PARAM;
    }

    if(buffer_size == 0)
    {
        return APP_PROTOCOL_ERROR_PARAM;
    }

    dht11_data = &upload_data->env.env_data.dht11_data;
    light_data = &upload_data->env.env_data.lightsensor_data;
    runtime_data = &upload_data->runtime;
    ota_report = upload_data->ota_report;
    light_level_str = App_Protocol_LightLevelToString(light_data->level);
    system_mode_str = App_Protocol_ModeToString(runtime_data->mode);

    if(runtime_data->mode == SYSTEM_MODE_AUTO_SCAN)
    {
        if(upload_data->scan == 0)
        {
            buffer[0] = '\0';
            return APP_PROTOCOL_ERROR_DATA_INVALID;
        }

        if(upload_data->scan->servo.current_index >= SYSTEM_SCAN_POINT_COUNT)
        {
            buffer[0] = '\0';
            return APP_PROTOCOL_ERROR_DATA_INVALID;
        }

        scan_point = &upload_data->scan->points[upload_data->scan->servo.current_index];
        hcsr04_data = &scan_point->point_distance.hcsr04_data;
        amg8833_data = &scan_point->point_thermal.amg8833_data;
        current_human = scan_point->point_detected.human_detected;
        scan_angle = upload_data->scan->servo.current_angle;
        scan_index = upload_data->scan->servo.current_index;
        scan_human = upload_data->scan->detect.human_detected;
        scan_valid = upload_data->scan->valid;
    }
    else if(runtime_data->mode == SYSTEM_MODE_MANUAL_SERVO)
    {
        if(upload_data->manual == 0)
        {
            buffer[0] = '\0';
            return APP_PROTOCOL_ERROR_DATA_INVALID;
        }

        hcsr04_data = &upload_data->manual->distance.hcsr04_data;
        amg8833_data = &upload_data->manual->thermal.amg8833_data;
        current_human = upload_data->manual->detect.human_detected;
        scan_angle = upload_data->manual->servo.current_angle;
        scan_index = upload_data->manual->servo.current_index;
        scan_human = 0;
        scan_valid = 0;
    }
    else
    {
        buffer[0] = '\0';
        return APP_PROTOCOL_ERROR_DATA_INVALID;
    }

    amg8833_summary = &amg8833_data->temp_summary;

    len = snprintf(buffer,
                   buffer_size,
                   "MODE=%s\r\n"
                   "LAST_SYSTEM_STATUS=%u\r\n"
                   "TEMP=%u,HUMI=%u,DHT_VALID=%u\r\n"
                   "LIGHT_RAW=%u,LIGHT_PERCENT=%u,LIGHT_LEVEL=%s,LIGHT_VALID=%u\r\n"
                   "DIST=%u,DIST_VALID=%u\r\n"
                   "TMAX=%d,TAVG=%d,HOT=%u,TMAX_ROW=%u,TMAX_COL=%u,THERMAL_VALID=%u\r\n"
                   "HUMAN=%u\r\n"
                   "ANGLE=%u,SCAN_INDEX=%u,SCAN_HUMAN=%u,SCAN_VALID=%u\r\n"
                   "POINT_DIST=%u,POINT_DVALID=%u,POINT_TMAX=%d,POINT_TAVG=%d,POINT_HOT=%u,POINT_HUMAN=%u,POINT_VALID=%u\r\n"
                   "OTA_STATE=%s,OTA_CONFIRMED=%s,OTA_PENDING=%s,OTA_BOOT=%s\r\n"
                   "OTA_SLOT_A_VER=%lu,OTA_SLOT_B_VER=%lu\r\n"
                   "OTA_LAST=%s,OTA_DL_STATUS=%s,OTA_TARGET=%s,OTA_PATH=%s,OTA_EXPECT_VER=%lu,OTA_PKG_VER=%lu\r\n"
                   ,

                   system_mode_str,  
                   (uint8_t)runtime_data->last_systemstatus,
                   (uint8_t)dht11_data->temperature,
                   (uint8_t)dht11_data->humidity,
                   (uint8_t)dht11_data->valid,
                   (uint16_t)light_data->raw,
                   (uint8_t)light_data->percent,
                   light_level_str,
                   (uint8_t)light_data->valid,
                   (uint16_t)hcsr04_data->distance_cm,
                   (uint8_t)hcsr04_data->valid,
                   (int16_t)amg8833_summary->max_temp_x100,
                   (int16_t)amg8833_summary->avg_temp_x100,
                   (uint8_t)amg8833_summary->hot_pixel_count,
                   (uint8_t)amg8833_summary->max_row,
                   (uint8_t)amg8833_summary->max_col,
                   (uint8_t)amg8833_data->valid,
                    current_human,
                    scan_angle,
                    scan_index,
                    scan_human,
                    scan_valid,
                    (uint16_t)((scan_point != 0) ? scan_point->point_distance.hcsr04_data.distance_cm : 0),
                    (uint8_t)((scan_point != 0) ? scan_point->point_distance.hcsr04_data.valid : 0),
                    (int16_t)((scan_point != 0) ? scan_point->point_thermal.amg8833_data.temp_summary.max_temp_x100 : 0),
                    (int16_t)((scan_point != 0) ? scan_point->point_thermal.amg8833_data.temp_summary.avg_temp_x100 : 0),
                    (uint8_t)((scan_point != 0) ? scan_point->point_thermal.amg8833_data.temp_summary.hot_pixel_count : 0),
                    (uint8_t)((scan_point != 0) ? scan_point->point_detected.human_detected : 0),
                    (uint8_t)((scan_point != 0) ? scan_point->point_valid : 0),
                    App_Protocol_OtaStateString(ota_report),
                    (ota_report != 0) ? AppOtaDownload_ReportSlotName(ota_report->confirmed_slot) : "INVALID",
                    (ota_report != 0) ? AppOtaDownload_ReportSlotName(ota_report->pending_slot) : "INVALID",
                    (ota_report != 0) ? AppOtaDownload_ReportSlotName(ota_report->boot_slot) : "INVALID",
                    (unsigned long)((ota_report != 0) ? ota_report->slot_a_version : 0U),
                    (unsigned long)((ota_report != 0) ? ota_report->slot_b_version : 0U),
                    (ota_report != 0) ? AppOtaDownload_ReportResultName(ota_report->result) : "NONE",
                    (ota_report != 0) ? AppOtaDownload_StatusName(ota_report->status) : "UNKNOWN",
                    App_Protocol_OtaTargetString(ota_report),
                    App_Protocol_OtaPathString(ota_report),
                    (unsigned long)(((ota_report != 0) && (ota_report->has_expected_version != 0U)) ?
                                    ota_report->expected_version : 0U),
                    (unsigned long)(((ota_report != 0) && (ota_report->has_package_info != 0U)) ?
                                    ota_report->package_version : 0U)
                   
                );

    if(len < 0)
    {
        buffer[0] = '\0';
        return APP_PROTOCOL_ERROR_PARAM;
    }

    if((uint16_t)len >= buffer_size)
    {
        buffer[0] = '\0';
        return APP_PROTOCOL_ERROR_BUFFER_SMALL;
    }

    return APP_PROTOCOL_OK;
}
