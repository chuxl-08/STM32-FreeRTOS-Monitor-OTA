#ifndef __APP_MENU_H
#define __APP_MENU_H
#include "error_code.h"
#include "stm32f10x.h"

/*
 * 文件职责：
 * 1. 声明 OLED 页面菜单状态机接口。
 * 2. 处理 DisplayTask 消费后的编码器增量，实现页面循环切换。
 * 3. 定义当前 DisplayTask 支持的页面枚举。
 *
 * 当前状态：
 * - 支持自动扫描页、上传状态页和任务监控页。
 */


typedef enum
{
    /* 自动扫描主页面：显示扫描数据和上传简要状态。 */
    DISPLAY_PAGE_AUTO_SCAN = 0,

    /* 上传状态页面：显示 WiFi、TCP、最近上传结果和计数。 */
    DISPLAY_PAGE_UPLOAD,

    /* 任务监控页面：显示各任务心跳状态和 age。 */
    DISPLAY_PAGE_MONITOR,

    /* 页面数量，不作为实际页面使用。 */
    DISPLAY_PAGE_COUNT
} DisplayPage_t;


AppMenu_Status_t App_Menu_Init(DisplayPage_t init_page);
void App_Menu_UpdateByEncoderDelta(int16_t encoder_delta);
DisplayPage_t App_Menu_GetCurrentPage(void);
void App_Menu_ResetToAutoPage(void);

#endif /* __APP_MENU_H */
