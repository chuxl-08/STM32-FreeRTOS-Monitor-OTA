#include "atk_esp01.h"
#include "bsp_usart.h"
#include "at_parser.h"
#include <stdio.h>
#include <string.h>
#include "stm32f10x.h"

/*
 * 文件职责：
 * 1. 实现 ATK-ESP-01 AT 指令驱动。
 * 2. 管理 WiFi 初始化、STA 模式、路由器连接、TCP 连接和数据上传。
 * 3. 通过 Middlewares/at_parser.c 等待关键响应，所有等待过程必须带 timeout。
 * 4. 底层串口固定使用 USART3，接收数据由 BSP USART 中断写入 RingBuffer。
 *
 * 当前状态：
 * - 已完成基础 AT 测试、关闭回显、STA 模式设置、WiFi 连接、IP 查询、TCP 连接和数据发送接口。
 */

#define ATK_ESP01_JOIN_AP_CMD_MAX_LEN       160
#define ATK_ESP01_JOIN_AP_TIMEOUT_MS        18000
#define ATK_ESP01_TCP_CMD_MAX_LEN           160
#define ATK_ESP01_SEND_CMD_MAX_LEN          32
#define ATK_ESP01_STARTTCP_TIMEOUT_MS       8000
#define ATK_ESP01_CIPSEND_PROMPT_TIMEOUT_MS 5000
#define ATK_ESP01_SEND_TIMEOUT_MS           10000
#define ATK_ESP01_CLOSE_TCP_TIMEOUT_MS      5000
#define ATK_ESP01_CIPSTATUS_TIMEOUT_MS      1000

/*
 * @brief   ATK-ESP01 状态码转日志字符串。
 * @param   status: ATK_ESP01_Status_t 状态码。
 * @retval  const char *: 简短状态名。
 * @note    串口日志统一使用 NAME(code) 形式，例如 OK(0)、TIMEOUT(3)。
 */
const char *ATK_ESP01_StatusName(ATK_ESP01_Status_t status)
{
    switch(status)
    {
        case ATK_ESP01_OK:
            return "OK";
        case ATK_ESP01_ERROR_PARAM:
            return "PARAM";
        case ATK_ESP01_ERROR_AT:
            return "AT";
        case ATK_ESP01_ERROR_TIMEOUT:
            return "TIMEOUT";
        case ATK_ESP01_ERROR_RESPONSE:
            return "RESPONSE";
        case ATK_ESP01_ERROR_OVERFLOW:
            return "OVERFLOW";
        case ATK_ESP01_ERROR_USART:
            return "USART";
        default:
            return "UNKNOWN";
    }
}

/*
 * @brief  将 AT Parser 层状态码转换为 ATK-ESP01 驱动层状态码。
 * @param  status: AT Parser 层返回状态。
 * @retval ATK_ESP01_Status_t: ATK-ESP01 驱动层状态。
 * @note   隔离中间件层和驱动层错误码。
 */
static ATK_ESP01_Status_t ATK_ESP01_MapParserStatus(AT_Parser_Status_t status)
{
    switch(status)
    {
        case AT_PARSER_OK:
            return ATK_ESP01_OK;
        case AT_PARSER_ERROR_PARAM:
            return ATK_ESP01_ERROR_PARAM;
        case AT_PARSER_ERROR_TIMEOUT:
            return ATK_ESP01_ERROR_TIMEOUT;
        case AT_PARSER_ERROR_RESPONSE:
            return ATK_ESP01_ERROR_RESPONSE;
        case AT_PARSER_ERROR_OVERFLOW:
            return ATK_ESP01_ERROR_OVERFLOW;
        case AT_PARSER_ERROR_USART:
            return ATK_ESP01_ERROR_USART;
        default:
            return ATK_ESP01_ERROR_AT;
    }
}

/*
 * @brief  测试 ATK-ESP-01 是否能够响应基础 AT 指令。
 * @param  无。
 * @retval ATK_ESP01_Status_t:
 *         - ATK_ESP01_OK: 收到 OK 响应。
 *         - 其他: AT Parser 层错误转换后的驱动层错误。
 */
ATK_ESP01_Status_t ATK_ESP01_Test(void)
{
    AT_Parser_Status_t parser_status;

    parser_status = AT_Parser_SendAndWait("AT\r\n", "OK", 1000);
    return ATK_ESP01_MapParserStatus(parser_status);
}

/*
 * @brief  关闭 ATK-ESP-01 指令回显。
 * @param  无。
 * @retval ATK_ESP01_Status_t:
 *         - ATK_ESP01_OK: 收到 OK 响应。
 *         - 其他: AT Parser 层错误转换后的驱动层错误。
 * @note   关闭回显后，后续响应中不会再包含发送出去的 AT 指令本身。
 */
