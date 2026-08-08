#include "stm32f10x.h"
#include "dht11.h"
#include "Delay.h"
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

/*
 * 文件职责：
 * 1. 实现 DHT11 单总线驱动。
 * 2. 提供温度、湿度和数据有效标志。
 * 3. 处理上电稳定时间、单总线超时和校验错误。
 */

#define DHT11_PORT              GPIOC
#define DHT11_PIN               GPIO_Pin_2
#define DHT11_RCC               RCC_APB2Periph_GPIOC
#define DHT11_TIMEOUT           100

 static uint8_t s_dht11_init_status = 0;

/*
 * @brief   向 DHT11 发送一个位。
 * @param   bit: 要发送的位值。
 * @param   duration_us: 发送持续时间（微秒）。
 */
static void DHT11_WriteBit(BitAction bit, uint16_t duration_us)
{
    GPIO_WriteBit(DHT11_PORT, DHT11_PIN, bit);
    Delay_us(duration_us);
}

/*
 * @brief   DHT11 初始化函数。
 * @retval  DHT11_Status_t:
 *          - DHT11_OK: 初始化成功
 * @note    配置 GPIOC Pin 2 为开漏输出，初始拉高。上电后等待至少 1 秒的稳定时间。
 */
DHT11_Status_t DHT11_Init(void)
{
    if(s_dht11_init_status)
    {
        return DHT11_OK;
    }

    RCC_APB2PeriphClockCmd(DHT11_RCC, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Pin = DHT11_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_PORT, &GPIO_InitStructure);

    GPIO_WriteBit(DHT11_PORT, DHT11_PIN, Bit_SET);
    Delay_s(1);

    s_dht11_init_status = 1;

    return DHT11_OK;
}

/*
 * @brief   DHT11 发送起始读信号
*/
static void DHT11_BeginRead(void)
{
    DHT11_WriteBit(Bit_RESET, 25000);
    DHT11_WriteBit(Bit_SET, 30);
}

/*
 * @brief   监控 DHT11 引脚电平
 * @param   expected_level: 期望的电平值
 * @param   timeout_us: 超时时间（微秒）
 * @retval  DHT11_Status_t:
 *          - DHT11_OK: 监控成功
 *          - DHT11_ERROR_TIMEOUT: 超时错误
 */
static DHT11_Status_t DHT11_LevelMonitor(uint8_t expected_level, uint16_t timeout_us)
{
    while(GPIO_ReadInputDataBit(DHT11_PORT, DHT11_PIN) == expected_level)
    {
        if (timeout_us == 0)
        {
            return DHT11_ERROR_TIMEOUT;
        }
        timeout_us--;
        Delay_us(1);
    }
    return DHT11_OK;
}

/*
 * @brief   检查 DHT11 是否开始发送数据传输起始信号
 * @retval  DHT11_Status_t:
 *          - DHT11_OK: 检查成功
 *          - DHT11_ERROR_TIMEOUT: 超时错误
 */
static DHT11_Status_t DHT11_BeginsWithStartSignal(void)
{
    if(DHT11_LevelMonitor(Bit_SET, DHT11_TIMEOUT) != DHT11_OK)
    {
        return DHT11_ERROR_TIMEOUT;
    }
    if(DHT11_LevelMonitor(Bit_RESET, DHT11_TIMEOUT) != DHT11_OK)
    {
        return DHT11_ERROR_TIMEOUT;
    }
    if(DHT11_LevelMonitor(Bit_SET, DHT11_TIMEOUT) != DHT11_OK)
    {
        return DHT11_ERROR_TIMEOUT;
    }
    return DHT11_OK;
}

/*
 * @brief   读取 DHT11 的一个位
 * @param   bit: 指向存储读取位的指针
 * @retval  DHT11_Status_t:
 *          - DHT11_OK: 读取成功
 *          - DHT11_ERROR_TIMEOUT: 超时错误
 */
static DHT11_Status_t DHT11_ReadBit(uint8_t *bit)
{
    if(DHT11_LevelMonitor(Bit_RESET, DHT11_TIMEOUT) != DHT11_OK)
    {
        return DHT11_ERROR_TIMEOUT;
    }

    Delay_us(40);
    *bit = GPIO_ReadInputDataBit(DHT11_PORT, DHT11_PIN);

    if(DHT11_LevelMonitor(Bit_SET, DHT11_TIMEOUT) != DHT11_OK)
    {
        return DHT11_ERROR_TIMEOUT;
    }

    return DHT11_OK;
}

/*
 * @brief   从 DHT11 读取温湿度数据
 * @param   data: 指向 DHT11_Data_t 结构体的指针，用于存储读取到的数据
 * @retval  DHT11_Status_t:
 *          - DHT11_OK: 读取成功，data 中的 temperature、humidity 和 valid 字段有效
 *          - DHT11_ERROR_PARAM: data 参数为 NULL
 *          - DHT11_ERROR_TIMEOUT: 读取过程中发生超时错误
 *          - DHT11_ERROR_START_SIGNAL: 起始信号错误
 *          - DHT11_ERROR_READ_BIT: 读取位错误
 *          - DHT11_ERROR_CHECKSUM: 校验错误
 * @note    通过临界保护区保护 dht11 数据读取过程
 */
DHT11_Status_t DHT11_Read(DHT11_Data_t *data)
{
    uint8_t buffer[5] = {0};
    uint8_t result;
    DHT11_Status_t status = DHT11_OK;

    if(data == 0)
    {
        return DHT11_ERROR_PARAM;
    }

    data->temperature = 0;
    data->humidity = 0;
    data->valid = 0;

    DHT11_BeginRead();

    taskENTER_CRITICAL();   // 进入 FreeRTOS 临界保护区

    if(DHT11_BeginsWithStartSignal() != DHT11_OK)
    {
        status = DHT11_ERROR_START_SIGNAL;
    }
    else
    {
        for(uint8_t byte_index = 0; byte_index < 5; byte_index++)
        {
            for(uint8_t bit_index = 0; bit_index < 8; bit_index++)
            {
                uint8_t bit;

                if(DHT11_ReadBit(&bit) != DHT11_OK)
                {
                    status = DHT11_ERROR_READ_BIT;
                    break;
                }

                buffer[byte_index] <<= 1;
                buffer[byte_index] |= bit;
            }

            if(status != DHT11_OK)
            {
                break;
            }
        }
    }

    taskEXIT_CRITICAL();    // 退出 FreeRTOS 临界保护区

    if(status != DHT11_OK)
    {
        return status;
    }

    result = (uint8_t)(buffer[0] + buffer[1] + buffer[2] + buffer[3]);
    if(buffer[4] != result)
    {
        return DHT11_ERROR_CHECKSUM;
    }

    data->temperature = buffer[2];
    data->humidity = buffer[0];
    data->valid = 1;

    return DHT11_OK;
}


