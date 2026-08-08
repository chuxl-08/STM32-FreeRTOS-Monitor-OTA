#include "at_parser.h"
#include "esp01_port.h"
#include <string.h>

#define AT_PARSER_RESPONSE_BUFFER_SIZE   128U

/**
 * @brief 发送 AT 命令并等待指定应答字符串。
 * @param cmd：待发送的 AT 命令字符串，需要调用者包含结尾的 "\r\n"。
 * @param expect：期望在 ESP01 返回数据中匹配到的字符串。
 * @param timeout_cycles：等待应答的超时周期数。
 * @return
 *      AT_PARSER_OK：收到期望应答。
 *      AT_PARSER_ERR_PARAM：cmd 或 expect 为空指针。
 *      AT_PARSER_ERR_TIMEOUT：超时未收到期望应答。
 *      AT_PARSER_ERR_OVERFLOW：接收缓存不足。
 *      AT_PARSER_ERR_RESPONSE：收到 ERROR 或 FAIL。
 *      AT_PARSER_ERR_PORT：底层串口发送失败。
 * @note
 */
AtParserStatus_t AtParser_SendAndWait(const char *cmd,
                                      const char *expect,
                                      uint32_t timeout_cycles)
{
    char response[AT_PARSER_RESPONSE_BUFFER_SIZE];
    uint32_t response_len = 0U;
    uint8_t byte;

    if ((cmd == 0) || (expect == 0))
    {
        return AT_PARSER_ERR_PARAM;
    }

    memset(response, 0, sizeof(response));
    Esp01Port_ClearRx();

    if (Esp01Port_SendString(cmd) != ESP01_PORT_OK)
    {
        return AT_PARSER_ERR_PORT;
    }

    while (timeout_cycles-- > 0U)
    {
        if (Esp01Port_TryReadByte(&byte) != 0U)
        {
            if (response_len >= (AT_PARSER_RESPONSE_BUFFER_SIZE - 1U))
            {
                return AT_PARSER_ERR_OVERFLOW;
            }

            response[response_len++] = (char)byte;
            response[response_len] = '\0';

            if (strstr(response, expect) != 0)
            {
                return AT_PARSER_OK;
            }

            if ((strstr(response, "ERROR") != 0) || (strstr(response, "FAIL") != 0))
            {
                return AT_PARSER_ERR_RESPONSE;
            }
        }
        else
        {
            __NOP();
        }
    }

    return AT_PARSER_ERR_TIMEOUT;
}

/**
 * @brief AtParserStatus_t 转字符串。
 * @param status：AT 解析模块返回状态。
 * @return
 *      String
 * @note
 */
const char *AtParser_StatusString(AtParserStatus_t status)
{
    switch (status)
    {
    case AT_PARSER_OK:
        return "OK";
    case AT_PARSER_ERR_PARAM:
        return "PARAM";
    case AT_PARSER_ERR_TIMEOUT:
        return "TIMEOUT";
    case AT_PARSER_ERR_OVERFLOW:
        return "OVERFLOW";
    case AT_PARSER_ERR_RESPONSE:
        return "RESPONSE";
    case AT_PARSER_ERR_PORT:
        return "PORT";
    default:
        return "UNKNOWN";
    }
}
