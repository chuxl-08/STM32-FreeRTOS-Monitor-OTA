#include "app_upload.h"
#include "atk_esp01.h"
#include "at_parser.h"
#include "app_protocol.h"
#include "app_upload_config.h"
#include "app_log.h"
#include "bsp_usart.h"
#include "Delay.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/*
 * 文件职责：
 * 1. 封装 ESP01 上传链路的初始化、重连和发送接口。
 * 2. 维护 WiFi/TCP 连接状态、最近上传结果和成功/失败计数。
 * 3. 调用 app_protocol.* 将 UploadTask 聚合的数据打包为轻量键值字符串。
 */

#define APP_UPLOAD_BUFFER_SIZE        768
#define APP_UPLOAD_SERVER_RESPONSE_BUFFER_SIZE  256
#define APP_UPLOAD_SERVER_RESPONSE_WAIT_MS      300U
#define APP_UPLOAD_SERVER_RESPONSE_TAIL_WAIT_MS 100U
#define APP_UPLOAD_CIPSEND_CMD_BUFFER_SIZE      32U
#define APP_UPLOAD_SEND_PROMPT_TIMEOUT_MS       5000U
#define APP_UPLOAD_SEND_RESULT_TIMEOUT_MS       10000U

static char upload_buffer[APP_UPLOAD_BUFFER_SIZE];
static char upload_server_response_buffer[APP_UPLOAD_SERVER_RESPONSE_BUFFER_SIZE];

/*
 * @brief   根据服务器命令内容打印统一日志。
 * @param   server_command: 已解析的服务器命令。
 * @retval  无。
 */
static void App_Upload_LogServerCommand(const AppUpload_ServerCommand_t *server_command)
{
    if((server_command == 0) ||
       (server_command->type != APP_UPLOAD_SERVER_CMD_OTA_REQUEST))
    {
        return;
    }

    if(server_command->has_ota_path && server_command->has_ota_version)
    {
        App_LogPrintf("[UPLOAD] server_cmd=OTA path=%s ver=%lu\r\n",
                      server_command->ota_path,
                      (unsigned long)server_command->ota_version);
    }
    else if(server_command->has_ota_path)
    {
        App_LogPrintf("[UPLOAD] server_cmd=OTA path=%s ver=NONE\r\n",
                      server_command->ota_path);
    }
    else if(server_command->has_ota_version)
    {
        App_LogPrintf("[UPLOAD] server_cmd=OTA path=DEFAULT ver=%lu\r\n",
                      (unsigned long)server_command->ota_version);
    }
    else
    {
        App_LogPrintf("[UPLOAD] server_cmd=OTA path=DEFAULT ver=NONE\r\n");
    }
}

/*
 * @brief   判断 OTA 命令是否已经形成一行完整命令。
 * @param   response: 当前响应缓冲区。
 * @retval  1: "OTA=1" 之后已收到 '\r' 或 '\n'，可以解析整行命令。
 *          0: 尚未收到完整行，继续等待后续字段。
 * @note    ESP01 会逐字节进入 USART3 RingBuffer。不能刚看到 "OTA=1" 就返回，
 *          也不能被前面的 "SEND OK\r\n" 提前触发，否则 PATH/VER 可能被截断。
 */
static uint8_t App_Upload_OtaCommandLineReady(const char *response)
{
    const char *ota_command;

    if(response == 0)
    {
        return 0U;
    }

    ota_command = strstr(response, "OTA=1");
    if(ota_command == 0)
    {
        return 0U;
    }

    return ((strchr(ota_command, '\r') != 0) ||
            (strchr(ota_command, '\n') != 0)) ? 1U : 0U;
}

/*
 * @brief   初始化服务器命令结构体。
 * @param   server_command: 待初始化命令；可为空。
 * @retval  无。
 */
static void App_Upload_ClearServerCommand(AppUpload_ServerCommand_t *server_command)
{
    if(server_command != 0)
    {
        memset(server_command, 0, sizeof(*server_command));
        server_command->type = APP_UPLOAD_SERVER_CMD_NONE;
    }
}

/*
 * @brief   从服务器响应中复制字符串字段。
 * @param   source: 字段起始位置，指向字段值第一个字符。
 * @param   dest: 输出缓冲区。
 * @param   dest_size: 输出缓冲区大小。
 * @retval  1: 复制成功；0: 参数错误或字段过长。
 * @note    字段以 ';'、空白或换行结束，适合解析 PATH=/xxx.pkg 这类轻量命令。
 */
