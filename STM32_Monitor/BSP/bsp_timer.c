#include "stm32f10x.h"
#include "bsp_timer.h"
#include "Delay.h"

/*
 * 文件职责：
 * 1. 实现 TIM2 输入捕获的底层配置。
 * 2. 当前固定使用 PA1 / TIM2_CH2 捕获 HC-SR04 Echo 高电平宽度。
 * 3. 实现 TIM3 编码器模式，当前固定使用 PC6/PC7 读取旋转编码器 A/B 相。
 * 4. 将 TIM2 配置为 1 MHz 计数频率，使捕获值单位为 us。
 * 5. 对上层 Driver 屏蔽 GPIO、TIM2/TIM3、NVIC 和输入捕获状态机细节。
 *
 * 当前状态：
 * - TIM2 挂载 APB1，当前系统时钟下 TIM2CLK = 72 MHz。
 * - TIM2 预分频为 72 - 1，计数周期为 1 us。
 * - TIM2_CH2 初始捕获上升沿，上升沿中断后清零 CNT 并切换为下降沿捕获。
 * - 下降沿中断中保存 Echo 高电平时间 echo_us，并置位 CAPTURE_DONE。
 * - 等待捕获完成时带 30000 us 超时保护，避免外设无响应时卡死。
 */

 #define TIM2_IN_PORT       GPIOA
 #define TIM2_IN_PIN        GPIO_Pin_1

 #define TIM2_TIMEOUT       30000

 #define TIM3_ENCODER_PORT        GPIOC
 #define TIM3_ENCODER_A_PIN       GPIO_Pin_6
 #define TIM3_ENCODER_B_PIN       GPIO_Pin_7
 #define TIM3_ENCODER_FILTER      0x6

 static uint8_t tim3_encoder_is_init = 0;

/*
 * @brief   TIM2_CH2 输入捕获状态。
 * @note    WAIT_RISING：等待 Echo 上升沿。
 *          WAIT_FALLING：已捕获上升沿并清零 CNT，等待 Echo 下降沿。
 *          CAPTURE_DONE：已捕获下降沿，echo_us 数据有效。
 */
 typedef enum
 {
    WAIT_RISING = 0,
    WAIT_FALLING,
    CAPTURE_DONE
 } Timer_Status;

 volatile Timer_Status timer_status;
 volatile uint32_t echo_us;


/**
  * @brief  初始化 TIM2 输入捕获及其对应 GPIO。
  * @param  None
  * @retval BSP_TIMER_Status_t:
  *         - BSP_TIMER_OK：TIM2_CH2 输入捕获初始化成功。
  * @note   当前配置用于 PA1 / TIM2_CH2：
  *         - PA1 配置为下拉输入，用于接收 HC-SR04 Echo 分压后的信号。
  *         - TIM2 使用内部时钟，计数频率为 1 MHz。
  *         - CH2 配置为输入捕获模式，初始捕获上升沿。
  *         - 开启 TIM2 CC2 捕获中断，NVIC 抢占优先级为 6。
  */
 BSP_TIMER_Status_t BSP_Timer_Init(void)
 {
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Pin = TIM2_IN_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(TIM2_IN_PORT, &GPIO_InitStructure);

    TIM_InternalClockConfig(TIM2);

    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;TIM_TimeBaseInitStructure.TIM_Period = 0xFFFF;
    TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1; // TIM2CLK = 72MHz    计数频率 1MHz  1us
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

    TIM_ICInitTypeDef TIM_ICInitStructure;
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_2;
    TIM_ICInitStructure.TIM_ICFilter = 0x8;
    TIM_ICInitStructure.TIM_ICPolarity = TIM_ICPolarity_Rising;
    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;
    TIM_ICInit(TIM2, &TIM_ICInitStructure);

    TIM_ITConfig(TIM2, TIM_IT_CC2, ENABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);

    timer_status = WAIT_RISING;

    return BSP_TIMER_OK;
 }

