#ifndef __RING_BUFFER_H
#define __RING_BUFFER_H
#include "stm32f10x.h"
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明通用环形缓冲接口。
 * 2. 为 USART 中断接收与主循环/上层解析之间提供字节缓存。
 * 3. 当前重点服务 USART3 + ATK-ESP-01 的 AT 响应接收。
 * 4. 本层只维护缓冲区数据结构，不负责关闭中断、AT 解析或串口收发。
 */

/*
 * @brief   Ring_Buffer_Data_t: 通用环形缓冲控制结构体。
 * @note    Ring_Buffer_Data_t:
 *          - buffer: 指向外部提供的实际存储数组。
 *          - size: 缓冲区总容量，单位为字节。
 *          - write_index: 下一次写入的位置。
 *          - read_index: 下一次读取的位置。
 *          - count: 当前缓冲区中尚未读取的有效字节数。
 *          - overflow_count: 缓冲区满时继续写入导致的数据丢弃次数。
 */
typedef struct 
{
    uint8_t *buffer;
    uint16_t size;
    uint16_t write_index;
    uint16_t read_index;
    uint16_t count;
    uint16_t overflow_count;
} Ring_Buffer_Data_t;

RingBuffer_Status_t RingBuffer_Clear(Ring_Buffer_Data_t *ring_buffer_data);
RingBuffer_Status_t RingBuffer_Init(Ring_Buffer_Data_t *data, uint8_t *buffer, uint16_t size);
RingBuffer_Status_t RingBuffer_WriteByte(Ring_Buffer_Data_t *data, uint8_t write_byte);
RingBuffer_Status_t RingBuffer_ReadByte(Ring_Buffer_Data_t *data, uint8_t *read_byte);
uint8_t RingBuffer_IsEmpty(Ring_Buffer_Data_t *data);
uint8_t RingBuffer_IsFull(Ring_Buffer_Data_t *data);
uint16_t RingBuffer_GetLength(Ring_Buffer_Data_t *data);
uint16_t RingBuffer_GetFree(Ring_Buffer_Data_t *data);
uint16_t RingBuffer_GetOverflow(Ring_Buffer_Data_t *data);

#endif /* __RING_BUFFER_H */
