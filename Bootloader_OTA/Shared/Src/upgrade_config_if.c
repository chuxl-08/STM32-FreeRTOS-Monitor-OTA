#include "upgrade_config_if.h"
#include "memory_map.h"
#include "crc32.h"

/**
 * @brief 判断升级状态是否属于当前 A/B 主线
 * @param state 待检查的升级状态
 * @return 1：合法状态；0：非法状态
 */
static uint8_t UpgradeConfig_IsStateValid(UpgradeState_t state)
{
	switch (state)
	{
		case UPGRADE_STATE_IDLE:
		case UPGRADE_STATE_PENDING_VERIFY:
		case UPGRADE_STATE_TESTING:
		case UPGRADE_STATE_CONFIRMED:
			return 1U;
		default:
			return 0U;
	}
}

/**
 * @brief UpgradeConfigIfErrorCode_t转字符串。
 * @param err: 错误码。
 * @return
 *      String
 * @note
 *
*/
const char *UpgradeConfigIfErrorCode_String(UpgradeConfigIfErrorCode_t err)
{
	switch (err)
	{
		case UpgradeConfigIf_OK:
			return "OK";
		case UpgradeConfigIf_ERR_PARAM:
			return "PARAM";
		case UpgradeConfigIf_ERR_MAGIC:
			return "MAGIC";
		case UpgradeConfigIf_ERR_VERSION:
			return "VERSION";
		case UpgradeConfigIf_ERR_STATE:
			return "STATE";
		case UpgradeConfigIf_ERR_CRC:
			return "CRC";
		case UpgradeConfigIf_ERR_OVER_RANGE:
			return "OVER_RANGE";
		case UpgradeConfigIf_ERR_ERASE:
			return "ERASE";
		case UpgradeConfigIf_ERR_WRITE:
			return "WRITE";
		case UpgradeConfigIf_ERR_SLOT:
			return "SLOT";
		case UpgradeConfigIf_SKIP:
			return "SKIP";
		default:
			return "UNKNOWN";
	}
}

/**
 * @brief 恢复config 初始状态。
 * @param cfg：UpgradeConfig_t 结构体指针。
 * @return
 *  	None
 * @note
 *
 */
void UpgradeConfig_SetDefault(UpgradeConfig_t *cfg)
{
	FirmwareHeader_t default_header = {0};

	cfg->magic = UPGRADE_CONFIG_MAGIC;
	cfg->version = UPGRADE_CONFIG_VERSION;
	cfg->sequence = 0;
	cfg->state = UPGRADE_STATE_IDLE;
	cfg->confirmed_slot = BOOT_SLOT_INVALID;
	cfg->pending_slot = BOOT_SLOT_INVALID;
	cfg->boot_slot = BOOT_SLOT_INVALID;
	cfg->rollback_slot  = BOOT_SLOT_INVALID;
	cfg->slot_a_version = 0;
	cfg->slot_b_version = 0;
	cfg->boot_attempts = 0;
	cfg->pending_header = default_header;

	cfg->config_crc32 = UpgradeConfig_CalcCrc(cfg);
}

/**
 * @brief UpgradeConfig_t CRC 计算。
 * @param cfg：UpgradeConfig_t 结构体指针。
 * @return
 *  	config_crc32 = 0 情况下
 * 		结构体的CRC
 * @note
 *
 */
uint32_t UpgradeConfig_CalcCrc(const UpgradeConfig_t *cfg)
{
	UpgradeConfig_t cfg_temp = *cfg;

	cfg_temp.config_crc32 = 0;

	return Crc32_Calculate(&cfg_temp, sizeof(UpgradeConfig_t));
}

/**
 * @brief Config 有效判断。
 * @param cfg：UpgradeConfig_t 结构体指针。
 * @return
 *  	UpgradeConfigIfErrorCode_t
 * 			- UpgradeConfigIf_OK：OK。
 * 			- UpgradeConfigIf_ERR_PARAM：参数无效。
 * 			- UpgradeConfigIf_ERR_MAGIC：config 识别码错误。
 * 			- UpgradeConfigIf_ERR_VERSION：config 版本错误。
 * 			- UpgradeConfigIf_ERR_CRC：config crc校验错误。
 * @note
 * 		cfg != NULL
 *  	magic == UPGRADE_CONFIG_MAGIC
 *		version == UPGRADE_CONFIG_VERSION
 *		state 在合法枚举范围内
 *		config_crc32 == UpgradeConfig_CalcCrc(cfg)
 */