/**
  * @brief  复位 TIM2_CH2 捕获状态。
  * @param  None
  * @retval None
  * @note   每次开始一次新的 HC-SR04 Echo 测量前调用：
  *         1. 清除 CC2 捕获中断挂起位。
  *         2. CNT 清零。
  *         3. CH2 捕获极性恢复为上升沿。
  *         4. 状态恢复为 WAIT_RISING，echo_us 清零。
  */
 static void BSP_TIM2_Capture_Reset()
 {
    TIM_ClearITPendingBit(TIM2, TIM_IT_CC2);
    TIM_SetCounter(TIM2, 0);
    TIM_OC2PolarityConfig(TIM2, TIM_ICPolarity_Rising);
    timer_status = WAIT_RISING;
    echo_us = 0;
 }


/**
  * @brief  等待 TIM2_CH2 捕获状态进入指定状态。
  * @param  status: 目标捕获状态。
  * @retval BSP_TIMER_Status_t:
  *         - BSP_TIMER_OK：在超时时间内进入目标状态。
  *         - BSP_TIMER_ERROR_TIMEOUT：等待捕获状态变化超时。
  * @note   当前超时时间为 30000 us，对应 HC-SR04 常用测距范围的保护等待。
  */
 static BSP_TIMER_Status_t BSP_TIM2_Capture_WaitCapture(Timer_Status status)
 {
    uint16_t timeout = TIM2_TIMEOUT;
    while(timer_status != status)
    {
        Delay_us(1);
        if(timeout-- == 0)
        {
            return BSP_TIMER_ERROR_TIMEOUT;
        }
    }
    return BSP_TIMER_OK;
 }


/**
  * @brief  获取 TIM2_CH2 捕获到的高电平持续时间。
  * @param  hightime: 输出参数，用于保存 Echo 高电平时间，单位 us。
  * @retval BSP_TIMER_Status_t:
  *         - BSP_TIMER_OK：捕获成功，hightime 数据有效。
  *         - BSP_TIMER_ERROR_PARAM：hightime 为空指针。
  *         - BSP_TIMER_ERROR_CAPTURE：等待捕获完成失败。
  * @note   本函数只负责返回高电平时间，不负责触发 HC-SR04 TRIG，
  *         也不负责将时间换算为距离。距离换算由 Drivers/hcsr04.* 完成。
  */
 BSP_TIMER_Status_t BSP_TIM2_Capture_GetHighTimeUs(uint32_t *hightime)
 {
    if(hightime == 0)
    {
        return BSP_TIMER_ERROR_PARAM;
    }

    BSP_TIM2_Capture_Reset();

    if(BSP_TIM2_Capture_WaitCapture(CAPTURE_DONE) == BSP_TIMER_OK)
    {
        *hightime = echo_us;
        return BSP_TIMER_OK;
    }
    else
    {
        return BSP_TIMER_ERROR_CAPTURE;
    }
 }


/**
  * @brief  初始化 TIM3 编码器模式及其对应 GPIO。
  * @param  None
  * @retval BSP_TIMER_Status_t:
  *         - BSP_TIMER_OK：TIM3 编码器模式初始化成功。
  * @note   当前配置用于 PC6 / TIM3_CH1 和 PC7 / TIM3_CH2：
  *         - TIM3 需要完全重映射到 PC6/PC7。
  *         - PC6/PC7 配置为上拉输入，用于接收机械编码器 A/B 相。
  *         - 使用 TIM_EncoderMode_TI12，A/B 两相均参与计数。
  *         - 计数器按 16 位运行，上层读取时转换为 int16_t 表示正负方向。
  */
 BSP_TIMER_Status_t BSP_TIM3_Encoder_Init(void)
 {
    GPIO_InitTypeDef GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_ICInitTypeDef TIM_ICInitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);

    GPIO_PinRemapConfig(GPIO_FullRemap_TIM3, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = TIM3_ENCODER_A_PIN | TIM3_ENCODER_B_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(TIM3_ENCODER_PORT, &GPIO_InitStructure);

    TIM_DeInit(TIM3);

    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = 0xFFFF;
    TIM_TimeBaseInitStructure.TIM_Prescaler = 0;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);

    TIM_ICStructInit(&TIM_ICInitStructure);
    TIM_ICInitStructure.TIM_ICFilter = TIM3_ENCODER_FILTER;
    TIM_ICInitStructure.TIM_ICPolarity = TIM_ICPolarity_Rising;
    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;

    TIM_ICInitStructure.TIM_Channel = TIM_Channel_1;
    TIM_ICInit(TIM3, &TIM_ICInitStructure);

    TIM_ICInitStructure.TIM_Channel = TIM_Channel_2;
    TIM_ICInit(TIM3, &TIM_ICInitStructure);

    TIM_EncoderInterfaceConfig(TIM3,
                               TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising,
                               TIM_ICPolarity_Rising);

    TIM_SetCounter(TIM3, 0);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_Cmd(TIM3, ENABLE);

    tim3_encoder_is_init = 1;

    return BSP_TIMER_OK;
 }


