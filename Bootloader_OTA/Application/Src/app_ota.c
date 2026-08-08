#include "app_ota.h"
#include "app_ota_config.h"
#include "at_parser.h"
#include "esp01.h"
#include "esp01_port.h"
#include "firmware.h"
#include "firmware_verify.h"
#include "flash_if.h"
#include "memory_map.h"
#include "platform.h"
#include "upgrade_config_if.h"
#include <stdio.h>
#include <string.h>

/*
 * 最小验证 Application 的 HTTP OTA 资源配置。
 *
 * - APP_OTA_HTTP_REQ_BUFFER_SIZE:
 *   HTTP GET 请求缓冲区，用于拼接 "GET /xxx.pkg HTTP/1.0"。
 *
 * - APP_OTA_AT_CMD_BUFFER_SIZE:
 *   ESP01 短 AT 命令缓冲区，主要用于 AT+CIPSEND。
 *
 * - APP_OTA_HTTP_HEADER_BUFFER_SIZE:
 *   HTTP 响应头缓冲区。下载流程会先解析 HTTP 状态码和 Content-Length，
 *   再继续接收固件包 body。
 *
 * - APP_OTA_PACKAGE_BUFFER_SIZE:
 *   完整参考固件包 RAM 缓冲区。该最小 Application 保留早期验证路径，
 *   STM32_Monitor 集成版 OTA 使用分块写 Flash 的方式。
 *
 * - APP_OTA_AT_TIMEOUT_CYCLES / APP_OTA_BYTE_TIMEOUT_CYCLES:
 *   AT 响应和 +IPD 原始字节读取的阻塞轮询超时计数。
 */
#define APP_OTA_HTTP_REQ_BUFFER_SIZE       192U
#define APP_OTA_AT_CMD_BUFFER_SIZE         64U
#define APP_OTA_HTTP_HEADER_BUFFER_SIZE    768U
#define APP_OTA_PACKAGE_BUFFER_SIZE        (48U * 1024U)
#define APP_OTA_AT_TIMEOUT_CYCLES          36000000U
#define APP_OTA_BYTE_TIMEOUT_CYCLES        72000000U

typedef struct
{
    uint32_t payload_remain;
} AppOtaIpdReader_t;

static uint8_t g_app_ota_pkg_buffer[APP_OTA_PACKAGE_BUFFER_SIZE];

/**
 * @brief 根据 BootSlot_t 转换为 FlashIf 使用的逻辑区域。
 * @param slot：目标 Slot。
 * @param region：输出 Flash 逻辑区域。
 * @return
 *      APP_OTA_OK：转换成功。
 *      APP_OTA_ERR_PARAM：参数为空或 slot 非法。
 * @note
 *      Application HTTP OTA 下载完成后，写入的是目标 Slot 的 raw image，
 *      不是独立缓存区。
 */
static AppOtaStatus_t AppOta_SlotToFlashRegion(BootSlot_t slot, FlashRegion_t *region)
{
    if (region == 0)
    {
        return APP_OTA_ERR_PARAM;
    }

    if (slot == BOOT_SLOT_A)
    {
        *region = FLASH_REGION_SLOT_A;
        return APP_OTA_OK;
    }

    if (slot == BOOT_SLOT_B)
    {
        *region = FLASH_REGION_SLOT_B;
        return APP_OTA_OK;
    }

    return APP_OTA_ERR_PARAM;
}

/**
 * @brief 读取 Config 中指定 Slot 的版本号。
 * @param cfg：Config 指针。
 * @param slot：目标 Slot。
 * @return Slot 版本号；slot 非法时返回 0。
 */
static uint32_t AppOta_ConfigSlotVersion(const UpgradeConfig_t *cfg, BootSlot_t slot)
{
    if (cfg == 0)
    {
        return 0U;
    }

    if (slot == BOOT_SLOT_A)
    {
        return cfg->slot_a_version;
    }

    if (slot == BOOT_SLOT_B)
    {
        return cfg->slot_b_version;
    }

    return 0U;
}