static uint8_t App_Upload_CopyCommandField(const char *source,
                                           char *dest,
                                           uint16_t dest_size)
{
    uint16_t index = 0U;

    if((source == 0) || (dest == 0) || (dest_size == 0U))
    {
        return 0U;
    }

    while((source[index] != '\0') &&
          (source[index] != ';') &&
          (source[index] != '\r') &&
          (source[index] != '\n') &&
          (source[index] != ' ') &&
          (source[index] != '\t'))
    {
        if(index >= (dest_size - 1U))
        {
            dest[0] = '\0';
            return 0U;
        }

        dest[index] = source[index];
        index++;
    }

    if(index == 0U)
    {
        dest[0] = '\0';
        return 0U;
    }

    dest[index] = '\0';
    return 1U;
}

/*
 * @brief   从服务器响应中解析无符号整数字段。
 * @param   source: 字段起始位置，指向数字第一个字符。
 * @param   value: 输出整数值。
 * @retval  1: 解析成功；0: 参数错误或没有数字。
 */
static uint8_t App_Upload_ParseCommandU32(const char *source, uint32_t *value)
{
    uint32_t parsed = 0U;
    uint8_t has_digit = 0U;

    if((source == 0) || (value == 0))
    {
        return 0U;
    }

    while((*source >= '0') && (*source <= '9'))
    {
        has_digit = 1U;
        parsed = parsed * 10U + (uint32_t)(*source - '0');
        source++;
    }

    if(has_digit == 0U)
    {
        return 0U;
    }

    *value = parsed;
    return 1U;
}

/*
 * @brief   解析上传服务器响应中的 OTA 命令。
 * @param   response: ESP01 收到的原始 TCP payload 缓冲。
 * @param   server_command: 输出命令。
 * @retval  1: 识别到 OTA 命令；0: 未识别到命令。
 * @note    支持基础格式 "OTA=1"，也支持带服务器建议路径和版本的格式：
 *          "OTA=1;PATH=/monitor_slot_b_full_v12.pkg;VER=12"。
 */
static uint8_t App_Upload_ParseServerCommand(const char *response,
                                             AppUpload_ServerCommand_t *server_command)
{
    const char *field;
    uint32_t version;

    if((response == 0) || (server_command == 0))
    {
        return 0U;
    }

    if(strstr(response, "OTA=1") == 0)
    {
        return 0U;
    }

    App_Upload_ClearServerCommand(server_command);
    server_command->type = APP_UPLOAD_SERVER_CMD_OTA_REQUEST;

    field = strstr(response, "PATH=");
    if(field != 0)
    {
        field += strlen("PATH=");
        if(App_Upload_CopyCommandField(field,
                                       server_command->ota_path,
                                       APP_UPLOAD_OTA_PATH_MAX_LEN) != 0U)
        {
            server_command->has_ota_path = 1U;
        }
        else
        {
            App_LogPrintf("[UPLOAD] server_cmd=OTA path_parse_fail\r\n");
        }
    }

    field = strstr(response, "VER=");
    if(field != 0)
    {
        field += strlen("VER=");
        if(App_Upload_ParseCommandU32(field, &version) != 0U)
        {
            server_command->has_ota_version = 1U;
            server_command->ota_version = version;
        }
        else
        {
            App_LogPrintf("[UPLOAD] server_cmd=OTA ver_parse_fail\r\n");
        }
    }

    return 1U;
}

/*
 * @brief   发送上传 payload，并在等待 SEND OK 的同一段串口流里捕获服务器命令。
 * @param   data: 已打包的上传 payload。
 * @param   server_command: 输出服务器命令；不关心命令时可传 0。
 * @retval  ATK_ESP01_Status_t: ESP01 发送结果。
 * @note    服务器回复的 "+IPD,...OTA=1;PATH=...;VER=..." 可能早于或夹在
 *          "SEND OK" 前后到达。如果继续使用 AT_Parser_SendAndWait(data, "SEND OK")，
 *          AT Parser 会把 +IPD 内容读走但不返回给 UploadTask，导致 --ota-count 1
 *          的首条 OTA 命令被消耗。本函数把 SEND OK 和服务器命令放在同一轮读取中处理。
 */
