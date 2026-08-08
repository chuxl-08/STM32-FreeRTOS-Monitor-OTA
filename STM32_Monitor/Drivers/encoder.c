#include "encoder.h"
#include "bsp_timer.h"

/*
 * 文件职责：
 * 1. 实现旋转编码器驱动。
 * 2. 使用 PC6/PC7 的 TIM3 编码器模式读取旋转方向和增量。
 * 3. 对上层屏蔽 TIM3 计数器细节，提供累计计数和增量计数接口。
 * 4. 使用 PC4 / EXTI4 处理 SW 按键事件。
 *
 * 当前状态：
 * - 支持初始化、累计计数读取、增量读取、计数清零和 SW 点击事件。
 */

#define ENCODER_DIRECTION_REVERSE        0
#define ENCODER_BUTTON_PORT              GPIOC
#define ENCODER_BUTTON_PIN               GPIO_Pin_4
#define ENCODER_BUTTON_EXTI_LINE         EXTI_Line4

static uint8_t s_encoder_is_init = 0;
static int16_t s_encoder_last_raw_count = 0;
static volatile uint8_t s_encoder_button_irq_event = 0;
static uint8_t s_encoder_button_click_event = 0;
static uint8_t s_encoder_button_pressed = 0;

/*
 * @brief   将 BSP_TIMER_Status_t 映射为 Encoder_Status_t。
 * @param   status: BSP Timer 状态码。
 * @retval  Encoder_Status_t
 */
static Encoder_Status_t Encoder_MapBSPStatus(BSP_TIMER_Status_t status)
{
    switch(status)
    {
        case BSP_TIMER_OK:
            return ENCODER_OK;
        case BSP_TIMER_ERROR_PARAM:
            return ENCODER_ERROR_PARAM;
        case BSP_TIMER_ERROR_NOT_INIT:
            return ENCODER_ERROR_NOT_INIT;
        default:
            return ENCODER_ERROR_TIMER;
    }
}

/*
 * @brief   根据项目期望方向修正计数方向。
 * @param   value: 原始计数或原始增量。
 * @retval  int16_t: 修正方向后的计数。
 * @note    如果上板后发现顺时针方向和预期相反，
 *          可将 ENCODER_DIRECTION_REVERSE 改为 1。
 */
static int16_t Encoder_ApplyDirection(int16_t value)
{
#if ENCODER_DIRECTION_REVERSE
    return -value;
#else
    return value;
#endif
}

/*
 * @brief   初始化 PC4 / SW 按键 EXTI4。
 * @param   None
 * @retval  Encoder_Status_t:
 *          - ENCODER_OK: 初始化成功。
 * @note    SW 按下为低电平，因此 PC4 配置为上拉输入，EXTI4 使用下降沿触发。
 *          EXTI4 中断只置位事件标志，按键确认和事件消费在 Driver 接口中完成。
 */
static Encoder_Status_t Encoder_ButtonInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = ENCODER_BUTTON_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(ENCODER_BUTTON_PORT, &GPIO_InitStructure);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource4);

    EXTI_ClearITPendingBit(ENCODER_BUTTON_EXTI_LINE);
    EXTI_InitStructure.EXTI_Line = ENCODER_BUTTON_EXTI_LINE;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    NVIC_InitStructure.NVIC_IRQChannel = EXTI4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 7;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    s_encoder_button_irq_event = 0;
    s_encoder_button_click_event = 0;
    s_encoder_button_pressed = 0;

    return ENCODER_OK;
}

/*
 * @brief   初始化旋转编码器以及按键。
 * @param   None
 * @retval  Encoder_Status_t:
 *          - ENCODER_OK: 初始化成功。
 *          - ENCODER_ERROR_TIMER: 底层 TIM3 编码器模式初始化失败。
 *          - ENCODER_ERROR_BUTTON: PC4 / SW 按键初始化失败。
 * @note    初始化后会清零 TIM3 计数，并清零 Driver 层上一次计数记录。
 *          同时初始化 PC4 / EXTI4 作为编码器 SW 按键输入。
 */
Encoder_Status_t Encoder_Init(void)
{
    if(s_encoder_is_init)
    {
        return ENCODER_OK;
    }

    BSP_TIMER_Status_t bsp_status;
    Encoder_Status_t button_status;

    s_encoder_last_raw_count = 0;
    s_encoder_button_irq_event = 0;
    s_encoder_button_click_event = 0;
    s_encoder_button_pressed = 0;

    bsp_status = BSP_TIM3_Encoder_Init();
    if(bsp_status != BSP_TIMER_OK)
    {
        return Encoder_MapBSPStatus(bsp_status);
    }

    bsp_status = BSP_TIM3_Encoder_ClearCount();
    if(bsp_status != BSP_TIMER_OK)
    {
        return Encoder_MapBSPStatus(bsp_status);
    }

    button_status = Encoder_ButtonInit();
    if(button_status != ENCODER_OK)
    {
        return ENCODER_ERROR_BUTTON;
    }

    s_encoder_is_init = 1;

    return ENCODER_OK;
}

/*
 * @brief   获取旋转编码器累计计数。
 * @param   count: 输出参数，用于保存当前累计计数。
 * @retval  Encoder_Status_t:
 *          - ENCODER_OK: 读取成功。
 *          - ENCODER_ERROR_PARAM: count 为空指针。
 *          - ENCODER_ERROR_NOT_INIT: 编码器尚未初始化。
 *          - ENCODER_ERROR_TIMER: 底层 TIM3 读取失败。
 */
