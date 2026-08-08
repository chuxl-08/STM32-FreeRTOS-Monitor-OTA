#include "at_parser.h"
#include "stm32f10x.h"
#include "bsp_usart.h"
#include <string.h>
#include "Delay.h"
/*
 * 文件职责：
 * 1. 实现 AT 指令响应解析。
 * 2. 基于 USART3 字节流累计响应字符串，并匹配 OK、ERROR、ready、> 等关键字。
 * 3. 为 ATK-ESP-01 驱动提供带 timeout 的 AT 指令发送和响应等待能力。
 *
 * 当前状态：
 * - 已完成阻塞式 SendAndWait 基础接口。
 * - 当前只返回匹配结果和错误类型，不向上层输出完整响应内容。
 */

#define RESPONSE_BUFFER_SIZE            256

/*
 * @brief  发送 AT 指令并等待指定关键字响应。
 * @param  cmd: 待发送的 AT 指令字符串，通常需要包含 "\r\n"。
 * @param  expect: 期望在响应中匹配到的关键字，例如 "OK"、"ready"、">"。
 * @param  timeout_ms: 等待超时时间，单位 ms。
 * @retval AT_Parser_Status_t:
 *         - AT_PARSER_OK: 匹配到 expect。
 *         - AT_PARSER_ERROR_PARAM: 输入参数错误。
 *         - AT_PARSER_ERROR_TIMEOUT: 超时未匹配到 expect。
 *         - AT_PARSER_ERROR_OVERFLOW: 响应缓存溢出。
 *         - AT_PARSER_ERROR_RESPONSE: 匹配到 "ERROR" 或 "FAIL"。
 *         - AT_PARSER_ERROR_USART: USART3 发送失败。
 * @note   不要求响应内容等于 expect，而是在累计响应中查找 expect。
 */
AT_Parser_Status_t AT_Parser_SendAndWait(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    if(cmd == 0 || expect == 0 || timeout_ms == 0)
    {
        return AT_PARSER_ERROR_PARAM;
    }

    uint8_t byte;
    uint16_t index;
    char response[RESPONSE_BUFFER_SIZE];

    index = 0;
    response[0] = '\0';

    BSP_USART3_ClearRxBuffer();

    if(BSP_USART3_SendString(cmd) != BSP_USART_OK)
    {
        return AT_PARSER_ERROR_USART;
    }

    while(timeout_ms > 0)
    {
        while(BSP_USART3_ReadByte(&byte) == BSP_USART_OK)
        {
            if(index >= RESPONSE_BUFFER_SIZE - 1)
            {
                return AT_PARSER_ERROR_OVERFLOW;
            }

            response[index++] = (char)byte;
            response[index] = '\0';

            if(strstr(response, "ERROR") != 0)
            {
                return AT_PARSER_ERROR_RESPONSE;
            }

            if(strstr(response, "FAIL") != 0)
            {
                return AT_PARSER_ERROR_RESPONSE;
            }

            if(strstr(response, expect) != 0)
            {
                return AT_PARSER_OK;
            }

        }

        Delay_ms(1);
        timeout_ms--;
    }

    return AT_PARSER_ERROR_TIMEOUT;
}