static ATK_ESP01_Status_t App_Upload_SendDataAndReadCommand(const char *data,
                                                            AppUpload_ServerCommand_t *server_command)
{
    char cmd[APP_UPLOAD_CIPSEND_CMD_BUFFER_SIZE];
    uint8_t byte;
    uint16_t index = 0U;
    uint32_t wait_ms = APP_UPLOAD_SEND_RESULT_TIMEOUT_MS;
    uint32_t tail_wait_ms = 0U;
    uint32_t post_send_wait_ms = 0U;
    uint8_t send_ok = 0U;
    uint8_t command_found = 0U;
    int cmd_len;

    if(data == 0)
    {
        return ATK_ESP01_ERROR_PARAM;
    }

    App_Upload_ClearServerCommand(server_command);
    upload_server_response_buffer[0] = '\0';

    cmd_len = snprintf(cmd,
                       sizeof(cmd),
                       "AT+CIPSEND=%u\r\n",
                       (unsigned int)strlen(data));
    if((cmd_len < 0) || ((uint32_t)cmd_len >= sizeof(cmd)))
    {
        return ATK_ESP01_ERROR_PARAM;
    }

    if(AT_Parser_SendAndWait(cmd,
                             ">",
                             APP_UPLOAD_SEND_PROMPT_TIMEOUT_MS) != AT_PARSER_OK)
    {
        return ATK_ESP01_ERROR_AT;
    }

    BSP_USART3_ClearRxBuffer();
    if(BSP_USART3_SendString(data) != BSP_USART_OK)
    {
        return ATK_ESP01_ERROR_USART;
    }

    while(wait_ms > 0U)
    {
        while(BSP_USART3_ReadByte(&byte) == BSP_USART_OK)
        {
            if(index < (APP_UPLOAD_SERVER_RESPONSE_BUFFER_SIZE - 1U))
            {
                upload_server_response_buffer[index++] = (char)byte;
                upload_server_response_buffer[index] = '\0';
            }

            if((strstr(upload_server_response_buffer, "ERROR") != 0) ||
               (strstr(upload_server_response_buffer, "FAIL") != 0))
            {
                return ATK_ESP01_ERROR_RESPONSE;
            }

            if(strstr(upload_server_response_buffer, "SEND OK") != 0)
            {
                if(send_ok == 0U)
                {
                    send_ok = 1U;
                    post_send_wait_ms = APP_UPLOAD_SERVER_RESPONSE_WAIT_MS;
                }
            }

            if(strstr(upload_server_response_buffer, "OTA=1") != 0)
            {
                tail_wait_ms = APP_UPLOAD_SERVER_RESPONSE_TAIL_WAIT_MS;
            }

        }

        if((command_found == 0U) &&
           (App_Upload_OtaCommandLineReady(upload_server_response_buffer) != 0U) &&
           (App_Upload_ParseServerCommand(upload_server_response_buffer,
                                          server_command) != 0U))
        {
            command_found = 1U;
            App_Upload_LogServerCommand(server_command);
            if(send_ok != 0U)
            {
                return ATK_ESP01_OK;
            }
        }

        if(tail_wait_ms > 0U)
        {
            tail_wait_ms--;
            if(tail_wait_ms == 0U)
            {
                if((command_found == 0U) &&
                   (App_Upload_ParseServerCommand(upload_server_response_buffer,
                                                  server_command) != 0U))
                {
                    command_found = 1U;
                    App_Upload_LogServerCommand(server_command);
                }

                if(send_ok != 0U)
                {
                    return ATK_ESP01_OK;
                }
            }
        }

        if(post_send_wait_ms > 0U)
        {
            post_send_wait_ms--;
            if((post_send_wait_ms == 0U) && (tail_wait_ms == 0U))
            {
                return ATK_ESP01_OK;
            }
        }

        if((send_ok != 0U) &&
           (post_send_wait_ms == 0U) &&
           (tail_wait_ms == 0U))
        {
            return ATK_ESP01_OK;
        }

        Delay_ms(1);
        wait_ms--;
    }

    return ATK_ESP01_ERROR_TIMEOUT;
}

const char *AppUploadStatusName(AppUpload_Status_t status)
{
    switch(status)
    {
        case APP_UPLOAD_OK:
            return "OK";
        case APP_UPLOAD_ERROR_PARAM:
            return "PARAM";
        case APP_UPLOAD_ERROR_INIT:
            return "INIT";
        case APP_UPLOAD_ERROR_WIFI_CONNECT:
            return "WIFI";
        case APP_UPLOAD_ERROR_TCP_CONNECT:
            return "TCP";
        case APP_UPLOAD_ERROR_PROTOCOL:
            return "PROTOCOL";    
        case APP_UPLOAD_ERROR_ATK_ESP01:
            return "ATK_ESP01";
        case APP_UPLOAD_ERROR_ESP01_BUSY:
            return "ESP01_BUSY";
        default:
            return "UNKNOWN";
    }    
}

