#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <stdint.h>

#define FIRMWARE_MAGIC              0x424F5441UL
#define FIRMWARE_HEADER_VERSION     1UL
#define FIRMWARE_IMAGE_TYPE_APP     1UL

/**
 * @brief 固件 Header
 * @note  40 bytes
 * 		magic: 包识别码，确认这是本项目固件包
 * 		header_version: 包头格式版本
 * 		image_type: 镜像类型，目前是 App
 * 		image_version: App 版本号，例如 v1
 * 		image_size: 后面 raw image 的字节数
 * 		image_crc32: 对 raw image 计算 CRC
 * 		load_address: 固件目标写入地址，A/B 双槽下为 Slot A 或 Slot B 起始地址
 * 		entry_address: Reset_Handler 地址
 * 		flags: 保留扩展字段
 * 		header_crc32: 对 header 自身做 CRC，计算时该字段先置 0
 */
typedef struct
{
    uint32_t magic;
    uint32_t header_version;
    uint32_t image_type;
    uint32_t image_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t load_address;
    uint32_t entry_address;
    uint32_t flags;
    uint32_t header_crc32;
} FirmwareHeader_t;

#endif