/**
 * @brief 写 Slot 前检查 Config 状态、目标 Slot 和版本闸门。
 * @param header：已通过 Header 校验的固件包头。
 * @param target_slot：固件包目标 Slot。
 * @return
 *      APP_OTA_OK：允许继续写入。
 *      APP_OTA_ERR_CONFIG：Config 读取失败或当前不是 CONFIRMED。
 *      APP_OTA_ERR_VERSION：目标为当前 confirmed Slot，或下载版本不高于当前确认版本。
 */
static AppOtaStatus_t AppOta_CheckTargetBeforeWrite(const FirmwareHeader_t *header, BootSlot_t target_slot)
{
    UpgradeConfig_t cfg;
    UpgradeConfigIfErrorCode_t cfg_status;
    uint32_t active_version;

    if (header == 0)
    {
        return APP_OTA_ERR_PARAM;
    }

    cfg_status = UpgradeConfig_Load(&cfg);
    if ((cfg_status != UpgradeConfigIf_OK) || (cfg.state != UPGRADE_STATE_CONFIRMED))
    {
        printf("[OTA] config not confirmed %s\r\n", UpgradeConfigIfErrorCode_String(cfg_status));
        return APP_OTA_ERR_CONFIG;
    }

    if (target_slot == cfg.confirmed_slot)
    {
        printf("[OTA] target is confirmed slot\r\n");
        return APP_OTA_ERR_VERSION;
    }

    active_version = AppOta_ConfigSlotVersion(&cfg, cfg.confirmed_slot);
    if (header->image_version <= active_version)
    {
        printf("[OTA] version skip active=%lu download=%lu\r\n",
               (unsigned long)active_version,
               (unsigned long)header->image_version);
        return APP_OTA_ERR_VERSION;
    }

    return APP_OTA_OK;
}

/**
 * @brief AppOtaStatus_t 转字符串。
 * @param status：OTA 模块状态。
 * @return
 *      String
 * @note
 *      用于 USART1 调试打印。
 */
const char *AppOta_StatusString(AppOtaStatus_t status)
{
    switch (status)
    {
    case APP_OTA_OK:
        return "OK";
    case APP_OTA_ERR_PARAM:
        return "PARAM";
    case APP_OTA_ERR_ESP01:
        return "ESP01";
    case APP_OTA_ERR_SEND:
        return "SEND";
    case APP_OTA_ERR_TIMEOUT:
        return "TIMEOUT";
    case APP_OTA_ERR_OVERFLOW:
        return "OVERFLOW";
    case APP_OTA_ERR_HTTP:
        return "HTTP";
    case APP_OTA_ERR_LENGTH:
        return "LENGTH";
    case APP_OTA_ERR_FLASH:
        return "FLASH";
    case APP_OTA_ERR_VERIFY:
        return "VERIFY";
    case APP_OTA_ERR_CONFIG:
        return "CONFIG";
    case APP_OTA_ERR_VERSION:
        return "VERSION";
    default:
        return "UNKNOWN";
    }
}


/**
 * @brief 带超时读取 ESP01 返回的 1 个原始字节。
 * @param byte：接收字节输出指针。
 * @return
 *      APP_OTA_OK：成功读到 1 字节。
 *      APP_OTA_ERR_PARAM：byte 为空指针。
 *      APP_OTA_ERR_TIMEOUT：超时没有读到数据。
 * @note
 *      该函数读取的是 ESP01 USART3 原始数据，可能是 "SEND OK"、
 *      "+IPD,<len>:"、HTTP header 或 HTTP body 中的任意字节。
 */
static AppOtaStatus_t AppOta_ReadRawByte(uint8_t *byte)
{
    uint32_t timeout = APP_OTA_BYTE_TIMEOUT_CYCLES;

    if(byte == 0)
    {
        return APP_OTA_ERR_PARAM;
    }

    while (timeout-- > 0U)
    {
        if(Esp01Port_TryReadByte(byte))
        {
            return APP_OTA_OK;
        }
        __NOP();
    }

    return APP_OTA_ERR_TIMEOUT;
}

