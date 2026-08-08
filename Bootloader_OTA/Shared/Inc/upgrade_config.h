#ifndef UPGRADE_CONFIG_H
#define UPGRADE_CONFIG_H

#include <stdint.h>
#include "firmware.h"
#include "memory_map.h"

#define UPGRADE_CONFIG_MAGIC        0x55434647UL
#define UPGRADE_CONFIG_VERSION      1UL

typedef enum
{
    UPGRADE_STATE_IDLE = 0,
    UPGRADE_STATE_PENDING_VERIFY,
    UPGRADE_STATE_TESTING,
    UPGRADE_STATE_CONFIRMED
} UpgradeState_t;

/**
 * @brief 升级状态。
 * @note  
 * 		magic: Config 识别码。
 * 		version: Config 结构体格式版本。
 * 		sequence: Config 写入序号。
 * 		state: 当前升级状态。
 *      confirmed_slot: 当前已确认可运行的 slot。
 *      pending_slot：新固件写入的 slot，等待测试。
 *      boot_slot：本次 Bootloader 准备启动的 slot。
 *      rollback_slot：新 slot 失败时回退的旧 slot。
 * 		slot_a_version: slot_a 区版本号。
 * 		slot_b_version: slot_b 区版本号。
 * 		boot_attempts: 启动次数。
 * 		pending_header: 待升级固件的 FirmwareHeader_t 完整包头。
 * 		config_crc32: Config CRC 校验值。
 */
typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    UpgradeState_t state;
    BootSlot_t confirmed_slot;
    BootSlot_t pending_slot;
    BootSlot_t boot_slot;
    BootSlot_t rollback_slot;
    uint32_t slot_a_version;
    uint32_t slot_b_version;
    uint32_t boot_attempts;
    FirmwareHeader_t pending_header;
    uint32_t config_crc32;
} UpgradeConfig_t;

static inline const char *UpgradState_String(UpgradeState_t state)
{
    switch (state)
    {
        case UPGRADE_STATE_IDLE:
			return "IDLE";
		case UPGRADE_STATE_PENDING_VERIFY:
			return "PENDING_VERIFY";
		case UPGRADE_STATE_TESTING:
			return "TESTING";
		case UPGRADE_STATE_CONFIRMED:
			return "CONFIRMED";
		default:
			return "UNKNOWN";
    }
}

#endif
