#include <stdio.h>
#include "stm32f10x.h"                  // Device header
#include "stm32f10x_dma.h"
#include "bsp_usart.h"
#include "Delay.h"
#include "ring_buffer.h"


/*
 * 文件职责：
 * 1. 实现 USART1 调试串口和 USART3 WiFi 串口的底层配置。
 * 2. 配置波特率、GPIO 复用、NVIC 中断和基础发送函数。
 * 3. 将 USART3 接收数据交给 Middlewares/ring_buffer 模块缓存。
 * 4. 为 AT parser 和 ATK-ESP-01 驱动提供字节级收发接口。
 *
 * 当前状态：
 * - 已完成 USART1 的初始化和发送函数实现。
 * - printf 已重定向到 USART1。
 * - USART3 用于 ATK-ESP-01，RX DMA 接收后由 IDLE 中断或读取轮询搬运到 RingBuffer。
 */

#define USART1_GPIO_RCC                RCC_APB2Periph_GPIOA
#define USART1_RCC                     RCC_APB2Periph_USART1
#define USART1_PORT                    GPIOA
#define USART1_TX_PIN                  GPIO_Pin_9
#define USART1_RX_PIN                  GPIO_Pin_10

#define USART3_GPIO_RCC                RCC_APB2Periph_GPIOB
#define USART3_RCC                     RCC_APB1Periph_USART3
#define USART3_PORT                    GPIOB
#define USART3_TX_PIN                  GPIO_Pin_10
#define USART3_RX_PIN                  GPIO_Pin_11
#define USART3_RX_BUFFER_SIZE          1024
#define USART3_DMA_RX_BUFFER_SIZE      512
#define USART3_DMA_RX_CHANNEL          DMA1_Channel3

#define USART_TIMEOUT_US   1000    // timeout 1ms

static uint8_t usart3_rx_buffer[USART3_RX_BUFFER_SIZE];
static Ring_Buffer_Data_t usart3_rx_ringbuffer_data;
static uint8_t usart3_dma_rx_buffer[USART3_DMA_RX_BUFFER_SIZE];
static uint16_t usart3_dma_last_pos;

static void BSP_USART3_FlushDmaRxToRingBuffer(void);
static void BSP_USART3_ResetDmaRx(void);

/* ********************************BSP_USART1******************************* */
/*
 * @brief  初始化 USART1 调试串口。
 * @retval None
 * @note   PA9 -> USART1_TX，PA10 -> USART1_RX。
 *         当前 USART1 主要用于 printf 调试输出，波特率为 115200。
 */
void BSP_USART1_Init(void)
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

/*
 * @brief  通过 USART1 发送 1 字节数据。
 * @param  send_byte: 待发送字节。
 * @retval None
 * @note   本函数用于 printf 重定向路径，不带超时保护。
 */
void BSP_USART1_SendByte(uint8_t send_byte)
{
	while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
	{
	}
	
	USART_SendData(USART1, send_byte);
}

/*
 * @brief  通过 USART1 发送字符串。
 * @param  send_str: 待发送的 C 字符串。
 * @retval None
 * @note   发送直到遇到字符串结束符 '\0'。
 */
void BSP_USART1_SendString(const char *send_str)
{
	while(*send_str)
	{
		BSP_USART1_SendByte(*send_str++);
	}
}

/* ********************************BSP_USART3******************************* */
/*
 * @brief   初始化 USART3。
 * @retval  BSP_USART_Status_t:
 *          - BSP_USART_OK: 初始化成功。
 *          - BSP_USART_ERROR_PARAM: RingBuffer 初始化失败。
 * @note    PB10 -> USART3_TX，PB11 -> USART3_RX，用于 ATK-ESP-01 AT 串口。
 *          USART3 波特率为 115200，与 ATK-ESP-01 默认波特率一致。
 *          初始化时先绑定 RingBuffer 接收缓存，再开启 RX DMA circular 和 IDLE 中断。
 */
