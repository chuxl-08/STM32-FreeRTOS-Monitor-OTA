#ifndef __BSP_IWDG_H
#define __BSP_IWDG_H

#include "stm32f10x.h"
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明独立看门狗 IWDG 初始化和喂狗接口。
 * 2. 由 MonitorTask 根据关键任务心跳决定是否喂狗。
 * 3. 避免在任意位置无条件喂狗，保证看门狗能发现任务卡死。
 */

/*
 * @brief   RCC 复位原因枚举。
 * @note    枚举值不直接等同于 RCC_FLAG_xxx 宏值，BSP 层负责完成映射。
 *          BSP_IWDG_RST_UNKNOWN 用于没有匹配到明确复位标志的场景。
 */
typedef enum
{
    BSP_IWDG_PINRST = 0,
    BSP_IWDG_PORRST,
    BSP_IWDG_SFTRST,
    BSP_IWDG_IWDGRST,
    BSP_IWDG_WWDGRST,
    BSP_IWDG_LPWRRST,
    BSP_IWDG_COUNT,
    BSP_IWDG_RST_UNKNOWN
} BSP_IWDG_RST_REASON;

const char *ResetReasonName(BSP_IWDG_RST_REASON reason);
void BSP_IWDG_Init(void);
void BSP_IWDG_Feed(void);
BSP_IWDG_RST_REASON BSP_IWDG_ResetReason(void);
void BSP_IWDG_ClearResetFlag(void);


#endif /* __BSP_IWDG_H */
