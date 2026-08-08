#ifndef __APP_UPLOAD_H
#define __APP_UPLOAD_H
#include "error_code.h"
#include "system_data.h"
#include "app_ota_download.h"

/*
 * 文件职责：
 * 1. 声明 ESP01 WiFi/TCP 上传应用层接口。
 * 2. 定义 UploadTask 聚合后的上传数据视图。
 * 3. 对 UploadTask 屏蔽 AT 指令、TCP 连接和协议打包细节。
 */

typedef struct
{
    System_RuntimeData_t runtime;
    System_EnvTaskData_t env;
    const System_ManualTaskData_t *manual;
    const System_ScanTaskData_t *scan;
    const AppOtaDownload_Report_t *ota_report;
} AppUpload_Data_t;

#define APP_UPLOAD_OTA_PATH_MAX_LEN  96U

/*
 * @brief   上传服务器返回给设备的轻量命令类型。
 * @note    当前只支持 OTA 请求。服务器 TCP payload 中包含 "OTA=1" 即表示触发一次 OTA。
 */
typedef enum
{
    APP_UPLOAD_SERVER_CMD_NONE = 0,
    APP_UPLOAD_SERVER_CMD_OTA_REQUEST
} AppUpload_ServerCommandType_t;

/*
 * @brief   上传服务器返回给设备的轻量命令。
 * @note    OTA 命令格式：
 *          OTA=1;PATH=/monitor_slot_b_full_v12.pkg;VER=12
 *          PATH/VER 是服务器建议值；App 仍会根据 Config 自己选择 inactive slot，
 *          并用固件包头校验最终写入目标，避免服务器直接指定写槽。
 */
typedef struct
{
    AppUpload_ServerCommandType_t type;
    uint8_t has_ota_path;
    char ota_path[APP_UPLOAD_OTA_PATH_MAX_LEN];
    uint8_t has_ota_version;
    uint32_t ota_version;
} AppUpload_ServerCommand_t;

const char *AppUploadStatusName(AppUpload_Status_t status);
AppUpload_Status_t App_Upload_Init(System_UploadStatus_t *upload_status);
AppUpload_Status_t App_Upload_Send(const AppUpload_Data_t *upload_data,
                                   System_UploadStatus_t *system_upload_status,
                                   AppUpload_ServerCommand_t *server_command);
AppUpload_Status_t App_Upload_Reconnect(System_UploadStatus_t *upload_status);


#endif /* __APP_UPLOAD_H */
