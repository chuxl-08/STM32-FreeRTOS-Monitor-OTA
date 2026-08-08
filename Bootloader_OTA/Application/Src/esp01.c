#include "esp01.h"
#include "at_parser.h"
#include "esp01_port.h"
#include <stdio.h>
#include <string.h>

#define ESP01_POWER_ON_DELAY_CYCLES      7200000U
#define ESP01_AT_TIMEOUT_CYCLES          7200000U
#define ESP01_WIFI_TIMEOUT_CYCLES        72000000U
#define ESP01_TCP_TIMEOUT_CYCLES         36000000U
#define ESP01_HTTP_TIMEOUT_CYCLES        72000000U
#define ESP01_CMD_BUFFER_SIZE            128U
#define ESP01_HTTP_REQ_BUFFER_SIZE       192U

/**
 * @brief AT 解析模块状态转换为 ESP01 模块状态。
 * @param status：AT 解析模块返回状态。
 * @return
 *      Esp01Status_t
 * @note
 *      ESP01 对外接口不直接暴露 AT_PARSER_xxx，方便后续扩展 WiFi、
 *      HTTP 下载等 ESP01 业务状态。
 */
static Esp01Status_t Esp01_FromAtParserStatus(AtParserStatus_t status)
{
    switch (status)
    {
    case AT_PARSER_OK:
        return ESP01_OK;
    case AT_PARSER_ERR_PARAM:
        return ESP01_ERR_PARAM;
    case AT_PARSER_ERR_TIMEOUT:
        return ESP01_ERR_TIMEOUT;
    case AT_PARSER_ERR_OVERFLOW:
        return ESP01_ERR_OVERFLOW;
    case AT_PARSER_ERR_RESPONSE:
        return ESP01_ERR_RESPONSE;
    case AT_PARSER_ERR_PORT:
        return ESP01_ERR_PORT;
    default:
        return ESP01_ERR_RESPONSE;
    }
}

/**
 * @brief ESP01 模块初始化并执行最小 AT 测试。
 * @param
 * @return
 *      ESP01_OK：ESP01 初始化后 AT 测试通过。
 *      其他错误：AT 测试失败，错误码来自 ESP01 模块状态。
 * @note
 *      当前初始化内容包括 USART3 初始化、上电等待和 AT 指令测试。
 */
Esp01Status_t Esp01_Init(void)
{
    Esp01Port_Init();
    Esp01Port_DelayCycles(ESP01_POWER_ON_DELAY_CYCLES);

    return Esp01_Test();
}

/**
 * @brief ESP01 AT 指令连通性测试。
 * @param
 * @return
 *      ESP01_OK：收到 OK。
 *      其他错误：未收到 OK 或收到异常应答。
 * @note
 *      发送 "AT\r\n" 并等待 "OK"，用于验证 MCU USART3 到 ESP01 的通信链路。
 */
Esp01Status_t Esp01_Test(void)
{
    return Esp01_FromAtParserStatus(
        AtParser_SendAndWait("AT\r\n", "OK", ESP01_AT_TIMEOUT_CYCLES));
}

/**
 * @brief 关闭 ESP01 指令回显。
 * @param
 * @return
 *      ESP01_OK：收到 OK。
 *      其他错误：未收到 OK 或收到异常应答。
 * @note
 *      发送 "ATE0\r\n"，后续进入 WiFi/HTTP 命令阶段时可减少串口返回内容干扰。
 */
Esp01Status_t Esp01_DisableEcho(void)
{
    return Esp01_FromAtParserStatus(
        AtParser_SendAndWait("ATE0\r\n", "OK", ESP01_AT_TIMEOUT_CYCLES));
}

/**
 * @brief 设置 ESP01 为 Station 模式。
 * @param
 * @return
 *      ESP01_OK：收到 OK。
 *      其他错误：AT 命令执行失败。
 * @note
 *      Station 模式用于让 ESP01 连接现有 Wi-Fi 路由器。
 */
Esp01Status_t Esp01_SetWifiModeStation(void)
{
    return Esp01_FromAtParserStatus(
        AtParser_SendAndWait("AT+CWMODE=1\r\n", "OK", ESP01_AT_TIMEOUT_CYCLES));
}

/**
 * @brief 设置 ESP01 为单连接模式。
 * @param
 * @return
 *      ESP01_OK：收到 OK。
 *      其他错误：AT 命令执行失败。
 * @note
 *      单连接模式下后续 TCP 连接使用普通 AT+CIPSTART/AT+CIPSEND 流程。
 */
Esp01Status_t Esp01_SetSingleConnection(void)
{
    return Esp01_FromAtParserStatus(
        AtParser_SendAndWait("AT+CIPMUX=0\r\n", "OK", ESP01_AT_TIMEOUT_CYCLES));
}

