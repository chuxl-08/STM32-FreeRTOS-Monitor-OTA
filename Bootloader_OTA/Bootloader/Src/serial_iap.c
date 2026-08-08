#include "serial_iap.h"
#include "platform.h"
#include <string.h>

#define SERIAL_IAP_ENTER_SYMBOL		'U'
#define SERIAL_IAP_SKIP_SYMBOL		'S'
#define SERIAL_IAP_HEADER			"BOTA"
#define SERIAL_IAP_READY_IMAGE		0x11U
#define SERIAL_IAP_ACK				0x06U
#define SERIAL_IAP_NACK				0x15U

/**
 * @brief 串口IAP入口等待。
 * @param timeout_cycles：等待周期数。 
 * @return
 *     	0：未收到入口符号U。
 *		1：接收到入口符号U。
 * @note
 * 		阻塞式等待。
 */
uint8_t SerialIap_WaitEnter(uint32_t timeout_cycles)
{
	uint8_t recieve_byte;
	
	while(timeout_cycles-- != 0UL)
	{
		if(Platform_TryGetChar(&recieve_byte))
		{
			if(recieve_byte == SERIAL_IAP_ENTER_SYMBOL)
			{
				return 1;
			}
			else if(recieve_byte == SERIAL_IAP_SKIP_SYMBOL)
			{
				return 0;
			}
		}
	}
	
	return 0;
}

/**
 * @brief 同步Header等待
 * @param timeout_cycles：等待周期数。
 * @return
 *     	0：未收到。
 *		1：收到。
 * @note
 * 
 */
uint8_t SerialIap_WaitSync(uint32_t timeout_cycles)
{
	uint8_t recieve_byte;
	uint8_t	match_index = 0;
	char header[sizeof(SERIAL_IAP_HEADER)] = SERIAL_IAP_HEADER;
	
	while(timeout_cycles-- != 0UL)
	{
		if(Platform_TryGetChar(&recieve_byte))
		{
			if(recieve_byte == header[match_index])
			{
				match_index++;
				if(header[match_index] == '\0')
				{
					return 1;
				}
			}
			else if(recieve_byte == header[0])
			{
				match_index = 1;
			}
			else
			{
				match_index = 0;
			}
		}
	}
	
	return 0;
}

/**
 * @brief 接收指定字节数数据
 * @param data_p：字节数据指针。
 * @param data_size：接收数据字节数。
 * @param timeout_cycles：超时周期数。
 * @return
 *     	0：超时返回。
 *		1：接收data_size字节完成。
 * @note
 * 
 */
uint8_t SerialIap_ReadExact(uint8_t *data_p, uint32_t data_size, uint32_t timeout_cycles)
{
	uint32_t received = 0;

	while(timeout_cycles-- != 0UL)
	{
		if(Platform_TryGetChar(&data_p[received]))
		{
			received++;
			if(received >= data_size)
			{
				return 1;
			}
		}
	}
	
	return 0;
}

/**
 * @brief 发送READY_IMAGE协议字节
 */
void SerialIap_SendReadyImage(void)
{
	Platform_PutChar(SERIAL_IAP_READY_IMAGE);
}

/**
 * @brief 发送ACK协议字节
 */
void SerialIap_SendAck(void)
{
	Platform_PutChar(SERIAL_IAP_ACK);
}

/**
 * @brief 发送NACK协议字节
 */
void SerialIap_SendNack(void)
{
	Platform_PutChar(SERIAL_IAP_NACK);
}

