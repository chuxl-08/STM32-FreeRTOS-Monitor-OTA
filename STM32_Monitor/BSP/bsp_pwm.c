#include "bsp_pwm.h"

/*
 * 文件职责：
 * 1. 实现 SG90 舵机 PWM 底层配置。
 * 2. 当前固定使用 PB8 / TIM4_CH3 输出 50 Hz 舵机控制信号。
 * 3. 为上层 servo.* 提供 PWM 初始化和脉宽设置能力。
 */

#define BSP_PWM_SERVO_PORT                     GPIOB
#define BSP_PWM_SERVO_PIN                      GPIO_Pin_8
#define BSP_PWM_SERVO_PORT_RCC                 RCC_APB2Periph_GPIOB
#define BSP_PWM_SERVO_TIM                      TIM4
#define BSP_PWM_SERVO_TIM_RCC                  RCC_APB1Periph_TIM4

#define BSP_PWM_SERVO_PERIOD_US                20000
#define BSP_PWM_SERVO_INIT_PULSE_US            1500
#define BSP_PWM_SERVO_MIN_PULSE_US             500
#define BSP_PWM_SERVO_MAX_PULSE_US             2500

static uint8_t s_bsp_pwm_servo_ready = 0;

/*
 * @brief   初始化 SG90 舵机 PWM 输出。
 * @note    当前固定使用 PB8 / TIM4_CH3：
 *          - TIM4CLK 按 72 MHz 计算。
 *          - 预分频 72 - 1 后计数频率为 1 MHz，计数单位为 1 us。
 *          - ARR = 20000 - 1，对应 20 ms 周期，即 50 Hz。
 *          - 初始 CCR3 = 1500，对应舵机中位附近。
 * @retval  BSP_PWM_Status_t:
 *          - BSP_PWM_OK: 初始化成功。
 */
BSP_PWM_Status_t BSP_PWM_Servo_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;

    s_bsp_pwm_servo_ready = 0;

    RCC_APB1PeriphClockCmd(BSP_PWM_SERVO_TIM_RCC, ENABLE);
    RCC_APB2PeriphClockCmd(BSP_PWM_SERVO_PORT_RCC, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = BSP_PWM_SERVO_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BSP_PWM_SERVO_PORT, &GPIO_InitStructure);

    TIM_TimeBaseStructInit(&TIM_TimeBaseInitStructure);
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = BSP_PWM_SERVO_PERIOD_US - 1;
    TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1;
    TIM_TimeBaseInit(BSP_PWM_SERVO_TIM, &TIM_TimeBaseInitStructure);

    TIM_OCStructInit(&TIM_OCInitStructure);
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_Pulse = BSP_PWM_SERVO_INIT_PULSE_US;
    TIM_OC3Init(BSP_PWM_SERVO_TIM, &TIM_OCInitStructure);

    TIM_OC3PreloadConfig(BSP_PWM_SERVO_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(BSP_PWM_SERVO_TIM, ENABLE);

    TIM_SetCompare3(BSP_PWM_SERVO_TIM, BSP_PWM_SERVO_INIT_PULSE_US);
    TIM_Cmd(BSP_PWM_SERVO_TIM, ENABLE);

    s_bsp_pwm_servo_ready = 1;

    return BSP_PWM_OK;
}

/*
 * @brief   设置 SG90 舵机 PWM 高电平时间。
 * @param   pulse_us: PWM 高电平时间，单位 us。
 * @note    限制在 500 us 到 2500 us，避免输出明显超出 SG90 控制范围。
 *          上层 servo.* 后续负责角度到 pulse_us 的转换。
 * @retval  BSP_PWM_Status_t:
 *          - BSP_PWM_OK: 设置成功。
 *          - BSP_PWM_ERROR_NOT_INIT: PWM 尚未初始化。
 *          - BSP_PWM_ERROR_RANGE: pulse_us 超出允许范围。
 */
BSP_PWM_Status_t BSP_PWM_Servo_SetPulseUs(uint16_t pulse_us)
{
    if(s_bsp_pwm_servo_ready == 0)
    {
        return BSP_PWM_ERROR_NOT_INIT;
    }

    if((pulse_us < BSP_PWM_SERVO_MIN_PULSE_US) || (pulse_us > BSP_PWM_SERVO_MAX_PULSE_US))
    {
        return BSP_PWM_ERROR_RANGE;
    }

    TIM_SetCompare3(BSP_PWM_SERVO_TIM, pulse_us);

    return BSP_PWM_OK;
}
