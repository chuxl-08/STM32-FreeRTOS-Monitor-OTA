#include "app_ota_confirm.h"
#include "app_log.h"
#include "upgrade_config_if.h"

#define APP_OTA_CONFIRM_STARTUP_DELAY_MS        1000U

static uint8_t s_ota_confirm_finished = 0U;

/*
 * @brief   OTA 确认模块使用的 Slot 日志字符串。
 * @param   slot: Bootloader Config 中记录的 Slot。
 * @retval  const char *: Slot 名称字符串。
 */
static const char *AppOtaConfirm_SlotName(BootSlot_t slot)
{
    switch(slot)
    {
        case BOOT_SLOT_A:
            return "A";
        case BOOT_SLOT_B:
            return "B";
        default:
            return "INVALID";
    }
}

/*
 * @brief   判断 MonitorTask 本轮采集到的任务心跳是否健康。
 * @param   items: MonitorTask 维护的任务监控数组。
 * @param   item_count: 任务监控数组元素数量。
 * @retval  1: 所有任务句柄有效且心跳 ALIVE。
 *          0: 参数错误、任务句柄缺失或存在 STALE 任务。
 * @note    当前 OTA 确认只证明 FreeRTOS 调度和核心任务没有卡死；
 *          不把传感器连线、Wi-Fi 在线等业务外设状态作为确认门槛。
 */
static uint8_t AppOtaConfirm_IsMonitorHealthy(const AppTask_MonitorItem_t *items,
                                              uint32_t item_count)
{
    uint32_t i;

    if((items == 0) || (item_count != APP_TASK_ID_COUNT))
    {
        return 0;
    }

    for(i = 0U; i < item_count; i++)
    {
        if(App_Task_IsTaskEnabled((AppTask_Id_t)i) == 0U)
        {
            continue;
        }

        if((items[i].task_handle == NULL) ||
           (items[i].health != APP_TASK_HEALTH_ALIVE))
        {
            return 0;
        }
    }

    return 1;
}

/*
 * @brief   在 MonitorTask 健康检查通过后尝试确认当前测试固件。
 * @param   items: MonitorTask 本轮任务健康检查结果。
 * @param   item_count: 任务监控数组元素数量。
 * @retval  无。
 * @note    只有 Config 处于 TESTING 状态时才调用 UpgradeConfig_SaveConfirmed()。
 *          这样确认点发生在 FreeRTOS 任务运行之后，而不是刚进入 main() 时。
 *          通过一次性保护，避免周期任务反复擦写 Config Flash。
 */
void AppOtaConfirm_TryConfirmFromMonitor(const AppTask_MonitorItem_t *items,
                                         uint32_t item_count)
{
    UpgradeConfig_t cfg;
    UpgradeConfigIfErrorCode_t cfg_result;
    UpgradeConfigIfErrorCode_t confirm_result;

    if(s_ota_confirm_finished)
    {
        return;
    }

    if(xTaskGetTickCount() < pdMS_TO_TICKS(APP_OTA_CONFIRM_STARTUP_DELAY_MS))
    {
        return;
    }

    if(AppOtaConfirm_IsMonitorHealthy(items, item_count) == 0U)
    {
        return;
    }

    cfg_result = UpgradeConfig_Load(&cfg);
    if(cfg_result != UpgradeConfigIf_OK)
    {
        App_LogPrintf("[OTA] confirm skip cfg=%s\r\n",
                      UpgradeConfigIfErrorCode_String(cfg_result));
        s_ota_confirm_finished = 1U;
        return;
    }

    if(cfg.state != UPGRADE_STATE_TESTING)
    {
        App_LogPrintf("[OTA] confirm skip state=%s\r\n",
                      UpgradState_String(cfg.state));
        s_ota_confirm_finished = 1U;
        return;
    }

    confirm_result = UpgradeConfig_SaveConfirmed();
    if(confirm_result == UpgradeConfigIf_OK)
    {
        App_LogPrintf("[OTA] confirm OK slot=%s\r\n",
                      AppOtaConfirm_SlotName(cfg.boot_slot));
    }
    else if(confirm_result == UpgradeConfigIf_SKIP)
    {
        App_LogPrintf("[OTA] confirm skip already confirmed\r\n");
    }
    else
    {
        App_LogPrintf("[OTA] confirm fail %s\r\n",
                      UpgradeConfigIfErrorCode_String(confirm_result));
    }

    s_ota_confirm_finished = 1U;
}
