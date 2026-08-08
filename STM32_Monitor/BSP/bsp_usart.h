#ifndef __BSP_USART_H
#define __BSP_USART_H
#include "stm32f10x.h"
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明 USART 底层初始化、发送和接收相关接口。
 * 2. USART1 用于调试日志，USART3 用于 ATK-ESP-01。
 * 3. 提供 USART3 RX DMA + IDLE、RingBuffer 缓存和 printf 重定向声明。
 *
 * 当前状态：
 * - USART1 用于 printf 调试输出。
 * - USART3 用于 ATK-ESP-01，支持 RX DMA + IDLE 接收并搬运到 RingBuffer。
 */

void BSP_USART1_Init(void);
void BSP_USART1_SendByte(uint8_t send_byte);
void BSP_USART1_SendString(const char *send_str);

BSP_USART_Status_t BSP_USART3_Init(void);
BSP_USART_Status_t BSP_USART3_SendByte(uint8_t send_byte);
BSP_USART_Status_t BSP_USART3_SendString(const char *send_str);
BSP_USART_Status_t BSP_USART3_ClearRxBuffer(void);
BSP_USART_Status_t BSP_USART3_ReadByte(uint8_t *recv_byte);
uint16_t BSP_USART3_GetRxCount(void);
uint16_t BSP_USART3_GetOverflowCount(void);


#endif /* __BSP_USART_H */