Encoder_Status_t Encoder_GetCount(int16_t *count)
{
    BSP_TIMER_Status_t bsp_status;
    int16_t raw_count;

    if(count == 0)
    {
        return ENCODER_ERROR_PARAM;
    }

    if(s_encoder_is_init == 0)
    {
        return ENCODER_ERROR_NOT_INIT;
    }

    bsp_status = BSP_TIM3_Encoder_GetCount(&raw_count);
    if(bsp_status != BSP_TIMER_OK)
    {
        return Encoder_MapBSPStatus(bsp_status);
    }

    *count = Encoder_ApplyDirection(raw_count);

    return ENCODER_OK;
}

/*
 * @brief   更新编码器 SW 按键状态。
 * @param   None
 * @retval  Encoder_Status_t:
 *          - ENCODER_OK: 更新成功。
 *          - ENCODER_ERROR_NOT_INIT: 编码器尚未初始化。
 * @note    EXTI4 只负责记录发生过下降沿，
 *          clicked 表示曾经捕获到一次下降沿，pressed 表示当前电平仍为按下。
 */
Encoder_Status_t Encoder_ButtonUpdate(void)
{
    if(s_encoder_is_init == 0)
    {
        return ENCODER_ERROR_NOT_INIT;
    }

    if(GPIO_ReadInputDataBit(ENCODER_BUTTON_PORT, ENCODER_BUTTON_PIN) == Bit_RESET)
    {
        s_encoder_button_pressed = 1;
    }
    else
    {
        s_encoder_button_pressed = 0;
    }

    if(s_encoder_button_irq_event)
    {
        s_encoder_button_irq_event = 0;
        s_encoder_button_click_event = 1;
    }

    return ENCODER_OK;
}

/*
 * @brief   获取编码器 SW 当前是否处于按下状态。
 * @param   None
 * @retval  uint8_t:
 *          - 1: 当前按下。
 *          - 0: 当前未按下，或编码器尚未初始化。
 */
uint8_t Encoder_ButtonIsPressed(void)
{
    if(s_encoder_is_init == 0)
    {
        return 0;
    }

    return s_encoder_button_pressed;
}

/*
 * @brief   获取并消费一次编码器 SW 点击事件。
 * @param   None
 * @retval  uint8_t:
 *          - 1: 从上次读取后发生过一次有效点击。
 *          - 0: 没有新的有效点击。
 */
uint8_t Encoder_ButtonWasClicked(void)
{
    uint8_t clicked;

    clicked = s_encoder_button_click_event;
    s_encoder_button_click_event = 0;

    return clicked;
}

/*
 * @brief   EXTI4 中断服务函数。
 * @param   None
 * @retval  None
 * @note    PC4 / SW 按下为低电平，EXTI4 使用下降沿触发。
 */
void EXTI4_IRQHandler(void)
{
    if(EXTI_GetITStatus(ENCODER_BUTTON_EXTI_LINE) == SET)
    {
        s_encoder_button_irq_event = 1;
        EXTI_ClearITPendingBit(ENCODER_BUTTON_EXTI_LINE);
    }
}

/*
 * @brief   获取旋转编码器相对上一次读取的增量。
 * @param   delta: 输出参数，用于保存本次增量。
 * @retval  Encoder_Status_t:
 *          - ENCODER_OK: 读取成功。
 *          - ENCODER_ERROR_PARAM: delta 为空指针。
 *          - ENCODER_ERROR_NOT_INIT: 编码器尚未初始化。
 *          - ENCODER_ERROR_TIMER: 底层 TIM3 读取失败。
 */
Encoder_Status_t Encoder_GetDelta(int16_t *delta)
{
    BSP_TIMER_Status_t bsp_status;
    int16_t raw_count;
    int16_t raw_delta;

    if(delta == 0)
    {
        return ENCODER_ERROR_PARAM;
    }

    if(s_encoder_is_init == 0)
    {
        return ENCODER_ERROR_NOT_INIT;
    }

    bsp_status = BSP_TIM3_Encoder_GetCount(&raw_count);
    if(bsp_status != BSP_TIMER_OK)
    {
        return Encoder_MapBSPStatus(bsp_status);
    }

    raw_delta = (int16_t)((uint16_t)((uint16_t)raw_count - (uint16_t)s_encoder_last_raw_count));
    s_encoder_last_raw_count = raw_count;
    *delta = Encoder_ApplyDirection(raw_delta);

    return ENCODER_OK;
}

/*
 * @brief   清零旋转编码器累计计数。
 * @param   None
 * @retval  Encoder_Status_t:
 *          - ENCODER_OK: 清零成功。
 *          - ENCODER_ERROR_NOT_INIT: 编码器尚未初始化。
 *          - ENCODER_ERROR_TIMER: 底层 TIM3 清零失败。
 * @note    清零后 Driver 层增量读取基准也会被同步清零。
 */
Encoder_Status_t Encoder_Clear(void)
{
    BSP_TIMER_Status_t bsp_status;

    if(s_encoder_is_init == 0)
    {
        return ENCODER_ERROR_NOT_INIT;
    }

    bsp_status = BSP_TIM3_Encoder_ClearCount();
    if(bsp_status != BSP_TIMER_OK)
    {
        return Encoder_MapBSPStatus(bsp_status);
    }

    s_encoder_last_raw_count = 0;

    return ENCODER_OK;
}
