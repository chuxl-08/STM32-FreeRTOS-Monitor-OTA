#include "app_ota_download.h"
#include "app_ota_config.h"
#include "app_log.h"
#include "atk_esp01.h"
#include "at_parser.h"
#include "bsp_usart.h"
#include "flash_if.h"
#include "firmware.h"
#include "firmware_verify.h"
#include "memory_map.h"
#include "upgrade_config_if.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>

/*
 * 文件职责：
 * 1. 在 STM32_Monitor App 侧通过 ESP01 HTTP 下载 Bootloader .pkg 固件包。
 * 2. 根据 A/B Config 选择 inactive slot，并将 raw image 直接写入目标 slot。
 * 3. 保存 PENDING_VERIFY 后软件复位，由 Bootloader 完成 TESTING/CONFIRMED/rollback。
 *
 * 当前边界：
 * - 完整业务任务集合中的阻塞式 App 侧 OTA 实现。
 * - HTTP 和 ESP01 +IPD 解析沿用 Bootloader/OTA 阶段验证的轻量协议思路，
 *   并按当前 A/B Slot 主线收敛到 inactive slot 写入流程。
 * - 不使用旧 48KB RAM pkg 缓冲，而是 Header 进 RAM、Image 分块写 Flash。
 * - ESP01 互斥由 OtaDownloadTask 在 app_task.c 中持有。
 */

#define APP_OTA_HTTP_REQ_BUFFER_SIZE       192U
#define APP_OTA_AT_CMD_BUFFER_SIZE         32U
#define APP_OTA_HTTP_HEADER_BUFFER_SIZE    768U
#define APP_OTA_FLASH_CHUNK_SIZE           256U
#define APP_OTA_RAW_BYTE_TIMEOUT_MS        10000U
#define APP_OTA_SEND_PROMPT_TIMEOUT_MS     5000U
#define APP_OTA_MAX_IPD_PAYLOAD            (SLOT_A_SIZE + sizeof(FirmwareHeader_t))

typedef struct
{
    /*
     * 当前 +IPD 帧中尚未被上层消费的 TCP payload 字节数。
     * 归零后需要重新等待下一个 "+IPD,<len>:" 帧头。
     */
    uint32_t payload_remain;
} AppOtaDownload_IpdReader_t;

/*
 * @brief   OTA 上报快照。
 */
static AppOtaDownload_Report_t s_ota_download_report;

/*
 * @brief   OTA 下载状态码转日志字符串。
 * @param   status: AppOtaDownload_Status_t 状态码。
 * @retval  const char *: 简短状态名，供 USART1 日志打印。
 */
const char *AppOtaDownload_StatusName(AppOtaDownload_Status_t status)
{
    switch(status)
    {
        case APP_OTA_DOWNLOAD_OK:
            return "OK";
        case APP_OTA_DOWNLOAD_ERR_PARAM:
            return "PARAM";
        case APP_OTA_DOWNLOAD_ERR_CONFIG:
            return "CONFIG";
        case APP_OTA_DOWNLOAD_ERR_STATE:
            return "STATE";
        case APP_OTA_DOWNLOAD_ERR_SLOT:
            return "SLOT";
        case APP_OTA_DOWNLOAD_ERR_ESP01:
            return "ESP01";
        case APP_OTA_DOWNLOAD_ERR_SEND:
            return "SEND";
        case APP_OTA_DOWNLOAD_ERR_TIMEOUT:
            return "TIMEOUT";
        case APP_OTA_DOWNLOAD_ERR_OVERFLOW:
            return "OVERFLOW";
        case APP_OTA_DOWNLOAD_ERR_HTTP:
            return "HTTP";
        case APP_OTA_DOWNLOAD_ERR_LENGTH:
            return "LENGTH";
        case APP_OTA_DOWNLOAD_ERR_FLASH:
            return "FLASH";
        case APP_OTA_DOWNLOAD_ERR_VERIFY:
            return "VERIFY";
        case APP_OTA_DOWNLOAD_ERR_VERSION:
            return "VERSION";
        default:
            return "UNKNOWN";
    }
}

/*
 * @brief   OTA report 结果转日志/上传字符串。
 * @param   result: AppOtaDownload_ReportResult_t 结果。
 * @retval  const char *: 简短结果名。
 */