ATK_ESP01_Status_t ATK_ESP01_DisableEcho(void)
{
    AT_Parser_Status_t parser_status;

    parser_status = AT_Parser_SendAndWait("ATE0\r\n", "OK", 1000);
    return ATK_ESP01_MapParserStatus(parser_status);
}

/*
 * @brief  设置 ATK-ESP-01 为 Station 模式。
 * @param  无。
 * @retval ATK_ESP01_Status_t:
 *         - ATK_ESP01_OK: 收到 OK 响应。
 *         - 其他: AT Parser 层错误转换后的驱动层错误。
 */
ATK_ESP01_Status_t ATK_ESP01_SetStationMode(void)
{
    AT_Parser_Status_t parser_status;

    parser_status = AT_Parser_SendAndWait("AT+CWMODE=1\r\n", "OK", 1500);
    return ATK_ESP01_MapParserStatus(parser_status);
}

/*
 * @brief  连接指定 WiFi 热点。
 * @param  ssid: WiFi 名称字符串。
 * @param  password: WiFi 密码字符串。
 * @retval ATK_ESP01_Status_t:
 *         - ATK_ESP01_OK: 收到 OK 响应。
 *         - ATK_ESP01_ERROR_PARAM: 参数为空或命令缓存空间不足。
 *         - 其他: AT Parser 层错误转换后的驱动层错误。
 */
ATK_ESP01_Status_t ATK_ESP01_JoinAP(const char *ssid, const char *password)
{
    AT_Parser_Status_t parser_status;
    char command[ATK_ESP01_JOIN_AP_CMD_MAX_LEN];
    uint16_t command_len;

    if(ssid == 0 || password == 0)
    {
        return ATK_ESP01_ERROR_PARAM;
    }

    command_len = strlen("AT+CWJAP=\"") + strlen(ssid) +
                  strlen("\",\"") + strlen(password) +
                  strlen("\"\r\n") + 1;
    
    if(command_len > ATK_ESP01_JOIN_AP_CMD_MAX_LEN)
    {
        return ATK_ESP01_ERROR_PARAM;
    }

    sprintf(command, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);

    parser_status = AT_Parser_SendAndWait(command, "OK", ATK_ESP01_JOIN_AP_TIMEOUT_MS);
    return ATK_ESP01_MapParserStatus(parser_status);
}

/*
 * @brief  查询 ATK-ESP-01 当前 IP 信息。
 * @param  无。
 * @retval ATK_ESP01_Status_t:
 *         - ATK_ESP01_OK: 收到 OK 响应。
 *         - 其他: AT Parser 层错误转换后的驱动层错误。
 */
ATK_ESP01_Status_t ATK_ESP01_GetIP(void)
{
    AT_Parser_Status_t parser_status;

    parser_status = AT_Parser_SendAndWait("AT+CIFSR\r\n", "OK", 1000);
    return ATK_ESP01_MapParserStatus(parser_status);
}

/*
 * @brief  初始化 ATK-ESP-01 基础通信状态。
 * @param  无。
 * @retval ATK_ESP01_Status_t:
 *         - ATK_ESP01_OK: USART3 初始化、AT 测试、关闭回显和 STA 模式设置全部成功。
 *         - ATK_ESP01_ERROR_USART: USART3 初始化失败。
 *         - 其他: 某一步 AT 指令失败时直接返回对应错误。
 */
ATK_ESP01_Status_t ATK_ESP01_Init(void)
{
    ATK_ESP01_Status_t status;

    if(BSP_USART3_Init() != BSP_USART_OK)
    {
        return ATK_ESP01_ERROR_USART;
    }

    status = ATK_ESP01_Test();
    if(status != ATK_ESP01_OK)
    {
        return status;
    }

    status = ATK_ESP01_DisableEcho();
    if(status != ATK_ESP01_OK)
    {
        return status;
    }

    status = ATK_ESP01_SetStationMode();
    if(status != ATK_ESP01_OK)
    {
        return status;
    }

    return ATK_ESP01_OK;
}

/*
 * @brief  检查 TCP 是否已连接。
 * @param  无。
 * @retval ATK_ESP01_Status_t:
 *         - ATK_ESP01_OK: 查询到 STATUS:3，表示 TCP 已连接。
 *         - 其他: AT Parser 层错误转换后的驱动层错误。
 * @note   AT 指令格式 AT+CIPSTATUS\r\n
 *         STATUS:3 表示已经建立 TCP/UDP 连接。
 */