UpgradeConfigIfErrorCode_t UpgradeConfig_IsValid(const UpgradeConfig_t *cfg)
{
	if(cfg==0)
	{
		return UpgradeConfigIf_ERR_PARAM;
	}

	if(cfg->magic != UPGRADE_CONFIG_MAGIC)
	{
		return UpgradeConfigIf_ERR_MAGIC;
	}

	if(cfg->version != UPGRADE_CONFIG_VERSION)
	{
		return UpgradeConfigIf_ERR_VERSION;
	}

	if(UpgradeConfig_IsStateValid(cfg->state) == 0U)
	{
		return UpgradeConfigIf_ERR_STATE;
	}

	if(cfg->config_crc32 != UpgradeConfig_CalcCrc(cfg))
	{
		return UpgradeConfigIf_ERR_CRC;
	}

	return UpgradeConfigIf_OK;
}

/**
 * @brief 读取 UpgradeConfig_t。
 * @param cfg：UpgradeConfig_t 结构体指针。
 * @return
 *  	UpgradeConfigIfErrorCode_t
 * 			- UpgradeConfigIf_OK：读取有效。
 * 			- UpgradeConfigIf_ERR_PARAM：参数无效。
 * 			- UpgradeConfigIf_ERR_MAGIC：config 识别码错误。
 * 			- UpgradeConfigIf_ERR_VERSION：config 版本错误。
 * 			- UpgradeConfigIf_ERR_CRC：config crc校验错误。
 * @note
 * 		从 Flash Config 区读取配置到 RAM。
 * 		读取的Config 无效，则生成默认 IDLE 状态。
 */
UpgradeConfigIfErrorCode_t UpgradeConfig_Load(UpgradeConfig_t *cfg)
{
	if(cfg == 0)
	{
		return UpgradeConfigIf_ERR_PARAM;
	}

	UpgradeConfigIfErrorCode_t config_verify_result;

	const UpgradeConfig_t *cfg_tempt = (const UpgradeConfig_t *)UPGRADE_CONFIG_BASE_ADDR;

	config_verify_result = UpgradeConfig_IsValid(cfg_tempt);
	if(config_verify_result == UpgradeConfigIf_OK)
	{
		*cfg = *cfg_tempt;
	}
	else
	{
		UpgradeConfig_SetDefault(cfg);
	}

	return config_verify_result;
}

/**
 * @brief UpgradeConfig 写入 Flash。
 * @param addr：目标写入地址。
 * @param data：字节数据指针变量。
 * @param size：写入字节数。
 * @return
 *  	FlashIfErrorCode_t
 *			- UpgradeConfigIf_OK：完成。
 *			- UpgradeConfigIf_NULL：参数无效。
 * 			- UpgradeConfigIf_WRITE：Flash 写入错误。
 * @note
 *
 */
static UpgradeConfigIfErrorCode_t UpgradeConfigIf_Write(uint32_t addr, const uint8_t *data, uint32_t size)
{
    uint32_t offset;
    uint32_t write_size;
    uint16_t halfword;
    FLASH_Status status;

    if(data == 0)
    {
        return 	UpgradeConfigIf_ERR_PARAM;
    }

    write_size = (size + 1U) & ~1U;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    for(offset = 0U; offset < size; offset += 2U)
    {
        halfword = data[offset];

        if((offset + 1U) < size)
        {
            halfword |= ((uint16_t)data[offset + 1U] << 8U);
        }
        else
        {
            halfword |= 0xFF00U;
        }

        status = FLASH_ProgramHalfWord(addr + offset, halfword);
        if(status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return UpgradeConfigIf_ERR_WRITE;
        }
    }

    FLASH_Lock();

    return UpgradeConfigIf_OK;
}

/**
 * @brief UpgradeConfig 保存。
 * @param cfg：config 指针变量。
 * @return
 *  	FlashIfErrorCode_t
 *			- UpgradeConfigIf_OK：完成。
 *			- UpgradeConfigIf_NULL：参数无效。
 * 			- UpgradeConfigIf_ERR_OVER_RANGE：写入地址越界。
 * 			- UpgradeConfigIf_ERR_ERASE：擦除失败。
 * 			- UpgradeConfigIf_ERR_WRITE：写入失败。
 * @note
 * 		重新计算 CRC、擦写 Config 页，并读回校验，避免写入失败后 Bootloader 误用损坏状态。
 *
 */