/**
 * @brief 等待并匹配 ESP01 的 "+IPD,<len>:" 帧头。
 * @param reader：IPD 读取器状态。
 * @return
 *      APP_OTA_OK：成功解析到一个 IPD 帧头，并记录 payload 长度。
 *      APP_OTA_ERR_PARAM：reader 为空指针。
 *      APP_OTA_ERR_TIMEOUT：等待串口数据超时。
 *      APP_OTA_ERR_HTTP：IPD 帧格式异常。
 * @note
 *      ESP01 主动接收模式下，TCP 数据会被 AT 固件包装为 "+IPD,<len>:payload"。
 *      DownloadVerify 只把冒号后面的 payload 交给 HTTP 解析层。
 */
static AppOtaStatus_t AppOta_WaitIpdHeader(AppOtaIpdReader_t *reader)
{
    static const char ipd_prefix[] = "+IPD,";
    uint8_t byte;
    uint8_t match_index = 0;
    uint32_t payload_size = 0;
    AppOtaStatus_t status;

    if(reader==0)
    {
        return APP_OTA_ERR_PARAM;
    }

    while(match_index < (sizeof(ipd_prefix) - 1U))
    {
        status = AppOta_ReadRawByte(&byte);
        if (status != APP_OTA_OK)
        {
            return status;
        }

        if(byte == (uint8_t)ipd_prefix[match_index])
        {
            match_index++;
        }
        else
        {
            match_index = (byte == (uint8_t)ipd_prefix[0])? 1U:0U;
        }
    }

    while(1)
    {
        status = AppOta_ReadRawByte(&byte);
        if (status != APP_OTA_OK)
        {
            return status;
        }
        // 接收到的len 仍是字符格式，需要转为数字参与计算。
        if((byte >=(uint8_t)'0') && (byte <=(uint8_t)'9'))
        {
            payload_size = payload_size * 10U + (uint32_t)(byte - (uint8_t)'0');
            if (payload_size > APP_OTA_PACKAGE_BUFFER_SIZE)
            {
                return APP_OTA_ERR_OVERFLOW;
            }
        }
        else if(byte == (uint8_t)':')
        {
            if(payload_size == 0U)
            {
                return APP_OTA_ERR_HTTP;
            }
            reader->payload_remain = payload_size;
            return APP_OTA_OK;
        }
        else
        {
            return APP_OTA_ERR_HTTP;
        }
    }


}

/**
 * @brief 从 ESP01 IPD 数据流中读取 1 个 TCP payload 字节。
 * @param reader：IPD 读取器状态。
 * @param byte：payload 字节输出指针。
 * @return
 *      APP_OTA_OK：成功读到 1 个 payload 字节。
 *      其他错误：等待 IPD 帧或读取 payload 失败。
 * @note
 *      该函数屏蔽 ESP01 的 "+IPD,<len>:" 包装，让上层看到连续的 HTTP 字节流。
 */
static AppOtaStatus_t AppOta_ReadPayloadByte(AppOtaIpdReader_t *reader, uint8_t *byte)
{
    AppOtaStatus_t status;

    if((reader == 0) || (byte == 0))
    {
        return APP_OTA_ERR_PARAM;
    }
    
    if(reader->payload_remain == 0)
    {
        status = AppOta_WaitIpdHeader(reader);
        if(status != APP_OTA_OK)
        {
            return status;
        }
    }
    
    status = AppOta_ReadRawByte(byte);
    
    if(status != APP_OTA_OK)
    {
        return status;
    }

    reader->payload_remain--;
    return APP_OTA_OK;
}

/**
 * @brief 从 HTTP header 中解析 Content-Length。
 * @param header：以 '\0' 结尾的 HTTP header 字符串。
 * @param content_length：解析出的 body 字节数。
 * @return
 *      1：解析成功。
 *      0：未找到或格式错误。
 * @note
 *      当前本地 Python HTTP server 返回标准 "Content-Length: xxx" 字段。
 */
static uint8_t AppOta_ParseContentLength(const char *header, uint32_t *content_length)
{
    const char *field;
    uint32_t value = 0U;
    uint8_t has_digit = 0U;

    if ((header == 0) || (content_length == 0))
    {
        return 0;
    }

    field = strstr(header, "Content-Length:");
    if(field == 0)
    {
        return 0;
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
        return 0;
    }

    *content_length = value;

    return 1;
}

