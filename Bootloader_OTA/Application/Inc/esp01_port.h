#ifndef __ESP01_PORT_H
#define __ESP01_PORT_H

#include "stm32f10x.h"
#include <stdint.h>

typedef enum
{
    ESP01_PORT_OK = 0,
    ESP01_PORT_ERR_PARAM,
    ESP01_PORT_ERR_TIMEOUT
} Esp01PortStatus_t;

void Esp01Port_Init(void);
void Esp01Port_ClearRx(void);
void Esp01Port_DelayCycles(volatile uint32_t cycles);
Esp01PortStatus_t Esp01Port_SendByte(uint8_t byte);
Esp01PortStatus_t Esp01Port_SendString(const char *str);
uint8_t Esp01Port_TryReadByte(uint8_t *byte);

#endif