/*
 * @brief  上传初始化
 * @param  upload_status: 上传状态数据结构指针。
 * @retval AppUpload_Status_t:
 *         - APP_UPLOAD_OK: 正常
 *         - APP_UPLOAD_ERROR_PARAM: 参数空错误
 *         - APP_UPLOAD_ERROR_INIT: 初始化错误 串口
 *         - APP_UPLOAD_ERROR_WIFI_CONNECT: WIFI连接失败
 *         - APP_UPLOAD_ERROR_TCP_CONNECT: TCP连接失败
 */
AppUpload_Status_t App_Upload_Init(System_UploadStatus_t *upload_status)
{
    if(upload_status == 0)
    {
        return APP_UPLOAD_ERROR_PARAM;
    }
    ATK_ESP01_Status_t atk_status;

    upload_status->wifi_connected = UPLOAD_STATUS_FAIL;
    upload_status->tcp_connected = UPLOAD_STATUS_FAIL;
    upload_status->last_upload_status = UPLOAD_STATUS_FAIL;
    upload_status->upload_count = 0;
    upload_status->upload_fail_count = 0;

    atk_status = ATK_ESP01_Init();
    if(atk_status != ATK_ESP01_OK)
    {
        App_LogPrintf("[UPLOAD][INIT] esp01 init fail: %s(%d)\r\n",
                      ATK_ESP01_StatusName(atk_status),
                      atk_status);
        return APP_UPLOAD_ERROR_INIT;
    }

    atk_status = ATK_ESP01_JoinAP(APP_UPLOAD_WIFI_SSID, APP_UPLOAD_WIFI_PASSWORD);
    if(atk_status != ATK_ESP01_OK)
    {
        upload_status->wifi_connected = UPLOAD_STATUS_FAIL;
        App_LogPrintf("[UPLOAD][INIT] wifi join ssid=%s fail: %s(%d)\r\n",
                      APP_UPLOAD_WIFI_SSID,
                      ATK_ESP01_StatusName(atk_status),
                      atk_status);
        return APP_UPLOAD_ERROR_WIFI_CONNECT;
    }
    else
    {
        upload_status->wifi_connected = UPLOAD_STATUS_OK;
    }

    atk_status = ATK_ESP01_StartTCP(APP_UPLOAD_TCP_HOST, APP_UPLOAD_TCP_PORT);
    if(atk_status != ATK_ESP01_OK)
    {
        upload_status->tcp_connected = UPLOAD_STATUS_FAIL;
        App_LogPrintf("[UPLOAD][INIT] tcp connect %s:%u fail: %s(%d)\r\n",
                      APP_UPLOAD_TCP_HOST,
                      (unsigned int)APP_UPLOAD_TCP_PORT,
                      ATK_ESP01_StatusName(atk_status),
                      atk_status);
        return APP_UPLOAD_ERROR_TCP_CONNECT;
    }
    else
    {
        upload_status->tcp_connected = UPLOAD_STATUS_OK;
    }

    return APP_UPLOAD_OK;
}

/*
 * @brief  重新连接wifi、tcp
 * @param  upload_status: 上传状态数据结构指针。
 * @retval AppUpload_Status_t:
 *         - APP_UPLOAD_OK: 正常
 *         - APP_UPLOAD_ERROR_PARAM: 参数空错误
 *         - APP_UPLOAD_ERROR_WIFI_CONNECT: WIFI连接失败
 *         - APP_UPLOAD_ERROR_TCP_CONNECT: TCP连接失败
 */
AppUpload_Status_t App_Upload_Reconnect(System_UploadStatus_t *upload_status)
{
    if(upload_status == 0)
    {
        return APP_UPLOAD_ERROR_PARAM;
    }

    ATK_ESP01_Status_t atk_status;

    if(upload_status->wifi_connected == UPLOAD_STATUS_FAIL)
    {
        App_LogPrintf("[UPLOAD][RECONNECT] wifi start\r\n");
        atk_status = ATK_ESP01_JoinAP(APP_UPLOAD_WIFI_SSID, APP_UPLOAD_WIFI_PASSWORD);
        if(atk_status == ATK_ESP01_OK)
        {
            upload_status->wifi_connected = UPLOAD_STATUS_OK;
            App_LogPrintf("[UPLOAD][RECONNECT] wifi OK\r\n");
        }
        else
        {
            upload_status->wifi_connected = UPLOAD_STATUS_FAIL;
            App_LogPrintf("[UPLOAD][RECONNECT] wifi FAIL %s(%d)\r\n",
                          ATK_ESP01_StatusName(atk_status),
                          atk_status);
            return APP_UPLOAD_ERROR_WIFI_CONNECT;
        }
    }

    if(upload_status->tcp_connected == UPLOAD_STATUS_FAIL)
    {
        App_LogPrintf("[UPLOAD][RECONNECT] tcp start\r\n");
        atk_status = ATK_ESP01_StartTCP(APP_UPLOAD_TCP_HOST, APP_UPLOAD_TCP_PORT);
        if(atk_status == ATK_ESP01_OK)
        {
            upload_status->tcp_connected = UPLOAD_STATUS_OK;
            App_LogPrintf("[UPLOAD][RECONNECT] tcp OK\r\n");
        }
        else
        {
            upload_status->tcp_connected = UPLOAD_STATUS_FAIL;
            App_LogPrintf("[UPLOAD][RECONNECT] tcp FAIL %s(%d)\r\n",
                          ATK_ESP01_StatusName(atk_status),
                          atk_status);
            return APP_UPLOAD_ERROR_TCP_CONNECT;
        }
    }

    return APP_UPLOAD_OK;
}