/**
 * @brief 发送 HTTP GET 请求，但不消费 HTTP 响应内容。
 * @param host：HTTP Host 字段。
 * @param port：HTTP Server 端口。
 * @param path：HTTP GET 路径。
 * @return
 *      APP_OTA_OK：CIPSEND 进入发送态，HTTP GET 已发送。
 *      APP_OTA_ERR_PARAM：host 或 path 为空。
 *      APP_OTA_ERR_OVERFLOW：命令或请求缓冲区不足。
 *      APP_OTA_ERR_SEND：发送失败或未等到 '>'。
 * @note
 *      HttpSend 的 Esp01_SendHttpGet() 会等待 "HTTP/1."，这会消耗响应流。
 *      DownloadVerify 必须保留完整响应给 IPD/HTTP/package 解析逻辑。
 */
static AppOtaStatus_t AppOta_SendHttpGetRequest(const char *host, uint16_t port, const char *path)
{
    char http_req[APP_OTA_HTTP_REQ_BUFFER_SIZE];
    char cmd[APP_OTA_AT_CMD_BUFFER_SIZE];
    int http_len;
    int cmd_len;

    if ((host == 0) || (path == 0))
    {
        return APP_OTA_ERR_PARAM;
    }

    http_len = snprintf(http_req,
                        sizeof(http_req),
                        "GET %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
                        path,
                        host,
                        (unsigned int)port);
    if ((http_len < 0) || ((uint32_t)http_len >= sizeof(http_req)))
    {
        return APP_OTA_ERR_OVERFLOW;
    }

    cmd_len = snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u\r\n", (unsigned int)strlen(http_req));
    if ((cmd_len < 0) || ((uint32_t)cmd_len >= sizeof(cmd)))
    {
        return APP_OTA_ERR_OVERFLOW;
    }

    if (AtParser_SendAndWait(cmd, ">", APP_OTA_AT_TIMEOUT_CYCLES) != AT_PARSER_OK)
    {
        return APP_OTA_ERR_SEND;
    }

    if (Esp01Port_SendString(http_req) != ESP01_PORT_OK)
    {
        return APP_OTA_ERR_SEND;
    }

    return APP_OTA_OK;
}

/**
 * @brief 从 HTTP 响应中接收完整 pkg 到 RAM。
 * @param pkg_buffer：pkg 接收缓冲区。
 * @param pkg_buffer_size：pkg 接收缓冲区大小。
 * @param pkg_size：实际收到的 pkg 大小。
 * @return
 *      APP_OTA_OK：HTTP header 和完整 body 接收成功。
 *      APP_OTA_ERR_HTTP：HTTP header 或 Content-Length 异常。
 *      APP_OTA_ERR_OVERFLOW：header 或 body 超过缓冲区。
 *      APP_OTA_ERR_TIMEOUT：接收超时。
 *      APP_OTA_ERR_LENGTH：pkg 长度不符合固件包最小要求。
 * @note
 *      接收 body 期间不打印进度，避免 USART1 调试输出阻塞导致 USART3 丢数据。
 */
