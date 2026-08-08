#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H
#include "stm32f10x.h"                  // Device header


uint8_t BootJump_IsAppValid(uint32_t app_addr);
void BootJump_PrintAppInfo(uint32_t app_addr);
void BootJump_JumpToApp(uint32_t app_addr);

#endif