#include "ring_buffer.h"

/*
 * 文件职责：
 * 1. 实现通用环形缓冲。
 * 2. 处理中断写入和主循环/任务读取之间的数据交换。
 * 3. 为 AT 解析器提供稳定的串口接收缓存。
 * 4. 本层不直接关闭中断，临界区保护由 BSP_USART 等调用层负责。
 */


/*
 * @brief  清空环形缓冲区状态。
 * @param  ring_buffer_data: 指向 Ring_Buffer_Data_t 控制结构体的指针。
 * @retval RingBuffer_Status_t:
 *         - RING_BUFFER_OK: 清空成功。
 *         - RING_BUFFER_ERROR_PARAM: ring_buffer_data 为空。
 * @note   仅清空读写索引、有效数据数量和溢出计数，不擦除 buffer 数组中的旧字节。
 */
RingBuffer_Status_t RingBuffer_Clear(Ring_Buffer_Data_t *ring_buffer_data)
{
   if(ring_buffer_data == 0)
   {
      return RING_BUFFER_ERROR_PARAM;
   }

    ring_buffer_data->write_index = 0;
    ring_buffer_data->read_index = 0;
    ring_buffer_data->count = 0;
    ring_buffer_data->overflow_count = 0;

    return RING_BUFFER_OK;
}

/*
 * @brief  初始化环形缓冲区。
 * @param  data: 指向 Ring_Buffer_Data_t 控制结构体的指针。
 * @param  buffer: 外部提供的实际存储数组。
 * @param  size: buffer 的容量，单位为字节。
 * @retval RingBuffer_Status_t:
 *         - RING_BUFFER_OK: 初始化成功。
 *         - RING_BUFFER_ERROR_PARAM: data、buffer 为空或 size 为 0。
 * @note   buffer 由调用者创建。
 */
RingBuffer_Status_t RingBuffer_Init(Ring_Buffer_Data_t *data, uint8_t *buffer, uint16_t size)
{
    if(data == 0 || buffer == 0 || size == 0)
    {
        return RING_BUFFER_ERROR_PARAM;
    }

    data->buffer = buffer;
    data->size = size;

    RingBuffer_Clear(data);

    return RING_BUFFER_OK;
}

/*
 * @brief  向环形缓冲区写入 1 字节数据。
 * @param  data: 指向 Ring_Buffer_Data_t 控制结构体的指针。
 * @param  write_byte: 待写入的字节。
 * @retval RingBuffer_Status_t:
 *         - RING_BUFFER_OK: 写入成功。
 *         - RING_BUFFER_ERROR_PARAM: 输入参数或缓冲区配置无效。
 *         - RING_BUFFER_ERROR_FULL: 缓冲区已满，本次数据被丢弃。
 */
RingBuffer_Status_t RingBuffer_WriteByte(Ring_Buffer_Data_t *data, uint8_t write_byte)
{
   if(data == 0 || data->buffer == 0 || data->size == 0)
   {
      return RING_BUFFER_ERROR_PARAM;
   }

   uint16_t write_index = data->write_index;

   if(data->count < data->size)
   {
      data->buffer[write_index] = write_byte;
      data->count ++;
      write_index ++;
      if(write_index >= data->size)
      {
         write_index = 0;
      }
      data->write_index = write_index;
   }
   else
   {
      data->overflow_count ++;
      return RING_BUFFER_ERROR_FULL;
   }
   return RING_BUFFER_OK;
}

/*
 * @brief  从环形缓冲区读取 1 字节数据。
 * @param  data: 指向 Ring_Buffer_Data_t 控制结构体的指针。
 * @param  read_byte: 输出参数，用于保存读取到的字节。
 * @retval RingBuffer_Status_t:
 *         - RING_BUFFER_OK: 读取成功。
 *         - RING_BUFFER_ERROR_PARAM: 输入参数或缓冲区配置无效。
 *         - RING_BUFFER_ERROR_EMPTY: 缓冲区为空，无数据可读。
 */
RingBuffer_Status_t RingBuffer_ReadByte(Ring_Buffer_Data_t *data, uint8_t *read_byte)
{
   if(data == 0 || read_byte == 0 || data->buffer == 0 || data->size == 0)
   {
      return RING_BUFFER_ERROR_PARAM;
   }

   if(data->count == 0)
   {

      *read_byte = 0;
      return RING_BUFFER_ERROR_EMPTY;
   }

   uint16_t read_index = data->read_index;

   *read_byte = data->buffer[read_index];
   data->count --;
   read_index ++;
   if(read_index >= data->size)
   {
      read_index = 0;
   }
   data->read_index = read_index;

   return RING_BUFFER_OK;
}

/*
 * @brief  判断环形缓冲区是否为空。
 * @param  data: 指向 Ring_Buffer_Data_t 控制结构体的指针。
 * @retval uint8_t:
 *         - 1: 缓冲区为空。
 *         - 0: 缓冲区非空。
 */
uint8_t RingBuffer_IsEmpty(Ring_Buffer_Data_t *data)
{
   uint16_t count = data->count;
   if(count == 0)
   {
      return 1;
   }
   else
   {
      return 0;
   }
}

/*
 * @brief  判断环形缓冲区是否已满。
 * @param  data: 指向 Ring_Buffer_Data_t 控制结构体的指针。
 * @retval uint8_t:
 *         - 1: 缓冲区已满。
 *         - 0: 缓冲区未满。
 */
uint8_t RingBuffer_IsFull(Ring_Buffer_Data_t *data)
{
   uint16_t count =  data->count;
   uint16_t size = data->size;
   if(count == size)
   {
      return 1;
   }
   else
   {
      return 0;
   }
}

/*
 * @brief  获取当前缓冲区中有效数据长度。
 * @param  data: 指向 Ring_Buffer_Data_t 控制结构体的指针。
 * @retval uint16_t: 当前尚未读取的有效字节数。
 */
uint16_t RingBuffer_GetLength(Ring_Buffer_Data_t *data)
{
   return data->count;
}

/*
 * @brief  获取当前缓冲区剩余可写空间。
 * @param  data: 指向 Ring_Buffer_Data_t 控制结构体的指针。
 * @retval uint16_t: 当前剩余可写字节数。
 */
uint16_t RingBuffer_GetFree(Ring_Buffer_Data_t *data)
{
   uint16_t count = data->count;
   return data->size - count;
}

/*
 * @brief  获取环形缓冲区溢出计数。
 * @param  data: 指向 Ring_Buffer_Data_t 控制结构体的指针。
 * @retval uint16_t: 溢出字节数。
 */
uint16_t RingBuffer_GetOverflow(Ring_Buffer_Data_t *data)
{
   uint16_t overflow = data->overflow_count;
   return overflow;
}