BSP_USART_Status_t BSP_USART3_Init(void)
{
    RCC_APB2PeriphClockCmd(USART3_GPIO_RCC, ENABLE);
    RCC_APB1PeriphClockCmd(USART3_RCC, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Pin = USART3_RX_PIN;
    GPIO_Init(USART3_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = USART3_TX_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(USART3_PORT, &GPIO_InitStructure);

    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_Init(USART3, &USART_InitStructure);

	RingBuffer_Status_t ringbuffer_status;
	ringbuffer_status = RingBuffer_Init(&usart3_rx_ringbuffer_data, usart3_rx_buffer, USART3_RX_BUFFER_SIZE);
	if(ringbuffer_status != RING_BUFFER_OK)
	{
		return BSP_USART_ERROR_PARAM;
	}

    BSP_USART3_ResetDmaRx();
	USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_Init(&NVIC_InitStructure);

	USART_Cmd(USART3, ENABLE);
    DMA_Cmd(USART3_DMA_RX_CHANNEL, ENABLE);
    USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);

	return BSP_USART_OK;
}


/*
 * @brief  通过 USART3 发送 1 字节数据。
 * @param  send_byte: 待发送字节。
 * @retval BSP_USART_Status_t:
 *         - BSP_USART_OK: 发送成功。
 *         - BSP_USART_ERROR_TIMEOUT: 等待 TXE 置位超时。
 * @note   当前超时时间由 USART_TIMEOUT_US 定义，避免发送等待卡死。
 */
BSP_USART_Status_t BSP_USART3_SendByte(uint8_t send_byte)
{
	uint16_t time_out = USART_TIMEOUT_US;
	while(USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
	{
		Delay_us(1);
		if(time_out-- == 0)
		{
			return BSP_USART_ERROR_TIMEOUT;
		}
	}

	USART_SendData(USART3, send_byte);
	return BSP_USART_OK;
}

/*
 * @brief  通过 USART3 发送字符串。
 * @param  send_str: 待发送的 C 字符串。
 * @retval BSP_USART_Status_t:
 *         - BSP_USART_OK: 发送成功。
 *         - BSP_USART_ERROR_PARAM: send_str 为空。
 *         - BSP_USART_ERROR_TIMEOUT: 某一字节发送超时。
 */
BSP_USART_Status_t BSP_USART3_SendString(const char *send_str)
{
	BSP_USART_Status_t send_status;

	if(send_str == 0)
	{
		return BSP_USART_ERROR_PARAM;
	}

	while(*send_str)
	{
		send_status = BSP_USART3_SendByte((uint8_t)*send_str++);
		if(send_status != BSP_USART_OK)
		{
			return send_status;
		}
	}

	return BSP_USART_OK;
}

/*
 * @brief  清空 USART3 接收 RingBuffer。
 * @retval BSP_USART_Status_t:
 *         - BSP_USART_OK: 清空成功。
 *         - BSP_USART_ERROR_PARAM: RingBuffer 参数异常。
 * @note   清空时临时关闭 USART3 IDLE 中断和 RX DMA，避免接收状态被并发修改。
 */
BSP_USART_Status_t BSP_USART3_ClearRxBuffer(void)
{
	RingBuffer_Status_t ringbuffer_status;

    USART_ITConfig(USART3, USART_IT_IDLE, DISABLE);
    USART_DMACmd(USART3, USART_DMAReq_Rx, DISABLE);
    DMA_Cmd(USART3_DMA_RX_CHANNEL, DISABLE);

    ringbuffer_status = RingBuffer_Clear(&usart3_rx_ringbuffer_data);
    BSP_USART3_ResetDmaRx();

    DMA_Cmd(USART3_DMA_RX_CHANNEL, ENABLE);
    USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);
    USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

	if(ringbuffer_status == RING_BUFFER_ERROR_PARAM)
	{
		return BSP_USART_ERROR_PARAM;
	}
	return BSP_USART_OK;
}

/*
 * @brief  从 USART3 接收 RingBuffer 中读取 1 字节。
 * @param  recv_byte: 输出参数，用于保存读取到的字节。
 * @retval BSP_USART_Status_t:
 *         - BSP_USART_OK: 读取成功。
 *         - BSP_USART_ERROR_PARAM: recv_byte 为空或 RingBuffer 参数异常。
 *         - BSP_USART_ERROR_EMPTY: 当前无可读数据。
 * @note   读取时临时关闭 USART3 IDLE 中断，先把 DMA 新字节搬入 RingBuffer。
 */
BSP_USART_Status_t BSP_USART3_ReadByte(uint8_t *recv_byte)
{
	if(recv_byte == 0)
	{
		return BSP_USART_ERROR_PARAM;
	}

	// 临时关闭 USART3 IDLE 中断，保护接收缓冲区状态
    USART_ITConfig(USART3, USART_IT_IDLE, DISABLE);

    BSP_USART3_FlushDmaRxToRingBuffer();

	RingBuffer_Status_t ring_status;
	ring_status = RingBuffer_ReadByte(&usart3_rx_ringbuffer_data, recv_byte);

	USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

	switch(ring_status)
	{
		case RING_BUFFER_OK: return BSP_USART_OK; break;
		case RING_BUFFER_ERROR_PARAM: return BSP_USART_ERROR_PARAM; break;
		case RING_BUFFER_ERROR_EMPTY: return BSP_USART_ERROR_EMPTY; break;
		
		default: return BSP_USART_ERROR_PARAM; break;
	}
}

/*
 * @brief  获取 USART3 接收 RingBuffer 中当前待读字节数。
 * @retval uint16_t: 当前缓存的待读字节数。
 */
uint16_t BSP_USART3_GetRxCount(void)
{
    USART_ITConfig(USART3, USART_IT_IDLE, DISABLE);
    BSP_USART3_FlushDmaRxToRingBuffer();
    USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

    return RingBuffer_GetLength(&usart3_rx_ringbuffer_data);
}

/*
 * @brief  获取 USART3 接收 RingBuffer 的溢出计数。
 * @retval uint16_t: 缓冲区满时继续接收导致的数据丢弃次数。
 */
uint16_t BSP_USART3_GetOverflowCount(void)
{
    return RingBuffer_GetOverflow(&usart3_rx_ringbuffer_data);
}

/*
 * @brief  复位 USART3 RX DMA 环形缓冲状态。
 * @retval None
 * @note   USART3_RX 在 STM32F103 上映射到 DMA1_Channel3。
 */
static void BSP_USART3_ResetDmaRx(void)
{
    DMA_InitTypeDef DMA_InitStructure;

    usart3_dma_last_pos = 0;

    DMA_DeInit(USART3_DMA_RX_CHANNEL);
    DMA_ClearFlag(DMA1_FLAG_GL3 | DMA1_FLAG_TC3 | DMA1_FLAG_HT3 | DMA1_FLAG_TE3);
    DMA_StructInit(&DMA_InitStructure);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)usart3_dma_rx_buffer;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = USART3_DMA_RX_BUFFER_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(USART3_DMA_RX_CHANNEL, &DMA_InitStructure);
}

