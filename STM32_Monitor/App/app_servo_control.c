#include "app_servo_control.h"
#include "servo.h"
#include "app_task.h"

/*
 * 文件职责：
 * 1. 实现编码器手动控制舵机的应用层逻辑。
 * 2. 从 InputTask 快照读取 encoder_total，并按本模块上次消费位置计算手动增量。
 * 3. 不直接读取 Encoder 驱动，避免手动控制和菜单切页重复消费底层增量。
 */

#define APP_SERVO_CONTROL_MIN_ANGLE             30
#define APP_SERVO_CONTROL_MAX_ANGLE             150
#define APP_SERVO_CONTROL_CENTER_ANGLE          90
#define APP_SERVO_CONTROL_ANGLE_STEP            5
#define APP_SERVO_CONTROL_COUNTS_PER_STEP       5

static int16_t s_encoder_count_accumulator = 0;
static int32_t s_last_input_encoder_total = 0;
static uint8_t s_input_encoder_total_valid = 0;

/*
 * @brief   将 app_servo_control.* 状态码转换为可读字符串。
 * @param   status: AppServoControl_Status_t 状态码。
 * @retval  const char *: 状态名称字符串。
 */
const char *AppServoControlStatusName(AppServoControl_Status_t status)
{
    switch(status)
    {
        case APP_SERVO_CONTROL_OK:
            return "OK";
        case APP_SERVO_CONTROL_ERROR_PARAM:
            return "PARAM";
        case APP_SERVO_CONTROL_ERROR_INIT:
            return "INIT";
        case APP_SERVO_CONTROL_ERROR_ENCODER:
            return "ENCODER";
        case APP_SERVO_CONTROL_ERROR_SERVO:
            return "SERVO";
        default:
            return "UNKNOWN";
    }
}

/*
 * @brief   将角度限制到舵机当前项目安全范围。
 * @param   angle: 输入角度。
 * @retval  uint8_t: 限幅后的角度。
 */
static uint8_t App_ServoControl_LimitAngle(int16_t angle)
{
    if(angle < APP_SERVO_CONTROL_MIN_ANGLE)
    {
        return APP_SERVO_CONTROL_MIN_ANGLE;
    }

    if(angle > APP_SERVO_CONTROL_MAX_ANGLE)
    {
        return APP_SERVO_CONTROL_MAX_ANGLE;
    }

    return (uint8_t)angle;
}

/*
 * @brief   初始化编码器手动舵机控制。
 * @param   servo_data: 舵机状态数据指针。
 * @retval  AppServoControl_Status_t:
 *          - APP_SERVO_CONTROL_OK: 初始化成功。
 *          - APP_SERVO_CONTROL_ERROR_PARAM: 参数为空。
 *          - APP_SERVO_CONTROL_ERROR_INIT: 舵机初始化或角度设置失败。
 * @note    初始化后舵机设置到 90 度中位。
 */
AppServoControl_Status_t App_ServoControl_Init(System_ServoData_t *servo_data)
{
    Servo_Status_t servo_status;

    if(servo_data == 0)
    {
        return APP_SERVO_CONTROL_ERROR_PARAM;
    }

    s_encoder_count_accumulator = 0;
    s_last_input_encoder_total = 0;
    s_input_encoder_total_valid = 0;

    servo_status = Servo_Init();
    servo_data->servo_status = servo_status;
    servo_data->valid = 0;
    if(servo_status != SERVO_OK)
    {
        return APP_SERVO_CONTROL_ERROR_INIT;
    }

    servo_status = Servo_SetAngle(APP_SERVO_CONTROL_CENTER_ANGLE);
    servo_data->target_angle = APP_SERVO_CONTROL_CENTER_ANGLE;
    servo_data->servo_status = servo_status;
    servo_data->valid = 0;
    if(servo_status != SERVO_OK)
    {
        return APP_SERVO_CONTROL_ERROR_INIT;
    }

    servo_data->current_angle = Servo_GetAngle();
    servo_data->current_index = 0;
    servo_data->valid = 1;

    return APP_SERVO_CONTROL_OK;
}

void App_ServoControl_ResetInputBaseline(void)
{
    s_encoder_count_accumulator = 0;
    s_input_encoder_total_valid = 0;
}

/*
 * @brief   根据编码器输入更新舵机角度。
 * @param   system_data: 系统统一数据结构指针。
 * @retval  AppServoControl_Status_t:
 *          - APP_SERVO_CONTROL_OK: 更新成功。
 *          - APP_SERVO_CONTROL_ERROR_PARAM: 参数为空。
 *          - APP_SERVO_CONTROL_ERROR_ENCODER: 输入快照不可用。
 *          - APP_SERVO_CONTROL_ERROR_SERVO: 舵机设置失败。
 * @note    使用 encoder_total 差值恢复自上次手动控制周期以来的完整旋转量；
 *          编码器旋转累计到 APP_SERVO_CONTROL_COUNTS_PER_STEP 后调整一次角度。
 *          SW 点击事件由 InputTask 通知 SystemTask，用于自动 / 手动模式切换。
 */
AppServoControl_Status_t App_ServoControl_Update(System_ServoData_t *servo_data, int32_t current_encoder_total)
{
    Servo_Status_t servo_status;
    int16_t encoder_delta;
    int16_t target_angle;
    uint8_t angle_changed;

    if(servo_data == 0)
    {
        return APP_SERVO_CONTROL_ERROR_PARAM;
    }

    if(s_input_encoder_total_valid == 0)
    {
        s_last_input_encoder_total = current_encoder_total;
        s_input_encoder_total_valid = 1;
        servo_data->target_angle = Servo_GetAngle();
        servo_data->current_angle = Servo_GetAngle();
        servo_data->current_index = 0;
        servo_data->servo_status = SERVO_OK;
        servo_data->valid = 1;
        return APP_SERVO_CONTROL_OK;
    }

    encoder_delta = (int16_t)(current_encoder_total - s_last_input_encoder_total);
    s_last_input_encoder_total = current_encoder_total;
    target_angle = Servo_GetAngle();
    angle_changed = 0;

    s_encoder_count_accumulator += encoder_delta;

    while(s_encoder_count_accumulator >= APP_SERVO_CONTROL_COUNTS_PER_STEP)
    {
        target_angle += APP_SERVO_CONTROL_ANGLE_STEP;
        s_encoder_count_accumulator -= APP_SERVO_CONTROL_COUNTS_PER_STEP;
        angle_changed = 1;
    }

    while(s_encoder_count_accumulator <= -APP_SERVO_CONTROL_COUNTS_PER_STEP)
    {
        target_angle -= APP_SERVO_CONTROL_ANGLE_STEP;
        s_encoder_count_accumulator += APP_SERVO_CONTROL_COUNTS_PER_STEP;
        angle_changed = 1;
    }

    if(angle_changed)
    {
        target_angle = App_ServoControl_LimitAngle(target_angle);
        servo_data->target_angle = target_angle;

        servo_status = Servo_SetAngle(target_angle);
        servo_data->servo_status = servo_status;
        servo_data->valid = 0;
        if(servo_status != SERVO_OK)
        {
            return APP_SERVO_CONTROL_ERROR_SERVO;
        }
    }

    servo_data->current_angle = Servo_GetAngle();
    servo_data->current_index = 0;
    servo_data->servo_status = SERVO_OK;
    servo_data->valid = 1;

    return APP_SERVO_CONTROL_OK;
}
