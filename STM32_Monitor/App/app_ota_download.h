#ifndef __APP_OTA_DOWNLOAD_H
#define __APP_OTA_DOWNLOAD_H

#include <stdint.h>
#include "memory_map.h"
#include "upgrade_config.h"

#define APP_OTA_DOWNLOAD_PATH_MAX_LEN  96U

/*
 * 文件职责：
 * 1. 对外声明 App 侧 ESP01 HTTP OTA 下载接口。
 * 2. 用统一状态码描述 Config、Slot、HTTP、Flash、Verify 等失败原因。
 * 3. ESP01 互斥由 app_task.c 的任务层统一管理，本模块只负责一次下载流程。
 */

/*
 * @brief   App 侧 OTA 下载状态码。
 * @note
 *          - OK：完整流程成功，正常情况下随后会软件复位。
 *          - STATE/SLOT/CONFIG：A/B 状态机前置条件不满足。
 *          - ESP01/SEND/TIMEOUT/HTTP：网络和协议链路错误。
 *          - FLASH/VERIFY/VERSION：写入、校验或版本闸门错误。
 */
typedef enum
{
    APP_OTA_DOWNLOAD_OK = 0,
    APP_OTA_DOWNLOAD_ERR_PARAM,
    APP_OTA_DOWNLOAD_ERR_CONFIG,
    APP_OTA_DOWNLOAD_ERR_STATE,
    APP_OTA_DOWNLOAD_ERR_SLOT,
    APP_OTA_DOWNLOAD_ERR_ESP01,
    APP_OTA_DOWNLOAD_ERR_SEND,
    APP_OTA_DOWNLOAD_ERR_TIMEOUT,
    APP_OTA_DOWNLOAD_ERR_OVERFLOW,
    APP_OTA_DOWNLOAD_ERR_HTTP,
    APP_OTA_DOWNLOAD_ERR_LENGTH,
    APP_OTA_DOWNLOAD_ERR_FLASH,
    APP_OTA_DOWNLOAD_ERR_VERIFY,
    APP_OTA_DOWNLOAD_ERR_VERSION
} AppOtaDownload_Status_t;

/*
 * @brief   一次 OTA 下载请求参数。
 * @note    OtaDownloadTask 从服务器命令、串口命令或 UI 入口收到请求后填充该结构。
 *          http_path 是服务器建议下载路径；target slot 仍由 App 根据 Config 选择。
 *          expected_version 用于下载后比对包头版本，避免服务器发错版本包。
 */
typedef struct
{
    uint8_t has_http_path;
    char http_path[APP_OTA_DOWNLOAD_PATH_MAX_LEN];
    uint8_t has_expected_version;
    uint32_t expected_version;
} AppOtaDownload_Request_t;

/*
 * @brief   App 侧 OTA 最近结果。
 * @note
 *          UploadTask 会把该结果拼进上传 payload，方便 PC/Server 判断当前设备
 *          是否已完成确认、正在下载，或上一次下载失败在 HTTP/FLASH/VERSION 等哪一步。
 */
typedef enum
{
    APP_OTA_REPORT_RESULT_NONE = 0,
    APP_OTA_REPORT_RESULT_CONFIRMED,
    APP_OTA_REPORT_RESULT_RUNNING,
    APP_OTA_REPORT_RESULT_PENDING_RESET,
    APP_OTA_REPORT_RESULT_FAILED
} AppOtaDownload_ReportResult_t;

/*
 * @brief   OTA 上报快照。
 * @note    cfg_* 字段来自 Bootloader Config；request/pkg 字段来自最近一次 App 下载尝试。
 *          复位后 RAM 中的 request/pkg 会丢失，但 cfg_* 仍能说明当前 confirmed slot/version。
 */
typedef struct
{
    uint8_t cfg_valid;
    UpgradeState_t cfg_state;
    BootSlot_t confirmed_slot;
    BootSlot_t pending_slot;
    BootSlot_t boot_slot;
    uint32_t slot_a_version;
    uint32_t slot_b_version;

    AppOtaDownload_ReportResult_t result;
    AppOtaDownload_Status_t status;
    uint8_t has_target_slot;
    BootSlot_t target_slot;

    uint8_t has_http_path;
    char http_path[APP_OTA_DOWNLOAD_PATH_MAX_LEN];
    uint8_t has_expected_version;
    uint32_t expected_version;

    uint8_t has_package_info;
    uint32_t package_version;
    uint32_t image_size;
    uint32_t image_crc32;
} AppOtaDownload_Report_t;

/*
 * @brief   OTA 下载状态码转字符串。
 */
const char *AppOtaDownload_StatusName(AppOtaDownload_Status_t status);

/*
 * @brief   OTA report 结果转字符串。
 */
const char *AppOtaDownload_ReportResultName(AppOtaDownload_ReportResult_t result);

/*
 * @brief   BootSlot_t 转 OTA report 字符串。
 */
const char *AppOtaDownload_ReportSlotName(BootSlot_t slot);

/*
 * @brief   记录一次 OTA 请求开始。
 */
void AppOtaDownload_ReportStart(const AppOtaDownload_Request_t *request);

/*
 * @brief   更新本次 OTA 目标 slot。
 */
void AppOtaDownload_ReportTarget(BootSlot_t target_slot);

/*
 * @brief   更新本次 OTA 下载到的包头摘要。
 */
void AppOtaDownload_ReportPackage(const FirmwareHeader_t *header);

/*
 * @brief   记录一次 OTA 请求结束状态。
 */
void AppOtaDownload_ReportFinish(AppOtaDownload_Status_t status);

/*
 * @brief   获取 OTA 上报快照。
 */
void AppOtaDownload_GetReport(AppOtaDownload_Report_t *report);

/*
 * @brief   执行一次 ESP01 HTTP OTA 下载到 inactive slot。
 * @note    成功保存 pending 后会触发 NVIC_SystemReset()，正常不返回。
 */
AppOtaDownload_Status_t AppOtaDownload_RunOnce(const AppOtaDownload_Request_t *request);

#endif /* __APP_OTA_DOWNLOAD_H */