static AppOtaStatus_t AppOta_ReceiveHttpPackage(uint8_t *pkg_buffer,
                                                uint32_t pkg_buffer_size,
                                                uint32_t *pkg_size)
{
    AppOtaIpdReader_t reader;
    char header[APP_OTA_HTTP_HEADER_BUFFER_SIZE];
    uint32_t header_len = 0U;
    uint32_t content_length = 0U;
    uint32_t received = 0U;
    uint32_t end_match = 0U;
    uint8_t byte;
    AppOtaStatus_t status;
    static const char header_end[] = "\r\n\r\n";

    if((pkg_buffer == 0) || (pkg_size == 0))
    {
        return APP_OTA_ERR_PARAM;
    }

    memset(&reader, 0, sizeof(reader));
    memset(header, 0, sizeof(header));

    while(end_match < 4U)
    {
        status = AppOta_ReadPayloadByte(&reader, &byte);
        if(status != APP_OTA_OK)
        {
            return status;
        }

        if(header_len >= (sizeof(header) - 1U))
        {
            return APP_OTA_ERR_OVERFLOW;
        }

        header[header_len++] = (char)byte;
        header[header_len] = '\0';

        if(byte == (uint8_t)header_end[end_match])
        {
            end_match++;
        }
        else
        {
            end_match = (byte == (uint8_t)header_end[0])? 1U:0U;
        }
    }
    
    if((strstr(header, "HTTP/1.") == 0) || (strstr(header, " 200 ") == 0))
    {
        return APP_OTA_ERR_HTTP;
    }

    if(AppOta_ParseContentLength(header, &content_length) == 0)
    {
        return APP_OTA_ERR_HTTP;
    }

    /* 先保证 HTTP body 至少包含包头，且不超过最小 Application 的 RAM 缓冲区。 */
    if ((content_length < sizeof(FirmwareHeader_t)) ||
        (content_length > pkg_buffer_size))
    {
        return APP_OTA_ERR_LENGTH;
    }

    while(received < content_length)
    {
        status = AppOta_ReadPayloadByte(&reader, &byte);
        if(status != APP_OTA_OK)
        {
            return status;
        }

        pkg_buffer[received++] = byte;
    }

    *pkg_size = received;
    printf("[OTA] HTTP header OK\r\n");
    printf("[OTA] Content-Length=%lu\r\n", (unsigned long)content_length);
    printf("[OTA] receive pkg OK %lu/%lu\r\n",
           (unsigned long)received,
           (unsigned long)content_length);
    return APP_OTA_OK;
}

/**
 * @brief 将 RAM 中的 pkg 写入目标 Slot 并完成固件校验。
 * @param pkg_buffer：完整 pkg 数据。
 * @param pkg_size：pkg 字节数，等于 FirmwareHeader_t + raw image。
 * @return
 *      APP_OTA_OK：写入和校验均成功。
 *      APP_OTA_ERR_LENGTH：pkg 长度和 header 描述不一致。
 *      APP_OTA_ERR_FLASH：擦除或写入目标 Slot 失败。
 *      APP_OTA_ERR_VERIFY：Header 或 image CRC 校验失败。
 * @note
 *      pkg 中的 Header 只保存在 RAM 和 Config；写入 Slot 的内容是 raw image。
 */
static AppOtaStatus_t AppOta_WriteAndVerifySlot(const uint8_t *pkg_buffer, uint32_t pkg_size)
{
    const FirmwareHeader_t *ram_header;
    FlashIfErrorCode_t flash_status;
    FirmwareVerifyResult_t verify_status;
    AppOtaStatus_t ota_status;
    BootSlot_t target_slot;
    FlashRegion_t target_region;

    if (pkg_buffer == 0)
    {
        return APP_OTA_ERR_PARAM;
    }

    if (pkg_size < sizeof(FirmwareHeader_t))
    {
        return APP_OTA_ERR_LENGTH;
    }

    ram_header = (const FirmwareHeader_t *)pkg_buffer;
    if (pkg_size != (sizeof(FirmwareHeader_t) + ram_header->image_size))
    {
        return APP_OTA_ERR_LENGTH;
    }

    target_slot = MemoryMap_SlotFromLoadAddress(ram_header->load_address);
    verify_status = FirmwareVerify_HeaderForSlot(ram_header, target_slot);
    if (verify_status != FW_VERIFY_OK)
    {
        printf("[VERIFY] slot header %s\r\n", FirmwareVerify_ResultString(verify_status));
        return APP_OTA_ERR_VERIFY;
    }
    printf("[VERIFY] slot header OK\r\n");

    ota_status = AppOta_CheckTargetBeforeWrite(ram_header, target_slot);
    if (ota_status != APP_OTA_OK)
    {
        return ota_status;
    }

    ota_status = AppOta_SlotToFlashRegion(target_slot, &target_region);
    if (ota_status != APP_OTA_OK)
    {
        return ota_status;
    }

    flash_status = FlashIf_EraseRegion(target_region, 0U, ram_header->image_size);
    if (flash_status != FLASHIF_OK)
    {
        printf("[OTA] erase slot %s\r\n", FlashIfErrorCode_String(flash_status));
        return APP_OTA_ERR_FLASH;
    }
    printf("[OTA] erase slot OK\r\n");

    flash_status = FlashIf_WriteRegion(target_region,
                                       0U,
                                       pkg_buffer + sizeof(FirmwareHeader_t),
                                       ram_header->image_size);
    if (flash_status != FLASHIF_OK)
    {
        printf("[OTA] write slot %s\r\n", FlashIfErrorCode_String(flash_status));
        return APP_OTA_ERR_FLASH;
    }
    printf("[OTA] write slot OK\r\n");

    verify_status = FirmwareVerify_ImageInSlot(ram_header, target_slot);
    if (verify_status != FW_VERIFY_OK)
    {
        printf("[VERIFY] slot image %s\r\n", FirmwareVerify_ResultString(verify_status));
        return APP_OTA_ERR_VERIFY;
    }
    printf("[VERIFY] slot image OK\r\n");

    return APP_OTA_OK;
}

