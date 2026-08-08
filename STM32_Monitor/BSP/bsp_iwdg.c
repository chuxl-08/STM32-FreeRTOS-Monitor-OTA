#include "bsp_iwdg.h"

/*
 * 文件职责：
 * 1. 实现 IWDG 底层初始化和喂狗动作。
 * 2. 配合应用层任务心跳实现系统异常恢复。
 * 3. 读取并清除 RCC 复位原因，便于启动日志判断是否由看门狗复位。
 *
 * 当前状态：
 * - 已实现 IWDG 初始化、喂狗、复位原因读取和复位标志清除。
 */
#define BSP_IWDG_RELOAD             3124


/*
 * @brief   复位原因转字符串。
 * @param   reason: 复位原因。
 * @retval  const char *复位原因字符串。
 */
const char *ResetReasonName(BSP_IWDG_RST_REASON reason)
{
    switch (reason)
    {
        case BSP_IWDG_PINRST:
            return "PINRST";
        case BSP_IWDG_PORRST:
            return "PORRST";
        case BSP_IWDG_SFTRST:
            return "SFTRST";
        case BSP_IWDG_IWDGRST:
            return "IWDGRST";
        case BSP_IWDG_WWDGRST:
            return "WWDGRST";
        case BSP_IWDG_LPWRRST:
            return "LPWRRST";
        case BSP_IWDG_RST_UNKNOWN:
            return "UNKNOWN";
        default:
            return "UnknownReason";
    }
}

/*
 * @brief   初始化独立看门狗。
 * @note    当前使用 128 分频，reload=3124，按 LSI 约 40kHz 估算超时时间约 10s。
 */
void BSP_IWDG_Init(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);

    IWDG_SetPrescaler(IWDG_Prescaler_128);      // 单位：3.2ms

    IWDG_SetReload(BSP_IWDG_RELOAD);

    /*
     * 启动前先重装载一次，保证 IWDG 从配置的 reload 值开始计数，
     * 避免沿用未知计数状态导致启动阶段误复位。
     */
    IWDG_ReloadCounter();
    IWDG_Enable();
}

/*
 * @brief   喂独立看门狗。
 * @note    本函数只执行底层 reload，系统健康判断由 MonitorTask 统一完成。
 */
void BSP_IWDG_Feed(void)
{
    IWDG_ReloadCounter();
}

/*
 * @brief   读取并清除上次复位原因。
 * @retval  BSP_IWDG_RST_REASON: 映射后的复位原因。
 * @note    多个 RCC 复位标志可能同时存在，因此优先保留看门狗相关原因。
 */
BSP_IWDG_RST_REASON BSP_IWDG_ResetReason(void)
{
    BSP_IWDG_RST_REASON reset_reason = BSP_IWDG_RST_UNKNOWN;

    if(RCC_GetFlagStatus(RCC_FLAG_IWDGRST) == SET)
    {
        reset_reason = BSP_IWDG_IWDGRST;
    }
    else if(RCC_GetFlagStatus(RCC_FLAG_WWDGRST) == SET)
    {
        reset_reason = BSP_IWDG_WWDGRST;
    }
    else if(RCC_GetFlagStatus(RCC_FLAG_SFTRST) == SET)
    {
        reset_reason = BSP_IWDG_SFTRST;
    }
    else if(RCC_GetFlagStatus(RCC_FLAG_LPWRRST) == SET)
    {
        reset_reason = BSP_IWDG_LPWRRST;
    }
    else if(RCC_GetFlagStatus(RCC_FLAG_PORRST) == SET)
    {
        reset_reason = BSP_IWDG_PORRST;
    }
    else if(RCC_GetFlagStatus(RCC_FLAG_PINRST) == SET)
    {
        reset_reason = BSP_IWDG_PINRST;
    }

    BSP_IWDG_ClearResetFlag();
    return reset_reason;
}

/*
 * @brief   清除 RCC 复位标志。
 */
void BSP_IWDG_ClearResetFlag(void)
{
    RCC_ClearFlag();
}
