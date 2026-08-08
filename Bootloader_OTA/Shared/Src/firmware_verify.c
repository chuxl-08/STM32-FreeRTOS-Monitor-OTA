#include "firmware_verify.h"
#include "memory_map.h"
#include "crc32.h"

/**
 * @brief 校验结果转字符串。
 * @param result: 校验函数返回结果。
 * @return
 *      String
 * @note
 *
*/
const char *FirmwareVerify_ResultString(FirmwareVerifyResult_t result)
{
	switch (result)
	{
		case FW_VERIFY_OK:
			return "OK";
		case FW_VERIFY_ERR_PARAM:
			return "PARAM";
		case FW_VERIFY_ERR_MAGIC:
			return "MAGIC";
		case FW_VERIFY_ERR_HEADER_VERSION:
			return "HEADER_VERSION";
		case FW_VERIFY_ERR_IMAGE_TYPE:
			return "IMAGE_TYPE";
		case FW_VERIFY_ERR_IMAGE_SIZE:
			return "IMAGE_SIZE";
		case FW_VERIFY_ERR_LOAD_ADDRESS:
			return "LOAD_ADDRESS";
		case FW_VERIFY_ERR_LOAD_RANGE:
			return "LOAD_RANGE";
		case FW_VERIFY_ERR_ENTRY_ADDRESS:
			return "ENTRY_ADDRESS";
		case FW_VERIFY_ERR_HEADER_CRC:
			return "HEADER_CRC";
		case FW_VERIFY_ERR_IMAGE_ADDRESS:
			return "IMAGE_ADDRESS";
		case FW_VERIFY_ERR_IMAGE_CRC:
			return "IMAGE_CRC";
		default:
			return "UNKNOWN";
	}
}

/**
 * @brief 固件 Header 校验函数（包含目标slot区域校验）。
 * @param {FirmwareHeader_t} *header 固件Header指针。
 * @param {BootSlot_t} target_slot 目标 slot区域。
 * @return {FirmwareVerifyResult_t}
 * 			- FW_VERIFY_OK: 校验无错误。
 * 			- FW_VERIFY_PARAM: 函数参数错误。
 * 			- FW_VERIFY_ERR_MAGIC: 包头识别码错误。
 * 			- FW_VERIFY_ERR_HEADER_VERSION: 包头格式版本错误。
 * 			- FW_VERIFY_ERR_IMAGE_TYPE：镜像类型错误。
 * 			- FW_VERIFY_ERR_IMAGE_SIZE: 镜像字节数错误。
 * 			- FW_VERIFY_ERR_LOAD_ADDRESS: 目标写入地址错误。
 * 			- FW_VERIFY_ERR_LOAD_RANGE: 写入镜像越界错误。
 * 			- FW_VERIFY_ERR_ENTRY_ADDRESS: App Reset_Handler地址错误。
 * 			- FW_VERIFY_ERR_HEADER_CRC: 固件Header crc校验错误。
 * @note
 * 		header 非空；magic/header_version/image_type 正确；image_size > 0 且不超过 target slot；
 * 		header_crc32 正确；load_address 和 entry_address 必须落在目标 slot。
 */
FirmwareVerifyResult_t FirmwareVerify_HeaderForSlot(const FirmwareHeader_t *header, BootSlot_t target_slot)
{
	if(header == 0)
	{
		return FW_VERIFY_ERR_PARAM;
	}

	if(MemoryMap_IsSlotValid(target_slot) == 0U)
	{
		return FW_VERIFY_ERR_PARAM;
	}

	if(header->magic != FIRMWARE_MAGIC)
	{
		return FW_VERIFY_ERR_MAGIC;
	}

	if(header->header_version != FIRMWARE_HEADER_VERSION)
	{
		return FW_VERIFY_ERR_HEADER_VERSION;
	}

	if(header->image_type != FIRMWARE_IMAGE_TYPE_APP)
	{
		return FW_VERIFY_ERR_IMAGE_TYPE;
	}

	if(!(header->image_size > 0))
	{
		return FW_VERIFY_ERR_IMAGE_SIZE;
	}

	if(header->load_address != MemoryMap_SlotBase(target_slot))
	{
		return FW_VERIFY_ERR_LOAD_ADDRESS;
	}

	if(!(MemoryMap_IsSlotRange(target_slot, header->load_address, header->image_size)))
	{
		return FW_VERIFY_ERR_LOAD_RANGE;
	}

	if(((header->entry_address) & 1UL) == 0UL || !(MemoryMap_IsSlotRange(target_slot, (header->entry_address & ~1UL), 4)))
	{
		return FW_VERIFY_ERR_ENTRY_ADDRESS;
	}

	FirmwareHeader_t header_temp = *header;
	uint32_t header_expect_crc = header->header_crc32;
	uint32_t header_crc;
	header_temp.header_crc32 = 0;

	header_crc = Crc32_Calculate(&header_temp, sizeof(FirmwareHeader_t));

	if(header_crc != header_expect_crc)
	{
		return FW_VERIFY_ERR_HEADER_CRC;
	}

	return FW_VERIFY_OK;
}

/**
 * @brief Slot 区镜像校验。
 * @param {FirmwareHeader_t} *header 固件 Header 指针。
 * @param {BootSlot_t} target_slot 目标 slot 区域。
 * @return {FirmwareVerifyResult_t}
 * 			- FW_VERIFY_OK: 校验无错误。
 * 			- FW_VERIFY_PARAM: 函数参数错误。
 * 			- FW_VERIFY_ERR_IMAGE_ADDRESS：镜像地址错误。
 * 			- FW_VERIFY_ERR_IMAGE_CRC：镜像CRC校验错误。
 * @note
 */
FirmwareVerifyResult_t FirmwareVerify_ImageInSlot(const FirmwareHeader_t *header, BootSlot_t target_slot)
{
	if(header == 0)
	{
		return FW_VERIFY_ERR_PARAM;
	}

	if(MemoryMap_IsSlotValid(target_slot) == 0U)
	{
		return FW_VERIFY_ERR_PARAM;
	}

	uint32_t image_size = header->image_size;
	uint32_t image_expect_crc = header->image_crc32;
	uint32_t crc32_result;

	if(MemoryMap_IsSlotRange(target_slot, MemoryMap_SlotBase(target_slot), header->image_size) == 0U)
	{
		return FW_VERIFY_ERR_IMAGE_ADDRESS;
	}

	crc32_result = Crc32_Calculate((uint32_t *)MemoryMap_SlotBase(target_slot), image_size);

	if(crc32_result != image_expect_crc)
	{
		return FW_VERIFY_ERR_IMAGE_CRC;
	}

	return FW_VERIFY_OK;
}