/*
 * @brief  将 USART3 DMA 环形缓冲中新收到的字节搬运到现有 RingBuffer。
 * @retval None
 * @note   上层仍从 BSP_USART3_ReadByte() 读字节，DMA 只替换底层收字节方式。
 */
static void BSP_USART3_FlushDmaRxToRingBuffer(void)
{
    uint16_t current_pos;
    uint16_t index;

    current_pos = (uint16_t)(USART3_DMA_RX_BUFFER_SIZE -
                             DMA_GetCurrDataCounter(USART3_DMA_RX_CHANNEL));
    if(current_pos >= USART3_DMA_RX_BUFFER_SIZE)
    {
        current_pos = 0;
    }

    if(current_pos == usart3_dma_last_pos)
    {
        return;
    }

    if(current_pos > usart3_dma_last_pos)
    {
        for(index = usart3_dma_last_pos; index < current_pos; index++)
        {
            (void)RingBuffer_WriteByte(&usart3_rx_ringbuffer_data, usart3_dma_rx_buffer[index]);
        }
    }
    else
    {
        for(index = usart3_dma_last_pos; index < USART3_DMA_RX_BUFFER_SIZE; index++)
        {
            (void)RingBuffer_WriteByte(&usart3_rx_ringbuffer_data, usart3_dma_rx_buffer[index]);
        }

        for(index = 0; index < current_pos; index++)
        {
            (void)RingBuffer_WriteByte(&usart3_rx_ringbuffer_data, usart3_dma_rx_buffer[index]);
        }
    }

    usart3_dma_last_pos = current_pos;
}


/*
 * @brief  USART3 中断服务函数。
 * @retval None
 * @note   IDLE 置位时读取 SR/DR 清标志，并把 DMA 新字节搬运到 USART3 RingBuffer。
 *         ORE 置位时读取 SR 后读取 DR，以清除溢出错误。
 */
void USART3_IRQHandler(void)
{
	volatile uint16_t clear_temp;

	if(USART_GetITStatus(USART3, USART_IT_IDLE) == SET)
	{
        clear_temp = USART3->SR;
        clear_temp = USART3->DR;
        (void)clear_temp;
        BSP_USART3_FlushDmaRxToRingBuffer();
	}

	if(USART_GetFlagStatus(USART3, USART_FLAG_ORE) == SET)
	{
		clear_temp = USART3->SR;
		clear_temp = USART3->DR;
		(void)clear_temp;
        BSP_USART3_FlushDmaRxToRingBuffer();
	}
}

/*
 * @brief  printf 字符输出重定向。
 * @param  ch: printf 输出的字符。
 * @param  f: 标准库文件指针，当前未使用。
 * @retval int: 已输出字符。
 * @note   将 printf 输出重定向到 USART1，便于串口调试。
 */
int fputc(int ch, FILE *f)
{
    BSP_USART1_SendByte((uint8_t)ch);
    return ch;
}
