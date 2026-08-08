#include "esp01_port.h"

#define ESP01_PORT_USART                 USART3
#define ESP01_PORT_USART_CLK             RCC_APB1Periph_USART3
#define ESP01_PORT_GPIO_CLK              RCC_APB2Periph_GPIOB
#define ESP01_PORT_GPIO                  GPIOB
#define ESP01_PORT_TX_PIN                GPIO_Pin_10
#define ESP01_PORT_RX_PIN                GPIO_Pin_11
#define ESP01_PORT_BAUDRATE              115200U
#define ESP01_PORT_TX_TIMEOUT_CYCLES     200000U

/**
 * @brief ESP01 底层 USART3 初始化。
 * @param
 * @return
 *      None
 * @note
 *      使用 SPL 初始化 USART3，PB10 为 TX，PB11 为 RX，波特率 115200。
 */
void Esp01Port_Init(void)
{
    GPIO_InitTypeDef gpio_init;
    USART_InitTypeDef usart_init;

    RCC_APB2PeriphClockCmd(ESP01_PORT_GPIO_CLK | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(ESP01_PORT_USART_CLK, ENABLE);

    GPIO_StructInit(&gpio_init);
    gpio_init.GPIO_Pin = ESP01_PORT_TX_PIN;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(ESP01_PORT_GPIO, &gpio_init);

    GPIO_StructInit(&gpio_init);
    gpio_init.GPIO_Pin = ESP01_PORT_RX_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(ESP01_PORT_GPIO, &gpio_init);

    USART_DeInit(ESP01_PORT_USART);
    USART_StructInit(&usart_init);
    usart_init.USART_BaudRate = ESP01_PORT_BAUDRATE;
    usart_init.USART_WordLength = USART_WordLength_8b;
    usart_init.USART_StopBits = USART_StopBits_1;
    usart_init.USART_Parity = USART_Parity_No;
    usart_init.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart_init.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(ESP01_PORT_USART, &usart_init);

    USART_Cmd(ESP01_PORT_USART, ENABLE);
    Esp01Port_ClearRx();
}

/**
 * @brief 清空 ESP01 串口接收缓存和溢出标志。
 * @param
 * @return
 *      None
 * @note
 *      发送新 AT 命令前调用，避免上一次残留应答影响本次字符串匹配。
 */
void Esp01Port_ClearRx(void)
{
    volatile uint16_t dummy;

    while (USART_GetFlagStatus(ESP01_PORT_USART, USART_FLAG_RXNE) == SET)
    {
        dummy = USART_ReceiveData(ESP01_PORT_USART);
        (void)dummy;
    }

    if (USART_GetFlagStatus(ESP01_PORT_USART, USART_FLAG_ORE) == SET)
    {
        dummy = ESP01_PORT_USART->SR;
        dummy = ESP01_PORT_USART->DR;
        (void)dummy;
    }
}

/**
 * @brief 简单阻塞延时。
 * @param cycles：延时周期数。
 * @return
 *      None
 * @note
 *    
 */
void Esp01Port_DelayCycles(volatile uint32_t cycles)
{
    while (cycles-- > 0U)
    {
        __NOP();
    }
}

/**
 * @brief ESP01 串口发送 1 字节数据。
 * @param byte：待发送字节。
 * @return
 *      ESP01_PORT_OK：发送成功。
 *      ESP01_PORT_ERR_TIMEOUT：等待 TXE 超时。
 * @note
 *      通过 USART3 发送，当前为阻塞式发送。
 */
Esp01PortStatus_t Esp01Port_SendByte(uint8_t byte)
{
    uint32_t timeout = ESP01_PORT_TX_TIMEOUT_CYCLES;

    while (USART_GetFlagStatus(ESP01_PORT_USART, USART_FLAG_TXE) == RESET)
    {
        if (timeout-- == 0U)
        {
            return ESP01_PORT_ERR_TIMEOUT;
        }
    }

    USART_SendData(ESP01_PORT_USART, byte);
    return ESP01_PORT_OK;
}

/**
 * @brief ESP01 串口发送字符串。
 * @param str：待发送字符串指针。
 * @return
 *      ESP01_PORT_OK：发送成功。
 *      ESP01_PORT_ERR_PARAM：str 为空指针。
 *      ESP01_PORT_ERR_TIMEOUT：发送字符时等待 TXE 超时。
 * @note
 *      调用者负责在 AT 命令字符串末尾包含 "\r\n"。
 */
Esp01PortStatus_t Esp01Port_SendString(const char *str)
{
    if (str == 0)
    {
        return ESP01_PORT_ERR_PARAM;
    }

    while (*str != '\0')
    {
        if (Esp01Port_SendByte((uint8_t)*str) != ESP01_PORT_OK)
        {
            return ESP01_PORT_ERR_TIMEOUT;
        }
        str++;
    }

    return ESP01_PORT_OK;
}

/**
 * @brief ESP01 串口非阻塞读取 1 字节数据。
 * @param byte：接收字节输出指针。
 * @return
 *      0：未读到数据或参数错误。
 *      1：成功读到 1 字节数据。
 * @note
 *      当前供 AT 解析模块轮询调用。
 */
uint8_t Esp01Port_TryReadByte(uint8_t *byte)
{
    if (byte == 0)
    {
        return 0U;
    }

    if (USART_GetFlagStatus(ESP01_PORT_USART, USART_FLAG_RXNE) == SET)
    {
        *byte = (uint8_t)USART_ReceiveData(ESP01_PORT_USART);
        return 1U;
    }

    if (USART_GetFlagStatus(ESP01_PORT_USART, USART_FLAG_ORE) == SET)
    {
        Esp01Port_ClearRx();
    }

    return 0U;
}
