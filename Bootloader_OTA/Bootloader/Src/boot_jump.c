#include "stm32f10x.h"                  // Device header
#include "core_cm3.h"
#include "boot_jump.h"
#include "memory_map.h"
#include <stdio.h>

typedef void (*AppEntry_t)(void);

static AppEntry_t s_app_entry;

/**
 * @brief 跳转前 App 地址有效判断
 * @param app_addr App 地址，必须是 Slot A 或 Slot B 起始地址
 * @return
 *      0：无效
 *		1: 有效
 * @note 判断：
   - MSP 在 SRAM 范围内
   - Reset_Handler 去掉 bit0 后在当前 Slot 范围内
   - Reset_Handler bit0 为 1
 */
uint8_t BootJump_IsAppValid(uint32_t app_addr)
{
	uint8_t msp_valid;
    uint8_t reset_handler_valid;
	volatile const uint32_t *vector = (volatile const uint32_t *)app_addr;
    BootSlot_t slot;

	uint32_t msp = vector[0];
    uint32_t raw_reset_handler = vector[1];

	uint32_t reset_handler = raw_reset_handler & ~1UL;
    slot = MemoryMap_SlotFromLoadAddress(app_addr);

	msp_valid = (uint8_t)MemoryMap_IsSRAMRange(msp, 0);
	if(((raw_reset_handler & 1UL) != 0UL) && MemoryMap_IsSlotValid(slot))
	{
		reset_handler_valid = (uint8_t)(MemoryMap_IsSlotRange(slot, reset_handler, 4));
	}
	else
	{
		reset_handler_valid = 0;
	}

    return msp_valid && reset_handler_valid;
}

/**
 * @brief 打印app向量表的部分信息
 * @param app_addr App 地址
 * @return
 *      None
 * @note
 */
void BootJump_PrintAppInfo(uint32_t app_addr)
{
	volatile const uint32_t *vector = (volatile const uint32_t *)app_addr;

	uint32_t msp = vector[0];
    uint32_t reset_handler = vector[1];
	reset_handler = reset_handler & ~1UL;

	printf("[BOOT] MSP=0x%x\r\n", msp);
	printf("[BOOT] Reset=0x%x\r\n", reset_handler);
}

/**
 * @brief 跳转函数
 * @param
 * @return
 *      None
 * @note
 */
void BootJump_JumpToApp(uint32_t app_addr)
{
	volatile const uint32_t *vector = (volatile const uint32_t *)app_addr;

	uint32_t app_msp = vector[0];
    uint32_t app_reset = vector[1];

	s_app_entry = (AppEntry_t)app_reset;

	__disable_irq();

	SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

	for (uint32_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

	// 设置向量表
	SCB->VTOR = app_addr;
    __DSB();
    __ISB();

	// 设置栈指针 指向app栈顶
	__set_MSP(app_msp);

	s_app_entry();

	while (1)
    {
    }
}