/*
 * @brief   判断一次上传发送失败后是否应强制重新建立 TCP。
 * @param   status: ESP01 发送路径返回的错误码。
 * @retval  1: 当前 TCP 会话可能已经不可用，UploadTask 下一轮应进入重连。
 *          0: 错误与 TCP 会话无关，不强制重连。
 * @note    OTA-DL 会独占 ESP01 并切换到 HTTP TCP 会话。它结束后，UploadTask
 *          记录的 tcp_connected 可能仍是 OK，但实际 CIPSEND 已经无法使用，
 *          因此 AT/TIMEOUT/RESPONSE/USART 这类发送路径错误都需要让上传链路重建。
 */
static uint8_t App_Upload_ShouldReconnectTcpAfterSendFail(ATK_ESP01_Status_t status)
{
    switch(status)
    {
        case ATK_ESP01_ERROR_AT:
        case ATK_ESP01_ERROR_TIMEOUT:
        case ATK_ESP01_ERROR_RESPONSE:
        case ATK_ESP01_ERROR_USART:
            return 1U;
        default:
            return 0U;
    }
}

/*
 * @brief  系统数据上传
 * @param  upload_data: UploadTask 组合后的上传数据。
 * @param  system_upload_status: 上传状态数据结构指针。
 * @param  server_command: 输出服务器下发命令；不关心命令时可传 0。
 * @retval AppUpload_Status_t:
 *         - APP_UPLOAD_OK: 正常
 *         - APP_UPLOAD_ERROR_PARAM: 参数空错误
 *         - APP_UPLOAD_ERROR_PROTOCOL: 数据打包失败
 *         - APP_UPLOAD_ERROR_ATK_ESP01: 数据发送失败
 */
AppUpload_Status_t App_Upload_Send(const AppUpload_Data_t *upload_data,
                                   System_UploadStatus_t *system_upload_status,
                                   AppUpload_ServerCommand_t *server_command)
{
    if(upload_data == 0 || system_upload_status == 0)
    {
        return APP_UPLOAD_ERROR_PARAM;
    }
    ATK_ESP01_Status_t atk_status;

    if(server_command != 0)
    {
        App_Upload_ClearServerCommand(server_command);
    }

    if(App_Protocol_BuildUploadString(upload_data, upload_buffer, APP_UPLOAD_BUFFER_SIZE) != APP_PROTOCOL_OK)
    {
        system_upload_status->upload_fail_count++;
        system_upload_status->last_upload_status = UPLOAD_STATUS_FAIL;
        return APP_UPLOAD_ERROR_PROTOCOL;
    }

    atk_status = App_Upload_SendDataAndReadCommand(upload_buffer, server_command);
    if(atk_status != ATK_ESP01_OK)
    {
        system_upload_status->upload_fail_count++;
        system_upload_status->last_upload_status = UPLOAD_STATUS_FAIL;
        if(App_Upload_ShouldReconnectTcpAfterSendFail(atk_status) != 0U)
        {
            system_upload_status->tcp_connected = UPLOAD_STATUS_FAIL;
            App_LogPrintf("[UPLOAD] send fail %s(%d), mark tcp disconnected\r\n",
                          ATK_ESP01_StatusName(atk_status),
                          atk_status);
        }
        return APP_UPLOAD_ERROR_ATK_ESP01;
    }
    else
    {
        system_upload_status->upload_count++;
        system_upload_status->last_upload_status = UPLOAD_STATUS_OK;
    }

    return APP_UPLOAD_OK;
}
