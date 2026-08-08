#include "stm32f10x.h"                  // Device header
#include "platform.h"
#include <stdio.h>

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

#define USART1_GPIO_RCC                RCC_APB2Periph_GPIOA
#define USART1_RCC                     RCC_APB2Periph_USART1
#define USART1_PORT                    GPIOA
#define USART1_TX_PIN                  GPIO_Pin_9
#define USART1_RX_PIN                  GPIO_Pin_10


/**
 * @brief 串口1初始化。
 * @param 
 * @return
 *     None
 * @note  
 */
void Platform_InitUsart1(void)
{
	RCC_APB2PeriphClockCmd(USART1_GPIO_RCC | USART1_RCC, ENABLE);
	
	// PA9 -> USART1_TX   PA10 -> USART1_RX
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_StructInit(&GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Pin = USART1_RX_PIN;
	GPIO_Init(USART1_PORT, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = USART1_TX_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(USART1_PORT, &GPIO_InitStructure);
	
	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate = 115200;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_Init(USART1, &USART_InitStructure);
	
	USART_Cmd(USART1, ENABLE);
}

/**
 * @brief 串口发送字符。
 * @param send_str 字符串指针。
 * @return
 *     None
 * @note
 */
void Platform_PutChar(uint8_t send_byte)
{
    while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
	{
	}
	
	USART_SendData(USART1, send_byte);
}

/**
 * @brief 串口发送字符串。
 * @param send_str 字符串指针。
 * @return
 *     None
 */
void Platform_PutString(const char *send_str)
{
    while(*send_str)
	{
		Platform_PutChar(*send_str++);
	}
}

/**
 * @brief 非阻塞是接收字符。
 * @param out：接收字符指针变量。
 * @return
 * 		0：未接收。
 *		1：成功接收。
 * @note
 */
uint8_t Platform_TryGetChar(uint8_t *out)
{
	if(USART_GetFlagStatus(USART1,  USART_FLAG_RXNE) == SET)
	{
		*out = (uint8_t)(USART_ReceiveData(USART1));
		return 1;
	}
	
	return 0;
}

/**
 * @brief 阻塞式延时。
 * @param cycles 延时周期。
 * @return
 *     None
 */
void Platform_Delay(volatile unsigned int cycles)
{
    while (cycles-- != 0U) {
        __NOP();
    }
}

/*
 * @brief  printf 字符输出重定向。
 * @param  ch: printf 输出的字符。
 * @param  f: 标准库文件指针，当前未使用。
 * @retval int: 已输出字符。
 * @note   将 printf 输出重定向到 USART1。
 */
int fputc(int ch, FILE *f)
{
    Platform_PutChar((uint8_t)ch);
    return ch;
}


