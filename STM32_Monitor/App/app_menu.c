#include "app_menu.h"

/*
 * 文件职责：
 * 1. 实现 OLED 菜单状态机。
 * 2. 根据 DisplayTask 传入的编码器增量切换自动扫描、上传状态和任务监控页面。
 * 3. 维护页面累计步进，避免编码器单次抖动导致页面频繁跳转。
 *
 * 当前状态：
 * - 支持自动扫描、上传状态和任务监控三个页面循环切换。
 */

#define APP_MENU_ENCODER_COUNTS_PER_STEP 4

static DisplayPage_t s_current_page;
static int16_t s_encoder_accumulator = 0;

/*
 * @brief   初始化菜单状态机。
 * @param   init_page: 初始页面。
 * @retval  AppMenu_Status_t:
 *          - APP_MENU_OK: 初始化成功。
 *          - APP_MENU_ERROR_PARAM: 初始页面非法。
 */
AppMenu_Status_t App_Menu_Init(DisplayPage_t init_page)
{
    if(init_page >= DISPLAY_PAGE_COUNT)
    {
        return APP_MENU_ERROR_PARAM;
    }

    s_current_page = init_page;
    s_encoder_accumulator = 0;

    return APP_MENU_OK;
}

/*
 * @brief   获取当前页面。
 * @retval  DisplayPage_t: 当前页面枚举值。
 */
DisplayPage_t App_Menu_GetCurrentPage(void)
{
    return s_current_page;
}

/*
 * @brief   根据编码器增量切换页面。
 * @param   encoder_delta: 本周期编码器增量。
 * @retval  None
 * @note    每累计 APP_MENU_ENCODER_COUNTS_PER_STEP 个计数切换一页，左右旋转均循环切页。
 */
void App_Menu_UpdateByEncoderDelta(int16_t encoder_delta)
{
    s_encoder_accumulator += encoder_delta;
    while(s_encoder_accumulator >= APP_MENU_ENCODER_COUNTS_PER_STEP)
    {
        s_encoder_accumulator -= APP_MENU_ENCODER_COUNTS_PER_STEP;

        if(s_current_page >= DISPLAY_PAGE_COUNT - 1)
        {
            s_current_page = DISPLAY_PAGE_AUTO_SCAN;
        }
        else
        {
            s_current_page++;
        }
    }

    while(s_encoder_accumulator <= -APP_MENU_ENCODER_COUNTS_PER_STEP)
    {
        s_encoder_accumulator += APP_MENU_ENCODER_COUNTS_PER_STEP;

        if(s_current_page == DISPLAY_PAGE_AUTO_SCAN)
        {
            s_current_page = DISPLAY_PAGE_COUNT - 1;
        }
        else
        {
            s_current_page--;
        }
    }
}

/*
 * @brief   重置菜单到自动扫描页面。
 * @retval  None
 * @note    清空累计增量，避免历史旋转量影响下一次页面切换。
 */
void App_Menu_ResetToAutoPage(void)
{
    s_current_page = DISPLAY_PAGE_AUTO_SCAN;
    s_encoder_accumulator = 0;
}
