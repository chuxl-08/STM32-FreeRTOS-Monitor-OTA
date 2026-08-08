#ifndef FLASH_IF_H
#define FLASH_IF_H
#include "stm32f10x.h"                  // Device header
#include "firmware.h"

typedef enum
{
	FLASHIF_OK=0,
	FLASHIF_ERR_NULL,
	FLASHIF_ERR_OUT_RANGE,
	FLASHIF_ERR_SIZE,
	FLASHIF_ERR_ALIGN,
	FLASHIF_ERR_WRITE,
	FLASHIF_ERR_ERASE,
	FLASHIF_ERR_REGION
} FlashIfErrorCode_t;

/**
 * @brief Flash 区域枚举。
 * @note  
 * 		FLASH_REGION_SLOT_A
 * 		FLASH_REGION_SLOT_B
 * 		FLASH_REGION_CONFIG
 */
typedef enum {
    FLASH_REGION_SLOT_A = 0,
    FLASH_REGION_SLOT_B,
    FLASH_REGION_CONFIG,
} FlashRegion_t;

/**
 * @brief Flash 区域表。
 * @note  
 * 		base：基地址。
 * 		size：大小。
 */
typedef struct {
    uint32_t base;
    uint32_t size;
} FlashRegionInfo_t;

const char *FlashIfErrorCode_String(FlashIfErrorCode_t err);
FlashIfErrorCode_t FlashIf_EraseRegion(FlashRegion_t region, uint32_t offset, uint32_t size);
FlashIfErrorCode_t FlashIf_WriteRegion(FlashRegion_t region, uint32_t offset, const uint8_t *data, uint32_t size);

#endif