static ATK_ESP01_Status_t ATK_ESP01_IsTCPConnected(void)
{
    AT_Parser_Status_t parser_status;
    const char *command = "AT+CIPSTATUS\r\n";

    parser_status = AT_Parser_SendAndWait(command, "STATUS:3", ATK_ESP01_CIPSTATUS_TIMEOUT_MS);
    return ATK_ESP01_MapParserStatus(parser_status);
}


/*
 * @brief  建立 TCP 连接。
 * @param  host: 服务器域名或 IP 地址字符串。
 * @param  port: 服务器端口号，不能为 0。
 * @retval ATK_ESP01_Status_t:
 *         - ATK_ESP01_OK: 收到 OK 响应，TCP 连接建立成功。
 *         - ATK_ESP01_ERROR_PARAM: 参数为空、端口为 0 或命令缓存空间不足。
 *         - 其他: AT Parser 层错误转换后的驱动层错误。
 * @note   AT 指令格式 AT+CIPSTART="TCP","host",port。
 */
ATK_ESP01_Status_t ATK_ESP01_StartTCP(const char *host, uint16_t port)
{
    AT_Parser_Status_t parser_status;
    char command[ATK_ESP01_TCP_CMD_MAX_LEN];
    uint16_t command_len;

    if(host == 0 || port == 0)
    {
        return ATK_ESP01_ERROR_PARAM;
    }

    if(ATK_ESP01_IsTCPConnected() == ATK_ESP01_OK)
    {
        return ATK_ESP01_OK;
    }

    command_len = strlen("AT+CIPSTART=\"TCP\",\"") + strlen(host) +
                  strlen("\",") + 5 +
                  strlen("\r\n") + 1;

    if(command_len > ATK_ESP01_TCP_CMD_MAX_LEN)
    {
        return ATK_ESP01_ERROR_PARAM;
    }

    sprintf(command, "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n", host, (unsigned int)port);

    parser_status = AT_Parser_SendAndWait(command, "OK", ATK_ESP01_STARTTCP_TIMEOUT_MS);
    return ATK_ESP01_MapParserStatus(parser_status);
}

/*
 * @brief  通过已建立的 TCP 连接发送字符串数据。
 * @param  data: 待发送字符串，不包含字符串结束符 '\0'。
 * @retval ATK_ESP01_Status_t:
 *         - ATK_ESP01_OK: 收到 SEND OK，数据发送成功。
 *         - ATK_ESP01_ERROR_PARAM: 参数为空、数据长度为 0 或命令缓存空间不足。
 *         - 其他: AT Parser 层错误转换后的驱动层错误。
 * @note   发送流程为先发送 AT+CIPSEND=<len> 并等待 '>' 提示符，
 *         再发送真实数据并等待 SEND OK。调用前应确保 TCP 已连接。
 */
ATK_ESP01_Status_t ATK_ESP01_SendData(const char *data)
{
    AT_Parser_Status_t parser_status;
    char command[ATK_ESP01_SEND_CMD_MAX_LEN];
    uint16_t data_len;
    uint16_t command_len;

    if(data == 0)
    {
        return ATK_ESP01_ERROR_PARAM;
    }

    data_len = strlen(data);
    if(data_len == 0)
    {
        return ATK_ESP01_ERROR_PARAM;
    }

    command_len = strlen("AT+CIPSEND=") + 5 + strlen("\r\n") + 1;
    if(command_len > ATK_ESP01_SEND_CMD_MAX_LEN)
    {
        return ATK_ESP01_ERROR_PARAM;
    }
    
    sprintf(command, "AT+CIPSEND=%u\r\n", (uint32_t)data_len);

    parser_status = AT_Parser_SendAndWait(command, ">", ATK_ESP01_CIPSEND_PROMPT_TIMEOUT_MS);
    if(parser_status != AT_PARSER_OK)
    {
        return ATK_ESP01_MapParserStatus(parser_status);
    }

    parser_status = AT_Parser_SendAndWait(data, "SEND OK", ATK_ESP01_SEND_TIMEOUT_MS);
    return ATK_ESP01_MapParserStatus(parser_status);
}

/*
 * @brief  关闭当前 TCP 连接。
 * @param  无。
 * @retval ATK_ESP01_Status_t:
 *         - ATK_ESP01_OK: 收到 OK 响应。
 *         - 其他: AT Parser 层错误转换后的驱动层错误。
 */
ATK_ESP01_Status_t ATK_ESP01_CloseTCP(void)
{
    AT_Parser_Status_t parser_status;

    parser_status = AT_Parser_SendAndWait("AT+CIPCLOSE\r\n", "OK", ATK_ESP01_CLOSE_TCP_TIMEOUT_MS);

    return ATK_ESP01_MapParserStatus(parser_status);
}