/**
  * @brief  读取 TIM3 编码器计数值。
  * @param  count: 输出参数，用于保存当前编码器计数。
  * @retval BSP_TIMER_Status_t:
  *         - BSP_TIMER_OK：读取成功。
  *         - BSP_TIMER_ERROR_PARAM：count 为空指针。
  *         - BSP_TIMER_ERROR_NOT_INIT：TIM3 编码器尚未初始化。
  * @note   TIM3 是 16 位计数器，强制转换为 int16_t 后：
  *         - 正数表示相对 0 的正向计数。
  *         - 负数表示相对 0 的反向计数。
  */
 BSP_TIMER_Status_t BSP_TIM3_Encoder_GetCount(int16_t *count)
 {
    if(count == 0)
    {
        return BSP_TIMER_ERROR_PARAM;
    }

    if(tim3_encoder_is_init == 0)
    {
        return BSP_TIMER_ERROR_NOT_INIT;
    }

    *count = (int16_t)TIM_GetCounter(TIM3);

    return BSP_TIMER_OK;
 }


/**
  * @brief  设置 TIM3 编码器计数值。
  * @param  count: 需要写入的计数值。
  * @retval BSP_TIMER_Status_t:
  *         - BSP_TIMER_OK：设置成功。
  *         - BSP_TIMER_ERROR_NOT_INIT：TIM3 编码器尚未初始化。
  * @note   用于驱动层清零或设置参考位置。
  */
 BSP_TIMER_Status_t BSP_TIM3_Encoder_SetCount(int16_t count)
 {
    if(tim3_encoder_is_init == 0)
    {
        return BSP_TIMER_ERROR_NOT_INIT;
    }

    TIM_SetCounter(TIM3, (uint16_t)count);

    return BSP_TIMER_OK;
 }


/**
  * @brief  清零 TIM3 编码器计数值。
  * @param  None
  * @retval BSP_TIMER_Status_t:
  *         - BSP_TIMER_OK：清零成功。
  *         - BSP_TIMER_ERROR_NOT_INIT：TIM3 编码器尚未初始化。
  */
 BSP_TIMER_Status_t BSP_TIM3_Encoder_ClearCount(void)
 {
    return BSP_TIM3_Encoder_SetCount(0);
 }


/**
  * @brief  TIM2 中断服务函数。
  * @param  None
  * @retval None
  * @note   仅处理 TIM2_CH2 捕获中断：
  *         - 捕获上升沿后读取 CCR2、清零 CNT，并切换为下降沿捕获。
  *         - 捕获下降沿后读取 CCR2 保存为 echo_us，并置位 CAPTURE_DONE。
  *         ISR 中只做捕获状态切换和数据保存，不进行 printf、OLED 显示或距离换算。
  */
 void TIM2_IRQHandler(void)
 {
    if(TIM_GetITStatus(TIM2, TIM_IT_CC2) == SET)
    {
        if(timer_status == WAIT_RISING)
        {
            TIM_GetCapture2(TIM2);
            TIM_SetCounter(TIM2, 0);
            TIM_OC2PolarityConfig(TIM2, TIM_ICPolarity_Falling);
            timer_status = WAIT_FALLING;
        }
        else if(timer_status == WAIT_FALLING)
        {
            echo_us = TIM_GetCapture2(TIM2);
            TIM_OC2PolarityConfig(TIM2, TIM_ICPolarity_Rising);
            timer_status = CAPTURE_DONE;
        }
        TIM_ClearITPendingBit(TIM2, TIM_IT_CC2);
    }
 }
