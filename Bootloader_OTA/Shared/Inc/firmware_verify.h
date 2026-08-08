#ifndef FIRMWARE_VERIFY_H
#define FIRMWARE_VERIFY_H
#include "firmware.h"
#include "memory_map.h"


/**
 * @brief 固件Header校验结果。
 * @note
 *      FW_VERIFY_OK: OK
 *      FW_VERIFY_PARAM: 函数参数错误。
 *      FW_VERIFY_ERR_MAGIC: 包头识别码错误。
 *      FW_VERIFY_ERR_HEADER_VERSION: 包头格式版本错误。
 *      FW_VERIFY_ERR_IMAGE_TYPE: 镜像类型错误。
 *      FW_VERIFY_ERR_IMAGE_SIZE: 镜像字节数错误。
 *      FW_VERIFY_ERR_LOAD_ADDRESS: 目标写入地址错误。
 *      FW_VERIFY_ERR_LOAD_RANGE: 写入镜像越界错误。
 *      FW_VERIFY_ERR_ENTRY_ADDRESS: App Reset_Handler地址错误。
 *      FW_VERIFY_ERR_HEADER_CRC: 固件Header crc校验错误。
 *      FW_VERIFY_ERR_IMAGE_ADDRESS：镜像地址错误。
 *      FW_VERIFY_ERR_IMAGE_CRC：镜像CRC校验错误。
 */
typedef enum
{
    FW_VERIFY_OK = 0,
    FW_VERIFY_ERR_PARAM,
    FW_VERIFY_ERR_MAGIC,
    FW_VERIFY_ERR_HEADER_VERSION,
    FW_VERIFY_ERR_IMAGE_TYPE,
    FW_VERIFY_ERR_IMAGE_SIZE,
    FW_VERIFY_ERR_LOAD_ADDRESS,
    FW_VERIFY_ERR_LOAD_RANGE,
    FW_VERIFY_ERR_ENTRY_ADDRESS,
    FW_VERIFY_ERR_HEADER_CRC,
    FW_VERIFY_ERR_IMAGE_ADDRESS,
    FW_VERIFY_ERR_IMAGE_CRC
} FirmwareVerifyResult_t;

const char *FirmwareVerify_ResultString(FirmwareVerifyResult_t result);
FirmwareVerifyResult_t FirmwareVerify_HeaderForSlot(const FirmwareHeader_t *header, BootSlot_t target_slot);
FirmwareVerifyResult_t FirmwareVerify_ImageInSlot(const FirmwareHeader_t *header, BootSlot_t target_slot);

#endif

