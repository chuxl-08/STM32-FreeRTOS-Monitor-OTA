#include "flash_if.h"
#include "stm32f10x_flash.h"
#include "memory_map.h"

/**
 * @brief FlashIfErrorCode_t转字符串。
 * @param err: 错误码。
 * @return
 *      String
 * @note
 *
*/
const char *FlashIfErrorCode_String(FlashIfErrorCode_t err)
{
	switch (err)
	{
		case FLASHIF_OK:
			return "OK";
		case FLASHIF_ERR_NULL:
			return "NULL";
		case FLASHIF_ERR_OUT_RANGE:
			return "OUT_RANGE";
		case FLASHIF_ERR_SIZE:
			return "SIZE";
		case FLASHIF_ERR_ALIGN:
			return "ALIGN";
		case FLASHIF_ERR_WRITE:
			return "WRITE";
		case FLASHIF_ERR_ERASE:
			return "ERASE";
		case FLASHIF_ERR_REGION:
			return "REGION";
		default:
			return "UNKNOWN";
	}
}

/**
 * @brief 按逻辑区域擦除 Flash。
 * @param region Flash 逻辑区域，Slot_A、Slot_B 或 Config 区域。
 * @param offset 区域内偏移地址，单位 Byte，必须按 Flash 页大小对齐。
 * @param size 需要擦除的字节数，函数内部按页向上取整。
 * @return
 *      FLASHIF_OK：擦除成功。
 *      FLASHIF_ERR_REGION：region 参数非法。
 *      FLASHIF_ERR_SIZE：size 为 0。
 *      FLASHIF_ERR_ALIGN：offset 未按 Flash 页大小对齐。
 *      FLASHIF_ERR_OUT_RANGE：offset + size 超出指定区域。
 *      FLASHIF_ERR_ERASE：底层 Flash 擦除失败。
 * @note
 *      该函数只允许擦除指定逻辑区域内部的 Flash，调用者传入的是区域内 offset，
 *      不是绝对 Flash 地址。避免误擦 Bootloader 区或其他不属于当前操作的区域。
 */
FlashIfErrorCode_t FlashIf_EraseRegion(FlashRegion_t region, uint32_t offset, uint32_t size)
{
    FlashRegionInfo_t region_info;
    uint32_t erase_start_addr;
    uint32_t erase_pages_count;
    uint32_t page_num;
    FLASH_Status erase_status;

    switch (region)
    {
        case FLASH_REGION_SLOT_A:
            region_info.base = SLOT_A_BASE_ADDR;
            region_info.size = SLOT_A_SIZE;
            break;

        case FLASH_REGION_SLOT_B:
            region_info.base = SLOT_B_BASE_ADDR;
            region_info.size = SLOT_B_SIZE;
            break;

        case FLASH_REGION_CONFIG:
            region_info.base = UPGRADE_CONFIG_BASE_ADDR;
            region_info.size = UPGRADE_CONFIG_SIZE;
            break;

        default:
            return FLASHIF_ERR_REGION;
    }

    if (size == 0U)
    {
        return FLASHIF_ERR_SIZE;
    }

    if ((offset % FLASH_PAGE_SIZE) != 0U)
    {
        return FLASHIF_ERR_ALIGN;
    }

    if ((offset > region_info.size) || (size > (region_info.size - offset)))
    {
        return FLASHIF_ERR_OUT_RANGE;
    }

    erase_start_addr = region_info.base + offset;
    erase_pages_count = (size + FLASH_PAGE_SIZE - 1U) / FLASH_PAGE_SIZE;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    for (page_num = 0U; page_num < erase_pages_count; page_num++)
    {
        erase_status = FLASH_ErasePage(erase_start_addr + page_num * FLASH_PAGE_SIZE);
        if (erase_status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return FLASHIF_ERR_ERASE;
        }
    }

    FLASH_Lock();

    return FLASHIF_OK;
}

/**
 * @brief 按逻辑区域写入 Flash。
 * @param region Flash 逻辑区域，Slot A、Slot B 或 Config。
 * @param offset 区域内偏移地址，单位 Byte，必须半字对齐。
 * @param data 待写入数据指针。
 * @param size 写入字节数。
 * @return
 *      FLASHIF_OK：写入成功。
 *      FLASHIF_ERR_REGION：region 参数非法。
 *      FLASHIF_ERR_NULL：data 为空指针。
 *      FLASHIF_ERR_SIZE：size 为 0。
 *      FLASHIF_ERR_ALIGN：offset 未半字对齐。
 *      FLASHIF_ERR_OUT_RANGE：offset + size 超出指定区域。
 *      FLASHIF_ERR_WRITE：底层 Flash 写入失败。
 * @note
 *      STM32F103 Flash 按 half-word，即 16 bit 编程。
 *      如果 size 是奇数，最后 1 个字节会和 0xFF 组成一个 half-word 写入。
 *      因此范围检查使用向上对齐后的 write_size，避免最后一次 half-word 写出区域边界。
 */
FlashIfErrorCode_t FlashIf_WriteRegion(FlashRegion_t region,
                                        uint32_t offset,
                                        const uint8_t *data,
                                        uint32_t size)
{
    FlashRegionInfo_t region_info;
    uint32_t addr;
    uint32_t write_size;
    uint32_t write_offset;
    uint16_t halfword;
    FLASH_Status status;

    switch (region)
    {
        case FLASH_REGION_SLOT_A:
            region_info.base = SLOT_A_BASE_ADDR;
            region_info.size = SLOT_A_SIZE;
            break;

        case FLASH_REGION_SLOT_B:
            region_info.base = SLOT_B_BASE_ADDR;
            region_info.size = SLOT_B_SIZE;
            break;

        case FLASH_REGION_CONFIG:
            region_info.base = UPGRADE_CONFIG_BASE_ADDR;
            region_info.size = UPGRADE_CONFIG_SIZE;
            break;

        default:
            return FLASHIF_ERR_REGION;
    }

    if (data == 0)
    {
        return FLASHIF_ERR_NULL;
    }

    if (size == 0U)
    {
        return FLASHIF_ERR_SIZE;
    }

    if ((offset & 1U) != 0U)
    {
        return FLASHIF_ERR_ALIGN;
    }

    write_size = (size + 1U) & ~1U;

    if ((offset > region_info.size) || (write_size > (region_info.size - offset)))
    {
        return FLASHIF_ERR_OUT_RANGE;
    }

    addr = region_info.base + offset;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    for (write_offset = 0U; write_offset < size; write_offset += 2U)
    {
        halfword = data[write_offset];

        if ((write_offset + 1U) < size)
        {
            halfword |= ((uint16_t)data[write_offset + 1U] << 8U);
        }
        else
        {
            halfword |= 0xFF00U;
        }

        status = FLASH_ProgramHalfWord(addr + write_offset, halfword);
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return FLASHIF_ERR_WRITE;
        }
    }

    FLASH_Lock();

    return FLASHIF_OK;
}
