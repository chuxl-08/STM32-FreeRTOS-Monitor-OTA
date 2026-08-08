#include "app_system.h"

/*
 * 文件职责：
 * 1. 提供系统运行状态和运行模式的日志字符串转换。
 * 2. SystemTask 的模式协调逻辑位于 app_task.c。
 */

const char *AppSystemStatusName(AppSystem_Status_t status)
{
    switch(status)
    {
        case APP_SYSTEM_OK:
            return "OK";
        case APP_SYSTEM_ERROR_PARAM:
            return "PARAM";
        case APP_SYSTEM_ERROR_ENCODER:
            return "ENCODER";
        case APP_SYSTEM_ERROR_INIT:
            return "INIT";
        case APP_SYSTEM_ERROR_SCAN:
            return "SCAN";
        case APP_SYSTEM_ERROR_SCAN_DISPLAY:
            return "SCAN_DISPLAY";
        case APP_SYSTEM_ERROR_MANUAL_SERVO_UPDATE:
            return "MANUAL_SERVO_UPDATE";
        case APP_SYSTEM_ERROR_MANUAL_DISPLAY_UPDATE:
            return "MANUAL_DISPLAY";
        case APP_SYSTEM_ERROR_MANUAL_SENSOR_UPDATE:
            return "MANUAL_SENSOR_UPDATE";
        case APP_SYSTEM_ERROR_AUTO:
            return "AUTO_ERR";
        case APP_SYSTEM_ERROR_MANUAL:
            return "MANUAL_ERR";
        case APP_SYSTEM_ERROR_UPLOAD:
            return "UPLOAD_ERR";
        default:
            return "UNKNOWN";
    }
}

const char *App_System_GetModeName(System_Mode_t mode)
{
    switch(mode)
    {
        case SYSTEM_MODE_AUTO_SCAN:
            return "AUTO";
        case SYSTEM_MODE_MANUAL_SERVO:
            return "MANUAL";
        default:
            return "UNKNOWN";
    }
}
