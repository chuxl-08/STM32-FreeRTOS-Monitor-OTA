#ifndef SERIAL_IAP_H
#define SERIAL_IAP_H
#include "stm32f10x.h"                  // Device header


uint8_t SerialIap_WaitEnter(uint32_t timeout_cycles);
uint8_t SerialIap_WaitSync(uint32_t timeout_cycles);
uint8_t SerialIap_ReadExact(uint8_t *data_p, uint32_t data_size, uint32_t timeout_cycles);
void SerialIap_SendReadyImage(void);
void SerialIap_SendAck(void);
void SerialIap_SendNack(void);

#endif