/**
 * @brief ESP01 连接指定 Wi-Fi。
 * @param ssid：Wi-Fi 名称字符串。
 * @param password：Wi-Fi 密码字符串。
 * @return
 *      ESP01_OK：连接命令返回 OK。
 *      ESP01_ERR_PARAM：ssid 或 password 为空指针。
 *      ESP01_ERR_OVERFLOW：AT 命令缓冲区不足。
 *      其他错误：AT 命令执行失败。
 * @note
 *      发送 AT+CWJAP，当前等待 OK 作为连接成功依据。
 */
Esp01Status_t Esp01_JoinAp(const char *ssid, const char *password)
{
    char cmd[ESP01_CMD_BUFFER_SIZE];
    int len;

    if ((ssid == 0) || (password == 0))
    {
        return ESP01_ERR_PARAM;
    }

    len = snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
    if ((len < 0) || ((uint32_t)len >= sizeof(cmd)))
    {
        return ESP01_ERR_OVERFLOW;
    }

    return Esp01_FromAtParserStatus(
        AtParser_SendAndWait(cmd, "OK", ESP01_WIFI_TIMEOUT_CYCLES));
}

/**
 * @brief ESP01 建立 TCP 连接。
 * @param host：目标服务器 IP 或域名字符串。
 * @param port：目标服务器端口。
 * @return
 *      ESP01_OK：TCP 连接命令返回 OK。
 *      ESP01_ERR_PARAM：host 为空指针。
 *      ESP01_ERR_OVERFLOW：AT 命令缓冲区不足。
 *      其他错误：AT 命令执行失败。
 * @note
 *      Day16 使用本地 HTTP server，host 应填写电脑局域网 IPv4，而不是 127.0.0.1。
 */
Esp01Status_t Esp01_StartTcp(const char *host, uint16_t port)
{
    char cmd[ESP01_CMD_BUFFER_SIZE];
    int len;

    if (host == 0)
    {
        return ESP01_ERR_PARAM;
    }

    len = snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n", host, (unsigned int)port);
    if ((len < 0) || ((uint32_t)len >= sizeof(cmd)))
    {
        return ESP01_ERR_OVERFLOW;
    }

    return Esp01_FromAtParserStatus(
        AtParser_SendAndWait(cmd, "OK", ESP01_TCP_TIMEOUT_CYCLES));
}

/**
 * @brief 通过已建立的 TCP 连接发送 HTTP GET 请求。
 * @param host：HTTP Host 字段。
 * @param port：HTTP Server 端口。
 * @param path：HTTP GET 路径。
 * @return
 *      ESP01_OK：收到 HTTP 响应头特征字符串。
 *      ESP01_ERR_PARAM：host 或 path 为空指针。
 *      ESP01_ERR_OVERFLOW：HTTP 请求或 CIPSEND 命令缓冲区不足。
 *      其他错误：AT 命令执行失败。
 * @note
 *      当前只验证 HTTP 响应是否到达，不解析 Content-Length 和包体内容。
 */
Esp01Status_t Esp01_SendHttpGet(const char *host, uint16_t port, const char *path)
{
    char http_req[ESP01_HTTP_REQ_BUFFER_SIZE];
    char cmd[ESP01_CMD_BUFFER_SIZE];
    int http_len;
    int cmd_len;
    Esp01Status_t status;

    if ((host == 0) || (path == 0))
    {
        return ESP01_ERR_PARAM;
    }

    http_len = snprintf(http_req,
                        sizeof(http_req),
                        "GET %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
                        path,
                        host,
                        (unsigned int)port);
    if ((http_len < 0) || ((uint32_t)http_len >= sizeof(http_req)))
    {
        return ESP01_ERR_OVERFLOW;
    }

    cmd_len = snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u\r\n", (unsigned int)strlen(http_req));
    if ((cmd_len < 0) || ((uint32_t)cmd_len >= sizeof(cmd)))
    {
        return ESP01_ERR_OVERFLOW;
    }

    status = Esp01_FromAtParserStatus(
        AtParser_SendAndWait(cmd, ">", ESP01_TCP_TIMEOUT_CYCLES));
    if (status != ESP01_OK)
    {
        return status;
    }

    return Esp01_FromAtParserStatus(
        AtParser_SendAndWait(http_req, "HTTP/1.", ESP01_HTTP_TIMEOUT_CYCLES));
}

/**
 * @brief Esp01Status_t 转字符串。
 * @param status：ESP01 模块返回状态。
 * @return
 *      String
 * @note
 *      用于 USART1 调试打印。
 */
const char *Esp01_StatusString(Esp01Status_t status)
{
    switch (status)
    {
    case ESP01_OK:
        return "OK";
    case ESP01_ERR_PARAM:
        return "PARAM";
    case ESP01_ERR_TIMEOUT:
        return "TIMEOUT";
    case ESP01_ERR_OVERFLOW:
        return "OVERFLOW";
    case ESP01_ERR_RESPONSE:
        return "RESPONSE";
    case ESP01_ERR_PORT:
        return "PORT";
    default:
        return "UNKNOWN";
    }
}
