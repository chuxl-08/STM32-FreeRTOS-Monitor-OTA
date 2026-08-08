#include "app_input.h"

/*
 * 文件职责：
 * 1. 初始化并统一更新编码器输入。
 * 2. 维护 InputTask 本地输入数据，包含本周期增量与累计旋转量。
 * 3. 避免多个业务模块直接调用 Encoder_GetDelta() 抢先消费输入。
 */


/*
 * @brief   将 app_input.* 状态码转换为可读字符串。
 * @param   status: AppInput_Status_t 状态码。
 * @retval  const char *: 状态名称字符串。
 */
const char *AppInputStatusName(AppInput_Status_t status)
{
    switch(status)
    {
        case APP_INPUT_OK:
            return "OK";
        case APP_INPUT_ERROR_INIT:
            return "INIT_ERR";
        case APP_INPUT_ERROR_ENCODER:
            return "ENCODER_ERR";
        default:
            return "UNKNOWN";
    }
}

/*
 * @brief   清空输入数据缓存。
 * @retval  None
 * @note    初始化阶段或输入读取失败时调用，避免上层继续使用过期输入事件。
 */
static void App_Input_ClearData(Encoder_Data_t *encoder_data)
{
    if(encoder_data == 0)
    {
        return;
    }

    encoder_data->encoder_delta = 0;
    encoder_data->button_pressed = 0;
    encoder_data->button_clicked = 0;
}

/*
 * @brief   初始化输入模块。
 * @retval  AppInput_Status_t:
 *          - APP_INPUT_OK: 初始化成功。
 *          - APP_INPUT_ERROR_INIT: 编码器初始化失败。
 * @note    AppInput 是项目内唯一直接初始化 Encoder 的应用层模块。
 */
AppInput_Status_t App_Input_Init(System_InputTaskData_t *input_data)
{
    Encoder_Status_t encoder_status;

    if(input_data == 0)
    {
        return APP_INPUT_ERROR_INIT;
    }

    App_Input_ClearData(&input_data->encoder_data);
    input_data->encoder_total = 0;
    input_data->valid = 0;

    encoder_status = Encoder_Init();
    input_data->encoder_status = encoder_status;
    if(encoder_status != ENCODER_OK)
    {
        return APP_INPUT_ERROR_INIT;
    }

    return APP_INPUT_OK;
}

/*
 * @brief   采集并缓存一轮编码器输入。
 * @retval  AppInput_Status_t:
 *          - APP_INPUT_OK: 更新成功。
 *          - APP_INPUT_ERROR_ENCODER: 编码器按键或旋转增量读取失败。
 * @note    先更新 SW 按键，再读取旋转增量；encoder_delta 保存本周期瞬时增量，
 *          encoder_total 累加全部增量，供低频消费者按差值恢复完整输入。
 */
AppInput_Status_t App_Input_Update(System_InputTaskData_t *input_data)
{
    Encoder_Status_t encoder_status;
    int16_t encoder_delta;

    if(input_data == 0)
    {
        return APP_INPUT_ERROR_ENCODER;
    }

    encoder_status = Encoder_ButtonUpdate();
    input_data->encoder_status = encoder_status;
    if(encoder_status != ENCODER_OK)
    {
        App_Input_ClearData(&input_data->encoder_data);
        input_data->valid = 0;
        return APP_INPUT_ERROR_ENCODER;
    }

    input_data->encoder_data.button_pressed = Encoder_ButtonIsPressed();
    input_data->encoder_data.button_clicked = Encoder_ButtonWasClicked();

    encoder_status = Encoder_GetDelta(&encoder_delta);
    input_data->encoder_status = encoder_status;
    if(encoder_status != ENCODER_OK)
    {
        input_data->encoder_data.encoder_delta = 0;
        input_data->valid = 0;
        return APP_INPUT_ERROR_ENCODER;
    }

    input_data->encoder_data.encoder_delta = encoder_delta;
    input_data->encoder_total += encoder_delta;
    input_data->valid = 1;

    return APP_INPUT_OK;
}
