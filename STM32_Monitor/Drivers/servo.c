#include "servo.h"
#include "bsp_pwm.h"

/*
 * 文件职责：
 * 1. 实现 SG90 舵机控制。
 * 2. 使用 TIM4_CH3 / PB8 输出 50 Hz PWM。
 * 3. 处理角度限幅、脉宽计算和当前角度记录。
 *
 */

#define SERVO_MIN_ANGLE                    30
#define SERVO_MAX_ANGLE                    150
#define SERVO_INIT_ANGLE                   90

#define SERVO_MIN_PULSE_US                 500
#define SERVO_MAX_PULSE_US                 2500
#define SERVO_MAX_MECHANICAL_ANGLE         180

static uint8_t s_servo_current_angle = SERVO_INIT_ANGLE;
static uint16_t s_servo_current_pulse_us = 1500;
static uint8_t s_servo_init_status = 0;

/*
 * @brief   将角度限制到当前项目允许的安全范围。
 * @param   angle: 输入角度，单位度。
 * @retval  uint8_t: 限幅后的角度。
 * @note    限制在 30 到 150 度
 */
static uint8_t Servo_LimitAngle(uint8_t angle)
{
    if(angle < SERVO_MIN_ANGLE)
    {
        return SERVO_MIN_ANGLE;
    }

    if(angle > SERVO_MAX_ANGLE)
    {
        return SERVO_MAX_ANGLE;
    }

    return angle;
}

/*
 * @brief   将舵机角度换算为 PWM 高电平时间。
 * @param   angle: 舵机角度，单位度。
 * @retval  uint16_t: PWM 高电平时间，单位 us。
 * @note    SG90 常见线性映射：
 *          0 度约 500 us，90 度约 1500 us，180 度约 2500 us。
 */
static uint16_t Servo_AngleToPulseUs(uint8_t angle)
{
    uint32_t pulse_us;

    pulse_us = SERVO_MIN_PULSE_US;
    pulse_us += ((uint32_t)angle * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US)) / SERVO_MAX_MECHANICAL_ANGLE;

    return (uint16_t)pulse_us;
}

/*
 * @brief   初始化 SG90 舵机驱动。
 * @note    初始化底层 PWM 后，将舵机设置到 90 度中位。
 * @retval  Servo_Status_t:
 *          - SERVO_OK: 初始化成功。
 *          - SERVO_ERROR_PWM: 底层 PWM 初始化或设置失败。
 */
Servo_Status_t Servo_Init(void)
{
    if(s_servo_init_status)
    {
        return SERVO_OK;
    }
    else
    {
        if(BSP_PWM_Servo_Init() != BSP_PWM_OK)
        {
            return SERVO_ERROR_PWM;
        }

        if(Servo_SetAngle(SERVO_INIT_ANGLE) != SERVO_OK)
        {
            return SERVO_ERROR_PWM;
        }

        s_servo_init_status = 1;
        return SERVO_OK;
    }
}

/*
 * @brief   设置 SG90 舵机目标角度。
 * @param   angle: 目标角度，单位度。自动限幅到 30 到 150 度。
 * @retval  Servo_Status_t:
 *          - SERVO_OK: 设置成功。
 *          - SERVO_ERROR_PWM: 底层 PWM 设置失败。
 */
Servo_Status_t Servo_SetAngle(uint8_t angle)
{
    uint8_t limited_angle;
    uint16_t pulse_us;

    limited_angle = Servo_LimitAngle(angle);
    pulse_us = Servo_AngleToPulseUs(limited_angle);

    if(BSP_PWM_Servo_SetPulseUs(pulse_us) != BSP_PWM_OK)
    {
        return SERVO_ERROR_PWM;
    }

    s_servo_current_angle = limited_angle;
    s_servo_current_pulse_us = pulse_us;

    return SERVO_OK;
}

/*
 * @brief   获取当前记录的舵机角度。
 * @retval  uint8_t: 当前角度，单位度。
 */
uint8_t Servo_GetAngle(void)
{
    return s_servo_current_angle;
}

/*
 * @brief   获取当前记录的 PWM 高电平时间。
 * @retval  uint16_t: 当前 PWM 高电平时间，单位 us。
 */
uint16_t Servo_GetPulseUs(void)
{
    return s_servo_current_pulse_us;
}
