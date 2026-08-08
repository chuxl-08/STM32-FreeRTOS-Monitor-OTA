#include "hcsr04.h"
#include "bsp_timer.h"
#include "Delay.h"

/*
 * 文件职责：
 * 1. 实现 HC-SR04 测距驱动。
 * 2. 控制 PC3 输出 Trig 触发脉冲。
 * 3. 调用 BSP TIM2 输入捕获接口获取 Echo 高电平时间。
 * 4. 将 Echo 高电平时间换算为距离，并进行有效范围判断。
 *
 * 当前状态：
 * - 已完成初始化、触发测距、距离换算和基础错误处理。
 * - Echo 输入由 BSP/bsp_timer.* 通过 PA1 / TIM2_CH2 完成。
 */

 /*
  * HC-SR04 引脚分配：
  * - Trig: PC3，由 MCU 输出触发脉冲。
  * - Echo: PA1 / TIM2_CH2，由 BSP_Timer 输入捕获测量高电平时间。
  */
 #define HCSR04_TRIG_PORT           GPIOC
 #define HCSR04_TRIG_PIN            GPIO_Pin_3
 #define HCSR04_RCC                 RCC_APB2Periph_GPIOC

 /*
  * HC-SR04 建议两次测距之间保留至少 60 ms 间隔，
  * 避免前一次超声波反射影响下一次测量。
  */
 #define HCSR04_TIME_LAG_MS         60

 static uint8_t s_hcsr04_init_status = 0;

 /**
  * @brief  初始化 HC-SR04 驱动。
  * @param  None
  * @retval HCSR04_Status_t:
  *         - HCSR04_OK：初始化成功。
  *         - HCSR04_ERROR_TIMER：TIM2 输入捕获初始化失败。
  * @note   本函数负责初始化 Trig 输出 GPIO，并调用 BSP_Timer_Init()
  *         完成 PA1 / TIM2_CH2 输入捕获配置。
  */
 HCSR04_Status_t HCSR04_Init(void)
 {
    if(s_hcsr04_init_status)
    {
        return HCSR04_OK;
    }
    else
    {
        RCC_APB2PeriphClockCmd(HCSR04_RCC, ENABLE);

        GPIO_InitTypeDef GPIO_InitStruct;
        GPIO_InitStruct.GPIO_Pin = HCSR04_TRIG_PIN;
        GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_InitStruct.GPIO_Speed = GPIO_Speed_10MHz;
        GPIO_Init(HCSR04_TRIG_PORT, &GPIO_InitStruct);

        GPIO_ResetBits(HCSR04_TRIG_PORT, HCSR04_TRIG_PIN);

        if(BSP_Timer_Init() != BSP_TIMER_OK)
        {
            return HCSR04_ERROR_TIMER;
        }
        else
        {
            s_hcsr04_init_status = 1;
            return HCSR04_OK;
        }
    }

 }

 /**
  * @brief  发送 HC-SR04 Trig 触发脉冲。
  * @param  None
  * @retval None
  * @note   HC-SR04 要求 Trig 高电平持续至少 10 us。
  *         当前输出约 20 us 高电平脉冲，随后拉低等待 Echo 响应。
  */
  static void HCSR04_StartSignal(void)
  {
    GPIO_SetBits(HCSR04_TRIG_PORT, HCSR04_TRIG_PIN);
    Delay_us(20);
    GPIO_ResetBits(HCSR04_TRIG_PORT, HCSR04_TRIG_PIN);
  }

 /**
  * @brief  读取 HC-SR04 测距结果。
  * @param  data: 输出参数，用于保存距离值 单位 cm 和数据有效性。
  * @retval HCSR04_Status_t:
  *         - HCSR04_OK：测距成功，distance_cm 数据有效。
  *         - HCSR04_ERROR_PARAM：distance_cm 为空指针。
  *         - HCSR04_ERROR_NO_ECHO：未捕获到有效 Echo 高电平。
  *         - HCSR04_ERROR_RANGE：距离超出当前有效范围。
  * @note   距离换算使用常见近似公式：
  *         distance_cm = echo_high_time_us / 58。
  *         当前有效范围按 HC-SR04 常用测距范围限制为 2 cm ~ 400 cm。
  */
  HCSR04_Status_t HCSR04_Read(HCSR04_Data_t *data)
  {
    if(data == 0)
    {
        return HCSR04_ERROR_PARAM;
    }
    data->distance_cm = 0;
    data->valid = 0;

    Delay_ms(HCSR04_TIME_LAG_MS);

    uint32_t hightime;
    uint16_t distance_result;

    HCSR04_StartSignal();
    if(BSP_TIM2_Capture_GetHighTimeUs(&hightime) != BSP_TIMER_OK)
    {
        return HCSR04_ERROR_NO_ECHO;
    }
    else
    {
        distance_result = hightime / 58 ;
        if(2 <= distance_result && distance_result <= 400)
        {
            data->distance_cm = distance_result;
            data->valid = 1;
            return HCSR04_OK;
        }
        else
        {
            data->distance_cm = 0;
            data->valid = 0;
            return HCSR04_ERROR_RANGE;
        }
    }

 }