/**
 * @brief 保存 pending 状态并触发软件复位。
 * @param download_header：已下载并校验通过的固件 Header。
 * @return
 *      APP_OTA_ERR_PARAM：download_header 为空。
 *      APP_OTA_ERR_VERSION：下载版本不高于当前已确认版本，本次跳过升级触发。
 *      APP_OTA_ERR_CONFIG：Config 保存失败。
 * @note
 *      UpgradeTrigger 只允许在目标 Slot 写入并完成镜像校验后调用该函数。
 *      为避免 App 每次启动后重复下载同版本包并复位，只有
 *      download_header->image_version 高于当前 confirmed slot 版本时才写 pending。
 */
static AppOtaStatus_t AppOta_SavePendingAndReset(const FirmwareHeader_t *download_header)
{
    UpgradeConfig_t cfg;
    UpgradeConfigIfErrorCode_t cfg_status;
    BootSlot_t target_slot;
    uint32_t active_version;

    if (download_header == 0)
    {
        return APP_OTA_ERR_PARAM;
    }

    target_slot = MemoryMap_SlotFromLoadAddress(download_header->load_address);
    if (MemoryMap_IsSlotValid(target_slot) == 0U)
    {
        return APP_OTA_ERR_PARAM;
    }

    cfg_status = UpgradeConfig_Load(&cfg);
    if ((cfg_status == UpgradeConfigIf_OK) &&
        (cfg.state == UPGRADE_STATE_CONFIRMED))
    {
        if (target_slot == cfg.confirmed_slot)
        {
            printf("[OTA] target is confirmed slot\r\n");
            return APP_OTA_ERR_VERSION;
        }

        active_version = AppOta_ConfigSlotVersion(&cfg, cfg.confirmed_slot);
        if (download_header->image_version <= active_version)
        {
            printf("[OTA] version skip active=%lu download=%lu\r\n",
                   (unsigned long)active_version,
                   (unsigned long)download_header->image_version);
            return APP_OTA_ERR_VERSION;
        }
    }

    cfg_status = UpgradeConfig_SavePending(download_header, target_slot);
    if (cfg_status != UpgradeConfigIf_OK)
    {
        printf("[OTA] save pending %s\r\n", UpgradeConfigIfErrorCode_String(cfg_status));
        return APP_OTA_ERR_CONFIG;
    }

    printf("[OTA] save pending OK\r\n");
    printf("[OTA] software reset\r\n");
    Platform_Delay(720000U);
    NVIC_SystemReset();

    while (1)
    {
    }
}

/**
 * @brief 执行 DownloadVerify ESP01 OTA 下载到目标 Slot 验证。
 * @param
 * @return
 *      APP_OTA_OK：网络下载、Flash 写入、目标 Slot 校验全部通过。
 *      其他错误：见 AppOtaStatus_t。
 * @note
 *      DownloadVerify 只负责把网络 pkg 可靠写入目标 Slot，不设置 pending，不触发复位。
 */