UpgradeConfigIfErrorCode_t UpgradeConfig_Save(const UpgradeConfig_t *cfg)
{
	if(cfg == 0)
	{
		return UpgradeConfigIf_ERR_PARAM;
	}

	UpgradeConfig_t cfg_temp = *cfg;
	uint32_t config_addr = UPGRADE_CONFIG_BASE_ADDR;
	UpgradeConfigIfErrorCode_t upgradeconfig_save_status;

	cfg_temp.config_crc32 = UpgradeConfig_CalcCrc(cfg);

	if(!(MemoryMap_IsConfigRange(config_addr, sizeof(UpgradeConfig_t))))
	{
		return UpgradeConfigIf_ERR_OVER_RANGE;
	}

	FLASH_Unlock();

	if(FLASH_ErasePage(config_addr) != FLASH_COMPLETE)
	{
		FLASH_Lock();
		return UpgradeConfigIf_ERR_ERASE;
	}

	FLASH_Lock();

	upgradeconfig_save_status = UpgradeConfigIf_Write(config_addr, (const uint8_t *)&cfg_temp, sizeof(UpgradeConfig_t));

	if(upgradeconfig_save_status != UpgradeConfigIf_OK)
	{
		return UpgradeConfigIf_ERR_WRITE;
	}

	upgradeconfig_save_status = UpgradeConfig_IsValid((const UpgradeConfig_t *)config_addr);
	if(upgradeconfig_save_status != UpgradeConfigIf_OK)
	{
		return upgradeconfig_save_status;
	}

	return UpgradeConfigIf_OK;
}

/**
 * @brief 保存指定 slot 区域的待升级状态。
 * @param pending_header：pending 固件 Header 指针变量。
 * @param pending_slot：待保存状态的 slot 区。
 * @return
 *  	FlashIfErrorCode_t
 *			- UpgradeConfigIf_OK：完成。
 *			- UpgradeConfigIf_ERR_PARAM：参数无效。
 *			- UpgradeConfigIf_ERR_SLOT：pending_slot 错误。
 * 			- UpgradeConfigIf_ERR_OVER_RANGE：写入地址越界。
 * 			- UpgradeConfigIf_ERR_ERASE：擦除失败。
 * 			- UpgradeConfigIf_ERR_WRITE：写入失败。
 * @note
 * 		参数有效判断。保存新slot到upgrade_config的同时对应更新支持回滚的slot区。
 * 		Config 无效时按默认 IDLE 第一次安装 Slot A。
 *
 */
UpgradeConfigIfErrorCode_t UpgradeConfig_SavePending(const FirmwareHeader_t *pending_header, BootSlot_t pending_slot)
{
	if(pending_header == 0)
	{
		return UpgradeConfigIf_ERR_PARAM;
	}

	if(MemoryMap_IsSlotValid(pending_slot) == 0U)
	{
		return UpgradeConfigIf_ERR_PARAM;
	}

	UpgradeConfig_t cfg;
	UpgradeConfigIfErrorCode_t upgradeconfig_status;

	UpgradeConfig_Load(&cfg);

	if(pending_slot == cfg.confirmed_slot)
	{
		return UpgradeConfigIf_ERR_SLOT;
	}

	cfg.pending_slot = pending_slot;
	cfg.pending_header = *pending_header;
	cfg.state = UPGRADE_STATE_PENDING_VERIFY;
	cfg.boot_slot = pending_slot;
	cfg.rollback_slot = cfg.confirmed_slot;
	cfg.boot_attempts = 0;
	cfg.sequence += 1;
	switch(pending_slot)
	{
		case BOOT_SLOT_A:
		{
			cfg.slot_a_version = pending_header->image_version;
			break;
		}
		case BOOT_SLOT_B:
		{
			cfg.slot_b_version = pending_header->image_version;
			break;
		}
		default:
			break;
	}

	upgradeconfig_status = UpgradeConfig_Save(&cfg);
	if(upgradeconfig_status != UpgradeConfigIf_OK)
	{
		return upgradeconfig_status;
	}

	return UpgradeConfigIf_OK;
}

/**
 * @brief 保存升级状态。
 * @param state 需要写入 Config 区的升级状态。
 * @return
 *      UpgradeConfigIf_OK：状态保存成功。
 *      UpgradeConfigIf_ERR_STATE：state 超出 UpgradeState_t 合法范围。
 *      其他错误：读取或写入 Config 失败，错误码来自 UpgradeConfig_Load/Save。
 * @note
 *      该函数用于 Bootloader 侧推进升级状态。
 *      A/B 主线当前用于 PENDING_VERIFY -> TESTING。
 *      写入新状态前会先从 Flash Config 区读取当前配置，保留 pending_header、
 *      pending_slot、rollback_slot 等上下文，再更新 sequence 和 state。
 */
