#ifndef __ATK_ESP01_H
#define __ATK_ESP01_H
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明正点原子 ATK-ESP-01 WiFi 模块驱动接口。
 * 2. 负责 AT 指令发送、响应等待、联网、TCP 连接和数据发送。
 * 3. 底层串口使用 USART3，接收数据进入 RingBuffer，再由 AT Parser 处理。
 *
 * 当前状态：
 * - 已声明基础 AT 测试、关闭回显、STA 模式设置、WiFi 连接、IP 查询、TCP 连接、数据发送和 TCP 关闭接口。
 */

const char *ATK_ESP01_StatusName(ATK_ESP01_Status_t status);
ATK_ESP01_Status_t ATK_ESP01_Test(void);
ATK_ESP01_Status_t ATK_ESP01_DisableEcho(void);
ATK_ESP01_Status_t ATK_ESP01_SetStationMode(void);
ATK_ESP01_Status_t ATK_ESP01_JoinAP(const char *ssid, const char *password);
ATK_ESP01_Status_t ATK_ESP01_GetIP(void);
ATK_ESP01_Status_t ATK_ESP01_Init(void);
ATK_ESP01_Status_t ATK_ESP01_StartTCP(const char *host, uint16_t port);
ATK_ESP01_Status_t ATK_ESP01_SendData(const char *data);
ATK_ESP01_Status_t ATK_ESP01_CloseTCP(void);

#endif /* __ATK_ESP01_H */