AppOtaStatus_t AppOta_RunDownloadVerifyTest(void)
{
    Esp01Status_t esp01_status;
    AppOtaStatus_t ota_status;
    uint32_t pkg_size = 0U;

    if ((strcmp(APP_OTA_WIFI_SSID, "YOUR_WIFI_SSID") == 0) ||
        (strcmp(APP_OTA_WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") == 0) ||
        (strcmp(APP_OTA_HTTP_HOST, "192.168.1.100") == 0))
    {
        printf("[OTA] config placeholder, skip\r\n");
        return APP_OTA_ERR_PARAM;
    }

    printf("[ESP01] init USART3\r\n");
    esp01_status = Esp01_Init();
    printf("[ESP01] AT %s\r\n", Esp01_StatusString(esp01_status));
    if (esp01_status != ESP01_OK)
    {
        return APP_OTA_ERR_ESP01;
    }

    esp01_status = Esp01_DisableEcho();
    printf("[ESP01] ATE0 %s\r\n", Esp01_StatusString(esp01_status));
    if (esp01_status != ESP01_OK)
    {
        return APP_OTA_ERR_ESP01;
    }

    esp01_status = Esp01_SetWifiModeStation();
    printf("[ESP01] CWMODE %s\r\n", Esp01_StatusString(esp01_status));
    if (esp01_status != ESP01_OK)
    {
        return APP_OTA_ERR_ESP01;
    }

    esp01_status = Esp01_SetSingleConnection();
    printf("[ESP01] CIPMUX %s\r\n", Esp01_StatusString(esp01_status));
    if (esp01_status != ESP01_OK)
    {
        return APP_OTA_ERR_ESP01;
    }

    esp01_status = Esp01_JoinAp(APP_OTA_WIFI_SSID, APP_OTA_WIFI_PASSWORD);
    printf("[ESP01] WIFI %s\r\n", Esp01_StatusString(esp01_status));
    if (esp01_status != ESP01_OK)
    {
        return APP_OTA_ERR_ESP01;
    }

    esp01_status = Esp01_StartTcp(APP_OTA_HTTP_HOST, APP_OTA_HTTP_PORT);
    printf("[ESP01] TCP %s\r\n", Esp01_StatusString(esp01_status));
    if (esp01_status != ESP01_OK)
    {
        return APP_OTA_ERR_ESP01;
    }

    ota_status = AppOta_SendHttpGetRequest(APP_OTA_HTTP_HOST,
                                           APP_OTA_HTTP_PORT,
                                           APP_OTA_HTTP_PATH);
    if (ota_status != APP_OTA_OK)
    {
        printf("[OTA] HTTP GET %s\r\n", AppOta_StatusString(ota_status));
        return ota_status;
    }

    ota_status = AppOta_ReceiveHttpPackage(g_app_ota_pkg_buffer,
                                           sizeof(g_app_ota_pkg_buffer),
                                           &pkg_size);
    if (ota_status != APP_OTA_OK)
    {
        printf("[OTA] receive %s\r\n", AppOta_StatusString(ota_status));
        return ota_status;
    }

    ota_status = AppOta_WriteAndVerifySlot(g_app_ota_pkg_buffer, pkg_size);
    if (ota_status != APP_OTA_OK)
    {
        printf("[OTA] download verify %s\r\n", AppOta_StatusString(ota_status));
        return ota_status;
    }

    printf("[OTA] download verify OK\r\n");
    return APP_OTA_OK;
}

/**
 * @brief 执行 UpgradeTrigger ESP01 OTA 触发 Bootloader 升级验证。
 * @param
 * @return
 *      正常触发软件复位后不会返回。
 *      APP_OTA_ERR_VERSION：下载包版本不高于当前 confirmed slot 版本，跳过 pending/reset。
 *      其他错误：网络下载、Flash 写入、校验或 Config 保存失败。
 * @note
 *      UpgradeTrigger 复用 DownloadVerify 的下载和目标 Slot 校验流程。
 *      只有所有校验都成功，才保存 PENDING_VERIFY 并复位交给 Bootloader。
 */
AppOtaStatus_t AppOta_RunUpgradeTriggerTest(void)
{
    AppOtaStatus_t ota_status;
    const FirmwareHeader_t *download_header;

    ota_status = AppOta_RunDownloadVerifyTest();
    if (ota_status != APP_OTA_OK)
    {
        return ota_status;
    }

    download_header = (const FirmwareHeader_t *)g_app_ota_pkg_buffer;
    return AppOta_SavePendingAndReset(download_header);
}