UpgradeConfigIfErrorCode_t UpgradeConfig_SaveState(UpgradeState_t state)
{
    UpgradeConfig_t cfg;
    UpgradeConfigIfErrorCode_t ret;

    if (UpgradeConfig_IsStateValid(state) == 0U)
    {
        return UpgradeConfigIf_ERR_STATE;
    }

    ret = UpgradeConfig_Load(&cfg);
    if (ret != UpgradeConfigIf_OK)
    {
        return ret;
    }

    cfg.sequence += 1;
    cfg.state = state;

    if (state == UPGRADE_STATE_TESTING)
    {
        cfg.boot_attempts = 1;
    }

    return UpgradeConfig_Save(&cfg);
}

/**
 * @brief 保存升级确认状态。
 * @return
 *      UpgradeConfigIf_OK：确认状态保存成功。
 * 		UpgradeConfigIf_ERR_STATE： state 异常。
 *      其他错误：读取或写入 Config 失败，错误码来自 UpgradeConfig_Load/Save。
 * @note
 *      用于 Application 正常启动后的确认动作。
 * 		当状态已经为UPGRADE_STATE_CONFIRMED 返回UpgradeConfigIf_SKIP，表示已经确认过，跳过此次保存。
 *      只有当状态为UPGRADE_STATE_TESTING条件下，继续执行UPGRADE_STATE_CONFIRMED状态的保存。
 * 		同时对应更新config中的slot相关设置，清除pending_slot和rollback_slot。
 * 		其他状态则返回state异常错误码。
 */
UpgradeConfigIfErrorCode_t UpgradeConfig_SaveConfirmed(void)
{
    UpgradeConfig_t cfg;
    UpgradeConfigIfErrorCode_t ret;

    ret = UpgradeConfig_Load(&cfg);
    if (ret != UpgradeConfigIf_OK)
    {
        return ret;
    }

	if(cfg.state == UPGRADE_STATE_CONFIRMED)
	{
		return UpgradeConfigIf_SKIP;
	}
	else if(cfg.state == UPGRADE_STATE_TESTING)
	{
		if(cfg.boot_slot != cfg.pending_slot)
		{
			return UpgradeConfigIf_ERR_SLOT;
		}
		else
		{
			cfg.sequence += 1;
			cfg.state = UPGRADE_STATE_CONFIRMED;
			cfg.boot_attempts = 0;
			cfg.confirmed_slot = cfg.boot_slot;
			cfg.pending_slot = BOOT_SLOT_INVALID;
			cfg.rollback_slot = BOOT_SLOT_INVALID;
			return UpgradeConfig_Save(&cfg);
		}
	}
	else
	{
		return UpgradeConfigIf_ERR_STATE;
	}
}

/**
 * @brief 回滚为Confirmed状态及相关处理。
 * @return
 *      UpgradeConfigIf_OK：状态保存及处理成功。
 * 		UpgradeConfigIf_ERR_SLOT：无效 rollback slot。
 * 		UpgradeConfigIf_ERR_STATE： state 异常。
 *      其他错误：读取或写入 Config 失败，错误码来自 UpgradeConfig_Load/Save。
 * @note
 * 		加载flash中的upgrade config，当state为TESTING状态下，才进行回滚保存以及对应的其他参数的调整。
 */
UpgradeConfigIfErrorCode_t UpgradeConfig_RollbackToConfirmed(void)
{
	UpgradeConfig_t cfg;
    UpgradeConfigIfErrorCode_t ret;

    ret = UpgradeConfig_Load(&cfg);
    if (ret != UpgradeConfigIf_OK)
    {
        return ret;
    }

	if(cfg.state == UPGRADE_STATE_TESTING)
	{
		if(MemoryMap_IsSlotValid(cfg.rollback_slot))
		{
			cfg.sequence += 1;
			cfg.boot_slot = cfg.rollback_slot;
			cfg.confirmed_slot = cfg.rollback_slot;
			cfg.pending_slot = BOOT_SLOT_INVALID;
			cfg.rollback_slot = BOOT_SLOT_INVALID;
			cfg.boot_attempts = 0;
			cfg.state = UPGRADE_STATE_CONFIRMED;
			return UpgradeConfig_Save(&cfg);
		}
		else
		{
			return UpgradeConfigIf_ERR_SLOT;
		}
	}
	else
	{
		return UpgradeConfigIf_ERR_STATE;
	}
}
