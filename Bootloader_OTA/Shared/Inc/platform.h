#ifndef PLATFORM_H
#define PLATFORM_H
#include "stm32f10x.h"

void Platform_InitUsart1(void);
void Platform_PutChar(uint8_t send_byte);
void Platform_PutString(const char *send_str);
void Platform_Delay(volatile unsigned int cycles);
uint8_t Platform_TryGetChar(uint8_t *out);

#endif
