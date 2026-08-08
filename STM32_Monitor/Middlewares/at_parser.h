#ifndef __AT_PARSER_H
#define __AT_PARSER_H
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明 AT 指令响应解析接口。
 * 2. 从 USART3 接收字节流中识别 OK、ERROR、ready、>、SEND OK 等关键响应。
 * 3. 为 ATK-ESP-01 驱动提供带超时保护的“发送命令并等待关键字”能力。
 *
 * 当前状态：
 * - 已完成 AT_Parser_SendAndWait() 基础接口。
 * - 支持匹配期望响应，并将 ERROR、FAIL 识别为错误响应。
 */

AT_Parser_Status_t AT_Parser_SendAndWait(const char *cmd, const char *expect, uint32_t timeout_ms);


#endif /* __AT_PARSER_H */
