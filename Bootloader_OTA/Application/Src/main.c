#include "stm32f10x.h"                  // Device header
#include "platform.h"
#include "memory_map.h"
#include "upgrade_config_if.h"
#include "app_ota.h"
#include <stdio.h>

int main(void)
{
    UpgradeConfigIfErrorCode_t upgrade_config_result;
    AppOtaStatus_t ota_status;

    SCB->VTOR = SLOT_A_BASE_ADDR;
    __DSB();
    __ISB();

    __enable_irq();

    Platform_InitUsart1();
    Platform_PutString("[APP] minimal Application start\r\n");

    /*
     * 最小验证 App 的确认点：
     * 用于验证 Bootloader TESTING -> CONFIRMED 链路。
     * 真实业务 App 的确认逻辑位于 STM32_Monitor MonitorTask 健康检查。
     */
    upgrade_config_result = UpgradeConfig_SaveConfirmed();
    if(upgrade_config_result == UpgradeConfigIf_OK)
    {
        printf("[CFG] save %s state %s\r\n",
               UpgradState_String(UPGRADE_STATE_CONFIRMED),
               UpgradeConfigIfErrorCode_String(upgrade_config_result));
    }
    else if(upgrade_config_result == UpgradeConfigIf_SKIP)
    {
        printf("[CFG] confirmed already, skip\r\n");
    }
    else
    {
        printf("[CFG] save %s state fail %s\r\n",
               UpgradState_String(UPGRADE_STATE_CONFIRMED),
               UpgradeConfigIfErrorCode_String(upgrade_config_result));
    }

    /*
     * HTTP OTA 参考流程：
     * - 默认公开配置为占位值时，AppOta_RunUpgradeTriggerTest() 会直接返回 PARAM。
     * - 填入真实 Wi-Fi/HTTP 配置后，该流程会下载 pkg、校验、保存 pending 并复位。
     */
    ota_status = AppOta_RunUpgradeTriggerTest();
    printf("[OTA] upgrade trigger %s\r\n", AppOta_StatusString(ota_status));

    while (1)
    {

    }
}