const char *AppOtaDownload_ReportResultName(AppOtaDownload_ReportResult_t result)
{
    switch(result)
    {
        case APP_OTA_REPORT_RESULT_NONE:
            return "NONE";
        case APP_OTA_REPORT_RESULT_CONFIRMED:
            return "CONFIRMED";
        case APP_OTA_REPORT_RESULT_RUNNING:
            return "RUNNING";
        case APP_OTA_REPORT_RESULT_PENDING_RESET:
            return "PENDING_RESET";
        case APP_OTA_REPORT_RESULT_FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}

/*
 * @brief   Slot 枚举转日志字符串。
 * @param   slot: Bootloader A/B slot 枚举。
 * @retval  "A" / "B" / "INVALID"。
 */
const char *AppOtaDownload_ReportSlotName(BootSlot_t slot)
{
    switch(slot)
    {
        case BOOT_SLOT_A:
            return "A";
        case BOOT_SLOT_B:
            return "B";
        default:
            return "INVALID";
    }
}

/*
 * @brief   更新 OTA report 中的 Config 快照。
 * @param   report: 待更新的 report。
 * @retval  无。
 * @note    Config 是 Flash 持久状态，复位后仍能说明当前 confirmed slot/version。
 */
static void AppOtaDownload_FillConfigReport(AppOtaDownload_Report_t *report)
{
    UpgradeConfig_t cfg;
    UpgradeConfigIfErrorCode_t cfg_status;

    if(report == 0)
    {
        return;
    }

    cfg_status = UpgradeConfig_Load(&cfg);
    if(cfg_status != UpgradeConfigIf_OK)
    {
        report->cfg_valid = 0U;
        return;
    }

    report->cfg_valid = 1U;
    report->cfg_state = cfg.state;
    report->confirmed_slot = cfg.confirmed_slot;
    report->pending_slot = cfg.pending_slot;
    report->boot_slot = cfg.boot_slot;
    report->slot_a_version = cfg.slot_a_version;
    report->slot_b_version = cfg.slot_b_version;
}

/*
 * @brief   记录一次 OTA 请求开始。
 * @param   request: 本次请求参数；为空表示使用默认路径/版本。
 * @retval  无。
 * @note    请求开始后 UploadTask 后续 payload 会显示 OTA_LAST=RUNNING。
 */
void AppOtaDownload_ReportStart(const AppOtaDownload_Request_t *request)
{
    taskENTER_CRITICAL();
    memset(&s_ota_download_report, 0, sizeof(s_ota_download_report));
    s_ota_download_report.result = APP_OTA_REPORT_RESULT_RUNNING;
    s_ota_download_report.status = APP_OTA_DOWNLOAD_OK;

    if(request != 0)
    {
        if(request->has_http_path != 0U)
        {
            (void)snprintf(s_ota_download_report.http_path,
                           sizeof(s_ota_download_report.http_path),
                           "%s",
                           request->http_path);
            s_ota_download_report.has_http_path = 1U;
        }

        if(request->has_expected_version != 0U)
        {
            s_ota_download_report.has_expected_version = 1U;
            s_ota_download_report.expected_version = request->expected_version;
        }
    }
    taskEXIT_CRITICAL();
}

/*
 * @brief   更新本次 OTA 目标 slot。
 * @param   target_slot: 当前下载目标 slot。
 * @retval  无。
 */
void AppOtaDownload_ReportTarget(BootSlot_t target_slot)
{
    taskENTER_CRITICAL();
    s_ota_download_report.has_target_slot = 1U;
    s_ota_download_report.target_slot = target_slot;
    taskEXIT_CRITICAL();
}

/*
 * @brief   更新本次 OTA 下载到的包头摘要。
 * @param   header: 已通过基础校验的固件包头。
 * @retval  无。
 */
void AppOtaDownload_ReportPackage(const FirmwareHeader_t *header)
{
    if(header == 0)
    {
        return;
    }

    taskENTER_CRITICAL();
    s_ota_download_report.has_package_info = 1U;
    s_ota_download_report.package_version = header->image_version;
    s_ota_download_report.image_size = header->image_size;
    s_ota_download_report.image_crc32 = header->image_crc32;
    taskEXIT_CRITICAL();
}

/*
 * @brief   记录一次 OTA 请求结束状态。
 * @param   status: AppOtaDownload_RunOnce() 返回状态。
 * @retval  无。
 */
void AppOtaDownload_ReportFinish(AppOtaDownload_Status_t status)
{
    taskENTER_CRITICAL();
    s_ota_download_report.status = status;
    s_ota_download_report.result = (status == APP_OTA_DOWNLOAD_OK) ?
                                   APP_OTA_REPORT_RESULT_PENDING_RESET :
                                   APP_OTA_REPORT_RESULT_FAILED;
    taskEXIT_CRITICAL();
}

/*
 * @brief   获取 OTA 上报快照。
 * @param   report: 输出 report。
 * @retval  无。
 * @note    每次读取都会刷新 Config 字段；若没有下载失败记录且 Config 为 CONFIRMED，
 *          则把 OTA_LAST 视为 CONFIRMED，方便复位后的新 App 上报确认态。
 */
void AppOtaDownload_GetReport(AppOtaDownload_Report_t *report)
{
    if(report == 0)
    {
        return;
    }

    taskENTER_CRITICAL();
    *report = s_ota_download_report;
    taskEXIT_CRITICAL();

    AppOtaDownload_FillConfigReport(report);

    if((report->result == APP_OTA_REPORT_RESULT_NONE) &&
       (report->cfg_valid != 0U) &&
       (report->cfg_state == UPGRADE_STATE_CONFIRMED))
    {
        report->result = APP_OTA_REPORT_RESULT_CONFIRMED;
        report->status = APP_OTA_DOWNLOAD_OK;
    }
}

/*
 * @brief   Slot 枚举转日志字符串。
 * @param   slot: Bootloader A/B slot 枚举。
 * @retval  "A" / "B" / "INVALID"。
 */
static const char *AppOtaDownload_SlotName(BootSlot_t slot)
{
    return AppOtaDownload_ReportSlotName(slot);
}

/*
 * @brief   根据目标 slot 选择 HTTP pkg 路径。
 * @param   slot: 本次要写入的 inactive slot。
 * @retval  const char *: HTTP GET path；slot 无效时返回 0。
 * @note    Slot A 包必须由 firmware_pack.py --slot a 生成，Slot B 同理。
 */
static const char *AppOtaDownload_PathForSlot(BootSlot_t slot)
{
    if(slot == BOOT_SLOT_A)
    {
        return APP_OTA_HTTP_PATH_SLOT_A;
    }

    if(slot == BOOT_SLOT_B)
    {
        return APP_OTA_HTTP_PATH_SLOT_B;
    }

    return 0;
}

/*
 * @brief   检查服务器下发的 HTTP path 是否可用于 ESP01 HTTP GET。
 * @param   path: 待检查路径。
 * @retval  1: path 合法；0: path 为空、过长或包含不安全字符。
 * @note    只接受以 '/' 开头的相对服务器路径，不接受 URL、空格、控制字符或 ".."。
 */
static uint8_t AppOtaDownload_IsSafeHttpPath(const char *path)
{
    uint32_t index;

    if((path == 0) || (path[0] != '/'))
    {
        return 0U;
    }

    if(strstr(path, "..") != 0)
    {
        return 0U;
    }

    if(strstr(path, "://") != 0)
    {
        return 0U;
    }

    for(index = 0U; path[index] != '\0'; index++)
    {
        if(index >= (APP_OTA_DOWNLOAD_PATH_MAX_LEN - 1U))
        {
            return 0U;
        }

        if((path[index] <= ' ') || (path[index] >= 0x7f))
        {
            return 0U;
        }
    }

    return (index > 1U) ? 1U : 0U;
}

/*
 * @brief   从 Config 中读取指定 slot 的记录版本号。
 * @param   cfg: 已加载的 UpgradeConfig_t。
 * @param   slot: 待查询版本的 slot。
 * @retval  uint32_t: slot 版本号；参数异常时返回 0。
 * @note    用于同版本/低版本跳过，避免反复下载同一个固件包后重启。
 */
static uint32_t AppOtaDownload_VersionForSlot(const UpgradeConfig_t *cfg, BootSlot_t slot)
{
    if(cfg == 0)
    {
        return 0U;
    }

    if(slot == BOOT_SLOT_A)
    {
        return cfg->slot_a_version;
    }

    if(slot == BOOT_SLOT_B)
    {
        return cfg->slot_b_version;
    }

    return 0U;
}

/*
 * @brief   将 BootSlot_t 转为 FlashIf 的逻辑擦写区域。
 * @param   slot: 目标 A/B slot。
 * @param   region: 输出 FlashIf 区域。
 * @retval  APP_OTA_DOWNLOAD_OK: 转换成功。
 *          APP_OTA_DOWNLOAD_ERR_PARAM: region 为空。
 *          APP_OTA_DOWNLOAD_ERR_SLOT: slot 非法。
 */
static AppOtaDownload_Status_t AppOtaDownload_SlotToFlashRegion(BootSlot_t slot,
                                                                 FlashRegion_t *region)
{
    if(region == 0)
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    if(slot == BOOT_SLOT_A)
    {
        *region = FLASH_REGION_SLOT_A;
        return APP_OTA_DOWNLOAD_OK;
    }

    if(slot == BOOT_SLOT_B)
    {
        *region = FLASH_REGION_SLOT_B;
        return APP_OTA_DOWNLOAD_OK;
    }

    return APP_OTA_DOWNLOAD_ERR_SLOT;
}

/*
 * @brief   检查 OTA 网络参数是否仍为占位值。
 * @retval  1: 仍是占位配置，应跳过 OTA。
 *          0: 配置看起来已经被替换为本地上板参数。
 * @note    避免忘记修改 WiFi/HTTP 地址时反复阻塞 ESP01 初始化和连接流程。
 */
static uint8_t AppOtaDownload_ConfigLooksPlaceholder(void)
{
    if((strcmp(APP_OTA_WIFI_SSID, "YOUR_WIFI_SSID") == 0) ||
       (strcmp(APP_OTA_WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") == 0) ||
       (strcmp(APP_OTA_HTTP_HOST, "192.168.x.x") == 0) ||
       (strcmp(APP_OTA_HTTP_HOST, "192.168.1.100") == 0))
    {
        return 1U;
    }

    return 0U;
}

/*
 * @brief   读取 Config 并选择本次 OTA 的 inactive slot。
 * @param   cfg: 输出当前 Config RAM 副本。
 * @param   target_slot: 输出本次下载目标 slot。
 * @retval  AppOtaDownload_Status_t: 成功或具体失败原因。
 * @note
 *          只有 CONFIRMED 稳定态才允许 App 侧发起新下载。
 *          同时检查 APP_VECTOR_BASE_ADDR 对应的当前运行 slot 必须等于 confirmed_slot，
 *          避免一个未确认/地址不匹配的 App 继续写另一个 slot。
 */
static AppOtaDownload_Status_t AppOtaDownload_SelectTarget(UpgradeConfig_t *cfg,
                                                           BootSlot_t *target_slot)
{
    UpgradeConfigIfErrorCode_t cfg_status;
    BootSlot_t current_slot;

    if((cfg == 0) || (target_slot == 0))
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    cfg_status = UpgradeConfig_Load(cfg);
    if(cfg_status != UpgradeConfigIf_OK)
    {
        App_LogPrintf("[OTA-DL] cfg load fail %s\r\n",
                      UpgradeConfigIfErrorCode_String(cfg_status));
        return APP_OTA_DOWNLOAD_ERR_CONFIG;
    }

    if(cfg->state != UPGRADE_STATE_CONFIRMED)
    {
        App_LogPrintf("[OTA-DL] skip state=%s\r\n",
                      UpgradState_String(cfg->state));
        return APP_OTA_DOWNLOAD_ERR_STATE;
    }

    if(MemoryMap_IsSlotValid(cfg->confirmed_slot) == 0U)
    {
        App_LogPrintf("[OTA-DL] confirmed slot invalid\r\n");
        return APP_OTA_DOWNLOAD_ERR_SLOT;
    }

    current_slot = MemoryMap_SlotFromLoadAddress(APP_VECTOR_BASE_ADDR);
    if((MemoryMap_IsSlotValid(current_slot) == 0U) ||
       (current_slot != cfg->confirmed_slot))
    {
        App_LogPrintf("[OTA-DL] current slot=%s cfg confirmed=%s mismatch\r\n",
                      AppOtaDownload_SlotName(current_slot),
                      AppOtaDownload_SlotName(cfg->confirmed_slot));
        return APP_OTA_DOWNLOAD_ERR_SLOT;
    }

    *target_slot = MemoryMap_OtherSlot(cfg->confirmed_slot);
    if(MemoryMap_IsSlotValid(*target_slot) == 0U)
    {
        return APP_OTA_DOWNLOAD_ERR_SLOT;
    }

    return APP_OTA_DOWNLOAD_OK;
}

/*
 * @brief   从 USART3 RingBuffer 中带超时读取 1 个 ESP01 原始字节。
 * @param   byte: 输出字节指针。
 * @param   timeout_ms: 超时时间，单位 ms。
 * @retval  APP_OTA_DOWNLOAD_OK: 读取成功。
 *          APP_OTA_DOWNLOAD_ERR_PARAM: byte 为空。
 *          APP_OTA_DOWNLOAD_ERR_TIMEOUT: 超时无数据。
 * @note    读到的可能是 AT 响应、+IPD 帧头、HTTP header 或 body 字节。
 */
static AppOtaDownload_Status_t AppOtaDownload_ReadRawByte(uint8_t *byte,
                                                          uint32_t timeout_ms)
{
    TickType_t start_tick;
    TickType_t timeout_tick;

    if(byte == 0)
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    start_tick = xTaskGetTickCount();
    timeout_tick = pdMS_TO_TICKS(timeout_ms);

    while((xTaskGetTickCount() - start_tick) < timeout_tick)
    {
        if(BSP_USART3_ReadByte(byte) == BSP_USART_OK)
        {
            return APP_OTA_DOWNLOAD_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(1U));
    }

    return APP_OTA_DOWNLOAD_ERR_TIMEOUT;
}

/*
 * @brief   等待 ESP01 的 "+IPD,<len>:" 帧头。
 * @param   reader: IPD reader 状态。
 * @retval  AppOtaDownload_Status_t: 成功或解析/超时错误。
 * @note    只解析帧头并记录 payload_remain，真正 payload 字节由后续读取函数消费。
 */
static AppOtaDownload_Status_t AppOtaDownload_WaitIpdHeader(AppOtaDownload_IpdReader_t *reader)
{
    static const char ipd_prefix[] = "+IPD,";
    uint8_t byte;
    uint8_t match_index = 0U;
    uint32_t payload_size = 0U;
    AppOtaDownload_Status_t status;

    if(reader == 0)
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    while(match_index < (sizeof(ipd_prefix) - 1U))
    {
        status = AppOtaDownload_ReadRawByte(&byte, APP_OTA_RAW_BYTE_TIMEOUT_MS);
        if(status != APP_OTA_DOWNLOAD_OK)
        {
            return status;
        }

        if(byte == (uint8_t)ipd_prefix[match_index])
        {
            match_index++;
        }
        else
        {
            match_index = (byte == (uint8_t)ipd_prefix[0]) ? 1U : 0U;
        }
    }

    while(1)
    {
        status = AppOtaDownload_ReadRawByte(&byte, APP_OTA_RAW_BYTE_TIMEOUT_MS);
        if(status != APP_OTA_DOWNLOAD_OK)
        {
            return status;
        }

        if((byte >= (uint8_t)'0') && (byte <= (uint8_t)'9'))
        {
            payload_size = payload_size * 10U + (uint32_t)(byte - (uint8_t)'0');
            if(payload_size > APP_OTA_MAX_IPD_PAYLOAD)
            {
                return APP_OTA_DOWNLOAD_ERR_OVERFLOW;
            }
        }
        else if(byte == (uint8_t)':')
        {
            if(payload_size == 0U)
            {
                return APP_OTA_DOWNLOAD_ERR_HTTP;
            }

            reader->payload_remain = payload_size;
            return APP_OTA_DOWNLOAD_OK;
        }
        else
        {
            return APP_OTA_DOWNLOAD_ERR_HTTP;
        }
    }
}

/*
 * @brief   从 ESP01 IPD 数据流读取 1 个 TCP payload 字节。
 * @param   reader: IPD reader 状态。
 * @param   byte: 输出 payload 字节。
 * @retval  AppOtaDownload_Status_t: 成功或具体失败原因。
 * @note    屏蔽 "+IPD,<len>:" 包装，让 HTTP 层看到连续字节流。
 */
static AppOtaDownload_Status_t AppOtaDownload_ReadPayloadByte(AppOtaDownload_IpdReader_t *reader,
                                                              uint8_t *byte)
{
    AppOtaDownload_Status_t status;

    if((reader == 0) || (byte == 0))
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    if(reader->payload_remain == 0U)
    {
        status = AppOtaDownload_WaitIpdHeader(reader);
        if(status != APP_OTA_DOWNLOAD_OK)
        {
            return status;
        }
    }

    status = AppOtaDownload_ReadRawByte(byte, APP_OTA_RAW_BYTE_TIMEOUT_MS);
    if(status != APP_OTA_DOWNLOAD_OK)
    {
        return status;
    }

    reader->payload_remain--;
    return APP_OTA_DOWNLOAD_OK;
}

/*
 * @brief   从 TCP payload 流中连续读取指定长度数据。
 * @param   reader: IPD reader 状态。
 * @param   buffer: 输出缓冲区。
 * @param   size: 需要读取的字节数。
 * @retval  AppOtaDownload_Status_t: 成功或具体失败原因。
 */
static AppOtaDownload_Status_t AppOtaDownload_ReadPayload(AppOtaDownload_IpdReader_t *reader,
                                                          uint8_t *buffer,
                                                          uint32_t size)
{
    uint32_t offset;
    AppOtaDownload_Status_t status;

    if((reader == 0) || (buffer == 0))
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    for(offset = 0U; offset < size; offset++)
    {
        status = AppOtaDownload_ReadPayloadByte(reader, &buffer[offset]);
        if(status != APP_OTA_DOWNLOAD_OK)
        {
            return status;
        }
    }

    return APP_OTA_DOWNLOAD_OK;
}

/*
 * @brief   从 HTTP header 字符串中解析 Content-Length。
 * @param   header: 以 '\0' 结尾的 HTTP header。
 * @param   content_length: 输出 body 长度。
 * @retval  1: 解析成功。
 *          0: 未找到字段或格式错误。
 */
static uint8_t AppOtaDownload_ParseContentLength(const char *header,
                                                 uint32_t *content_length)
{
    const char *field;
    uint32_t value = 0U;
    uint8_t has_digit = 0U;

    if((header == 0) || (content_length == 0))
    {
        return 0U;
    }

    field = strstr(header, "Content-Length:");
    if(field == 0)
    {
        return 0U;
    }

    field += strlen("Content-Length:");
    while((*field == ' ') || (*field == '\t'))
    {
        field++;
    }

    while((*field >= '0') && (*field <= '9'))
    {
        has_digit = 1U;
        value = value * 10U + (uint32_t)(*field - '0');
        field++;
    }

    if(has_digit == 0U)
    {
        return 0U;
    }

    *content_length = value;
    return 1U;
}

/*
 * @brief   发送 HTTP GET 请求但不消费 HTTP 响应内容。
 * @param   host: HTTP Server IP 或域名。
 * @param   port: HTTP Server 端口。
 * @param   path: 固件包路径。
 * @retval  AppOtaDownload_Status_t: 成功或发送失败原因。
 * @note    这里不能用会等待 "HTTP/1." 的封装，否则会提前吃掉响应流。
 */
static AppOtaDownload_Status_t AppOtaDownload_SendHttpGet(const char *host,
                                                          uint16_t port,
                                                          const char *path)
{
    char http_req[APP_OTA_HTTP_REQ_BUFFER_SIZE];
    char cmd[APP_OTA_AT_CMD_BUFFER_SIZE];
    int http_len;
    int cmd_len;

    if((host == 0) || (path == 0))
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    http_len = snprintf(http_req,
                        sizeof(http_req),
                        "GET %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
                        path,
                        host,
                        (unsigned int)port);
    if((http_len < 0) || ((uint32_t)http_len >= sizeof(http_req)))
    {
        return APP_OTA_DOWNLOAD_ERR_OVERFLOW;
    }

    cmd_len = snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u\r\n", (unsigned int)strlen(http_req));
    if((cmd_len < 0) || ((uint32_t)cmd_len >= sizeof(cmd)))
    {
        return APP_OTA_DOWNLOAD_ERR_OVERFLOW;
    }

    if(AT_Parser_SendAndWait(cmd, ">", APP_OTA_SEND_PROMPT_TIMEOUT_MS) != AT_PARSER_OK)
    {
        return APP_OTA_DOWNLOAD_ERR_SEND;
    }

    if(BSP_USART3_SendString(http_req) != BSP_USART_OK)
    {
        return APP_OTA_DOWNLOAD_ERR_SEND;
    }

    return APP_OTA_DOWNLOAD_OK;
}

/*
 * @brief   读取并解析 HTTP 响应头。
 * @param   reader: IPD reader 状态。
 * @param   content_length: 输出 HTTP body 长度。
 * @retval  AppOtaDownload_Status_t: 成功或 HTTP/长度/溢出错误。
 * @note    当前只接受 HTTP/1.x 200 响应和标准 Content-Length。
 */
static AppOtaDownload_Status_t AppOtaDownload_ReadHttpHeader(AppOtaDownload_IpdReader_t *reader,
                                                             uint32_t *content_length)
{
    static const char header_end[] = "\r\n\r\n";
    char header[APP_OTA_HTTP_HEADER_BUFFER_SIZE];
    uint32_t header_len = 0U;
    uint32_t end_match = 0U;
    uint8_t byte;
    AppOtaDownload_Status_t status;

    if((reader == 0) || (content_length == 0))
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    memset(header, 0, sizeof(header));

    while(end_match < 4U)
    {
        status = AppOtaDownload_ReadPayloadByte(reader, &byte);
        if(status != APP_OTA_DOWNLOAD_OK)
        {
            return status;
        }

        if(header_len >= (sizeof(header) - 1U))
        {
            return APP_OTA_DOWNLOAD_ERR_OVERFLOW;
        }

        header[header_len++] = (char)byte;
        header[header_len] = '\0';

        if(byte == (uint8_t)header_end[end_match])
        {
            end_match++;
        }
        else
        {
            end_match = (byte == (uint8_t)header_end[0]) ? 1U : 0U;
        }
    }

    if((strstr(header, "HTTP/1.") == 0) || (strstr(header, " 200 ") == 0))
    {
        return APP_OTA_DOWNLOAD_ERR_HTTP;
    }

    if(AppOtaDownload_ParseContentLength(header, content_length) == 0U)
    {
        return APP_OTA_DOWNLOAD_ERR_HTTP;
    }

    App_LogPrintf("[OTA-DL] HTTP header OK length=%lu\r\n",
                  (unsigned long)*content_length);

    return APP_OTA_DOWNLOAD_OK;
}

/*
 * @brief   擦除本次 OTA 目标 slot。
 * @param   target_slot: inactive slot。
 * @retval  AppOtaDownload_Status_t: 成功或 Flash/slot 错误。
 * @note    先擦除再发 HTTP GET，避免擦 Flash 期间 USART3 接收溢出。
 */
static AppOtaDownload_Status_t AppOtaDownload_EraseTarget(BootSlot_t target_slot)
{
    FlashRegion_t target_region;
    FlashIfErrorCode_t flash_status;
    AppOtaDownload_Status_t status;

    status = AppOtaDownload_SlotToFlashRegion(target_slot, &target_region);
    if(status != APP_OTA_DOWNLOAD_OK)
    {
        return status;
    }

    flash_status = FlashIf_EraseRegion(target_region, 0U, MemoryMap_SlotSize(target_slot));
    if(flash_status != FLASHIF_OK)
    {
        App_LogPrintf("[OTA-DL] erase slot %s fail %s\r\n",
                      AppOtaDownload_SlotName(target_slot),
                      FlashIfErrorCode_String(flash_status));
        return APP_OTA_DOWNLOAD_ERR_FLASH;
    }

    App_LogPrintf("[OTA-DL] erase slot %s OK\r\n",
                  AppOtaDownload_SlotName(target_slot));
    return APP_OTA_DOWNLOAD_OK;
}

/*
 * @brief   分块接收 raw image 并直接写入目标 slot。
 * @param   reader: IPD reader 状态。
 * @param   header: 已通过 slot header 校验的固件包头。
 * @param   target_slot: inactive slot。
 * @retval  AppOtaDownload_Status_t: 成功或接收/写 Flash/CRC 校验错误。
 * @note    Header 不写入 slot，slot 起始地址必须保留 App 向量表。
 */
static AppOtaDownload_Status_t AppOtaDownload_ReceiveImageToSlot(AppOtaDownload_IpdReader_t *reader,
                                                                 const FirmwareHeader_t *header,
                                                                 BootSlot_t target_slot)
{
    FlashRegion_t target_region;
    FlashIfErrorCode_t flash_status;
    FirmwareVerifyResult_t verify_status;
    AppOtaDownload_Status_t status;
    uint8_t buffer[APP_OTA_FLASH_CHUNK_SIZE];
    uint32_t remaining;
    uint32_t offset = 0U;
    uint32_t chunk;

    if((reader == 0) || (header == 0))
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    status = AppOtaDownload_SlotToFlashRegion(target_slot, &target_region);
    if(status != APP_OTA_DOWNLOAD_OK)
    {
        return status;
    }

    remaining = header->image_size;
    while(remaining > 0U)
    {
        chunk = (remaining > APP_OTA_FLASH_CHUNK_SIZE) ? APP_OTA_FLASH_CHUNK_SIZE : remaining;
        status = AppOtaDownload_ReadPayload(reader, buffer, chunk);
        if(status != APP_OTA_DOWNLOAD_OK)
        {
            return status;
        }

        flash_status = FlashIf_WriteRegion(target_region, offset, buffer, chunk);
        if(flash_status != FLASHIF_OK)
        {
            App_LogPrintf("[OTA-DL] write slot %s fail offset=%lu %s\r\n",
                          AppOtaDownload_SlotName(target_slot),
                          (unsigned long)offset,
                          FlashIfErrorCode_String(flash_status));
            return APP_OTA_DOWNLOAD_ERR_FLASH;
        }

        offset += chunk;
        remaining -= chunk;
    }

    verify_status = FirmwareVerify_ImageInSlot(header, target_slot);
    if(verify_status != FW_VERIFY_OK)
    {
        App_LogPrintf("[OTA-DL] verify image fail %s\r\n",
                      FirmwareVerify_ResultString(verify_status));
        return APP_OTA_DOWNLOAD_ERR_VERIFY;
    }

    App_LogPrintf("[OTA-DL] receive image OK size=%lu crc=0x%08lx\r\n",
                  (unsigned long)header->image_size,
                  (unsigned long)header->image_crc32);
    return APP_OTA_DOWNLOAD_OK;
}

/*
 * @brief   从 HTTP body 中读取 FirmwareHeader_t 和 raw image 到目标 slot。
 * @param   target_slot: inactive slot。
 * @param   header: 输出已接收并校验的 FirmwareHeader_t。
 * @retval  AppOtaDownload_Status_t: 成功或 HTTP/长度/校验/Flash 错误。
 * @note    body 长度必须等于 sizeof(FirmwareHeader_t) + image_size。
 */
static AppOtaDownload_Status_t AppOtaDownload_ReceivePackageToSlot(BootSlot_t target_slot,
                                                                   FirmwareHeader_t *header)
{
    AppOtaDownload_IpdReader_t reader;
    AppOtaDownload_Status_t status;
    FirmwareVerifyResult_t verify_status;
    uint32_t content_length;

    if(header == 0)
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    memset(&reader, 0, sizeof(reader));
    memset(header, 0, sizeof(*header));

    status = AppOtaDownload_ReadHttpHeader(&reader, &content_length);
    if(status != APP_OTA_DOWNLOAD_OK)
    {
        return status;
    }

    if(content_length < sizeof(FirmwareHeader_t))
    {
        return APP_OTA_DOWNLOAD_ERR_LENGTH;
    }

    status = AppOtaDownload_ReadPayload(&reader, (uint8_t *)header, sizeof(FirmwareHeader_t));
    if(status != APP_OTA_DOWNLOAD_OK)
    {
        return status;
    }

    verify_status = FirmwareVerify_HeaderForSlot(header, target_slot);
    if(verify_status != FW_VERIFY_OK)
    {
        App_LogPrintf("[OTA-DL] verify header fail %s\r\n",
                      FirmwareVerify_ResultString(verify_status));
        return APP_OTA_DOWNLOAD_ERR_VERIFY;
    }

    if(content_length != (sizeof(FirmwareHeader_t) + header->image_size))
    {
        App_LogPrintf("[OTA-DL] length mismatch http=%lu image=%lu\r\n",
                      (unsigned long)content_length,
                      (unsigned long)header->image_size);
        return APP_OTA_DOWNLOAD_ERR_LENGTH;
    }

    status = AppOtaDownload_ReceiveImageToSlot(&reader, header, target_slot);
    if(status != APP_OTA_DOWNLOAD_OK)
    {
        return status;
    }

    return APP_OTA_DOWNLOAD_OK;
}

/*
 * @brief   保存 PENDING_VERIFY 并触发软件复位。
 * @param   header: 已下载并校验通过的固件包头。
 * @param   target_slot: 已写入新镜像的 inactive slot。
 * @retval  正常情况下不返回；失败时返回 CONFIG/PARAM 错误。
 * @note    复位后由 Bootloader 校验 pending slot，进入 TESTING 或保留旧 confirmed slot。
 */
static AppOtaDownload_Status_t AppOtaDownload_SavePendingAndReset(const FirmwareHeader_t *header,
                                                                  BootSlot_t target_slot)
{
    UpgradeConfigIfErrorCode_t cfg_status;

    if(header == 0)
    {
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    cfg_status = UpgradeConfig_SavePending(header, target_slot);
    if(cfg_status != UpgradeConfigIf_OK)
    {
        App_LogPrintf("[OTA-DL] save pending fail %s\r\n",
                      UpgradeConfigIfErrorCode_String(cfg_status));
        return APP_OTA_DOWNLOAD_ERR_CONFIG;
    }

    App_LogPrintf("[OTA-DL] save pending slot=%s OK\r\n",
                  AppOtaDownload_SlotName(target_slot));
    AppOtaDownload_ReportFinish(APP_OTA_DOWNLOAD_OK);
    App_LogPrintf("[OTA-DL] software reset\r\n");
    vTaskDelay(pdMS_TO_TICKS(200U));
    NVIC_SystemReset();

    while(1)
    {
    }
}

/*
 * @brief   执行一次 App 侧 ESP01 HTTP OTA 下载流程。
 * @retval  AppOtaDownload_Status_t: 下载、校验、pending 保存或跳过原因。
 * @note
 *          流程顺序：
 *          1. 确认 Config 为 CONFIRMED 并选择 inactive slot。
 *          2. 初始化 ESP01/WiFi，擦除目标 slot。
 *          3. HTTP 下载 .pkg，Header 进 RAM，raw image 分块写 Flash。
 *          4. 镜像校验成功且版本更高时保存 pending 并 reset。
 *          服务器请求可覆盖 HTTP path 和期望版本；目标 slot 仍由 App 侧选择。
 *          调用前应已获取 ESP01 互斥锁，避免 UploadTask 同时操作 AT Parser/USART3。
 */
AppOtaDownload_Status_t AppOtaDownload_RunOnce(const AppOtaDownload_Request_t *request)
{
    UpgradeConfig_t cfg;
    FirmwareHeader_t header;
    BootSlot_t target_slot;
    const char *http_path;
    uint32_t confirmed_version;
    ATK_ESP01_Status_t esp_status;
    AppOtaDownload_Status_t status;

    if(AppOtaDownload_ConfigLooksPlaceholder())
    {
        App_LogPrintf("[OTA-DL] config placeholder, skip\r\n");
        return APP_OTA_DOWNLOAD_ERR_PARAM;
    }

    status = AppOtaDownload_SelectTarget(&cfg, &target_slot);
    if(status != APP_OTA_DOWNLOAD_OK)
    {
        return status;
    }
    AppOtaDownload_ReportTarget(target_slot);

    if((request != 0) && (request->has_http_path != 0U))
    {
        if(AppOtaDownload_IsSafeHttpPath(request->http_path) == 0U)
        {
            App_LogPrintf("[OTA-DL] request path invalid: %s\r\n",
                          request->http_path);
            return APP_OTA_DOWNLOAD_ERR_PARAM;
        }

        http_path = request->http_path;
    }
    else
    {
        http_path = AppOtaDownload_PathForSlot(target_slot);
    }
    if(http_path == 0)
    {
        return APP_OTA_DOWNLOAD_ERR_SLOT;
    }

    if((request != 0) && (request->has_expected_version != 0U))
    {
        App_LogPrintf("[OTA-DL] confirmed=%s target=%s path=%s expect_ver=%lu\r\n",
                      AppOtaDownload_SlotName(cfg.confirmed_slot),
                      AppOtaDownload_SlotName(target_slot),
                      http_path,
                      (unsigned long)request->expected_version);
    }
    else
    {
        App_LogPrintf("[OTA-DL] confirmed=%s target=%s path=%s expect_ver=NONE\r\n",
                      AppOtaDownload_SlotName(cfg.confirmed_slot),
                      AppOtaDownload_SlotName(target_slot),
                      http_path);
    }

    esp_status = ATK_ESP01_CloseTCP();
    (void)esp_status;

    esp_status = ATK_ESP01_Init();
    App_LogPrintf("[OTA-DL] ESP01 init: %s(%d)\r\n",
                  ATK_ESP01_StatusName(esp_status),
                  esp_status);
    if(esp_status != ATK_ESP01_OK)
    {
        return APP_OTA_DOWNLOAD_ERR_ESP01;
    }

    esp_status = ATK_ESP01_JoinAP(APP_OTA_WIFI_SSID, APP_OTA_WIFI_PASSWORD);
    App_LogPrintf("[OTA-DL] WiFi join ssid=%s: %s(%d)\r\n",
                  APP_OTA_WIFI_SSID,
                  ATK_ESP01_StatusName(esp_status),
                  esp_status);
    if(esp_status != ATK_ESP01_OK)
    {
        return APP_OTA_DOWNLOAD_ERR_ESP01;
    }

    status = AppOtaDownload_EraseTarget(target_slot);
    if(status != APP_OTA_DOWNLOAD_OK)
    {
        return status;
    }

    esp_status = ATK_ESP01_StartTCP(APP_OTA_HTTP_HOST, APP_OTA_HTTP_PORT);
    App_LogPrintf("[OTA-DL] TCP connect %s:%u: %s(%d)\r\n",
                  APP_OTA_HTTP_HOST,
                  (unsigned int)APP_OTA_HTTP_PORT,
                  ATK_ESP01_StatusName(esp_status),
                  esp_status);
    if(esp_status != ATK_ESP01_OK)
    {
        return APP_OTA_DOWNLOAD_ERR_ESP01;
    }

    status = AppOtaDownload_SendHttpGet(APP_OTA_HTTP_HOST,
                                        APP_OTA_HTTP_PORT,
                                        http_path);
    if(status != APP_OTA_DOWNLOAD_OK)
    {
        App_LogPrintf("[OTA-DL] HTTP GET %s\r\n",
                      AppOtaDownload_StatusName(status));
        return status;
    }

    status = AppOtaDownload_ReceivePackageToSlot(target_slot, &header);
    if(status != APP_OTA_DOWNLOAD_OK)
    {
        App_LogPrintf("[OTA-DL] receive %s\r\n",
                      AppOtaDownload_StatusName(status));
        return status;
    }
    AppOtaDownload_ReportPackage(&header);

    if((request != 0) &&
       (request->has_expected_version != 0U) &&
       (header.image_version != request->expected_version))
    {
        App_LogPrintf("[OTA-DL] expected version mismatch expect=%lu pkg=%lu\r\n",
                      (unsigned long)request->expected_version,
                      (unsigned long)header.image_version);
        return APP_OTA_DOWNLOAD_ERR_VERSION;
    }

    confirmed_version = AppOtaDownload_VersionForSlot(&cfg, cfg.confirmed_slot);
    if(header.image_version <= confirmed_version)
    {
        App_LogPrintf("[OTA-DL] version skip confirmed=%lu download=%lu\r\n",
                      (unsigned long)confirmed_version,
                      (unsigned long)header.image_version);
        return APP_OTA_DOWNLOAD_ERR_VERSION;
    }

    status = AppOtaDownload_SavePendingAndReset(&header, target_slot);
    AppOtaDownload_ReportFinish(status);
    return status;
}
