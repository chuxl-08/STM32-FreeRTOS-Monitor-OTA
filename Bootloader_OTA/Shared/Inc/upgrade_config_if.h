#ifndef UPGRADE_CONFIG_IF_H
#define UPGRADE_CONFIG_IF_H
#include "stm32f10x.h"
#include "upgrade_config.h"

// UpgradeConfigIf.* 错误码。
typedef enum
{
	UpgradeConfigIf_OK=0,
	UpgradeConfigIf_ERR_PARAM,
	UpgradeConfigIf_ERR_MAGIC,
	UpgradeConfigIf_ERR_VERSION,
	UpgradeConfigIf_ERR_STATE,
	UpgradeConfigIf_ERR_CRC,
    UpgradeConfigIf_ERR_OVER_RANGE,
    UpgradeConfigIf_ERR_ERASE,
    UpgradeConfigIf_ERR_WRITE,
	UpgradeConfigIf_ERR_SLOT,
	UpgradeConfigIf_SKIP
} UpgradeConfigIfErrorCode_t;

const char *UpgradeConfigIfErrorCode_String(UpgradeConfigIfErrorCode_t err);
void UpgradeConfig_SetDefault(UpgradeConfig_t *cfg);
uint32_t UpgradeConfig_CalcCrc(const UpgradeConfig_t *cfg);
UpgradeConfigIfErrorCode_t UpgradeConfig_IsValid(const UpgradeConfig_t *cfg);
UpgradeConfigIfErrorCode_t UpgradeConfig_Load(UpgradeConfig_t *cfg);
UpgradeConfigIfErrorCode_t UpgradeConfig_Save(const UpgradeConfig_t *cfg);
UpgradeConfigIfErrorCode_t UpgradeConfig_SavePending(const FirmwareHeader_t *pending_header, BootSlot_t pending_slot);
UpgradeConfigIfErrorCode_t UpgradeConfig_SaveState(UpgradeState_t state);
UpgradeConfigIfErrorCode_t UpgradeConfig_SaveConfirmed(void);
UpgradeConfigIfErrorCode_t UpgradeConfig_RollbackToConfirmed(void);

#endif


