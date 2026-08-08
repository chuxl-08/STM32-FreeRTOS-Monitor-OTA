#ifndef __DHT11_H
#define __DHT11_H
#include "stm32f10x.h"
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明 DHT11 温湿度传感器驱动接口。
 * 2. 负责单总线时序、40 bit 数据读取、校验和有效性判断。
 * 3. 超时保护，避免传感器异常导致系统卡死。
 */

/*
 * @brief   DHT11_Data_t: dht11温湿度数据
 * @note
 *          - temperature: 温度 单位：1 C
 *          - humidity: 湿度 1%RH
 *          - valid: 数据有效性
 *              1 ： 有效
 *              0 ： 无效
 */
typedef struct
{
    uint8_t temperature;
    uint8_t humidity;
    uint8_t valid;
} DHT11_Data_t;

DHT11_Status_t DHT11_Init(void);
DHT11_Status_t DHT11_Read(DHT11_Data_t *data);

#endif /* __DHT11_H */
