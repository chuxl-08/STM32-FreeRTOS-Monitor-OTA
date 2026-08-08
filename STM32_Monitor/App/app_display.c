#include "app_display.h"
#include "oled.h"
#include <stdio.h>
#include <string.h>

/*
 * 文件职责：
 * 1. 实现 OLED 页面级显示逻辑。
 * 2. 根据 DisplayTask 聚合的快照刷新自动扫描、上传状态、任务监控和手动控制页面。
 * 3. 通过行缓存减少重复 OLED 写入，I2C 互斥量由 DisplayTask 在调用前后保护。
 *
 * 当前状态：
 * - 使用 4 行固定宽度文本页面显示当前运行模式相关数据。
 */

#define APP_DISPLAY_TEMP_INVALID    "--.--"
#define APP_DISPLAY_LINE_COUNT      4
#define APP_DISPLAY_LINE_LENGTH     16
#define APP_DISPLAY_LINE_BUFFER_SIZE 17

#define APP_DISPLAY_RETURN_IF_ERROR(expr)                   \
    do                                                      \
    {                                                       \
        AppDisplay_Status_t status = (expr);                \
        if(status != APP_DISPLAY_OK)                        \
        {                                                   \
            return status;                                  \
        }                                                   \
    } while (0)
    

#define APP_DISPLAY_RETURN_IF_OLED_ERROR(expr)              \
    do                                                      \
    {                                                       \
        OLED_Status_t status = (expr);                      \
        if(status != OLED_OK)                               \
        {                                                   \
            return APP_DISPLAY_ERROR_OLED;                  \
        }                                                   \
    } while(0)


static uint8_t s_app_display_ready = 0;
static uint8_t s_app_display_line_cache_valid = 0;
static char s_app_display_line_cache[APP_DISPLAY_LINE_COUNT][APP_DISPLAY_LINE_BUFFER_SIZE];

/*
 * @brief   将app_display.* 状态码转换为可读字符串。
 * @param   status: AppDisplay_Status_t 状态码。
 * @retval  const char *: 状态名称字符串
 */
const char *AppDisplayStatusName(AppDisplay_Status_t status)
{
    switch(status)
    {
        case APP_DISPLAY_OK:
            return "OK";
        case APP_DISPLAY_ERROR_PARAM:
            return "ERR_PARAM";
        case APP_DISPLAY_ERROR_OLED:
            return "ERR_OLED";
        default:
            return "UNKNOWN";
    }
}

static void App_Display_NormalizeLine(char *line_buffer, const char *text)
{
    uint8_t i;
    uint8_t text_end;

    text_end = 0;
    for(i = 0; i < APP_DISPLAY_LINE_LENGTH; i++)
    {
        if((text != 0) && (text_end == 0))
        {
            if(text[i] != '\0')
            {
                line_buffer[i] = text[i];
            }
            else
            {
                line_buffer[i] = ' ';
                text_end = 1;
            }
        }
        else
        {
            line_buffer[i] = ' ';
        }
    }
    line_buffer[APP_DISPLAY_LINE_LENGTH] = '\0';
}

static AppDisplay_Status_t App_Display_WriteLine(uint8_t line, const char *text)
{
    char normalized_line[APP_DISPLAY_LINE_BUFFER_SIZE];

    if((line < 1) || (line > APP_DISPLAY_LINE_COUNT) || (text == 0))
    {
        return APP_DISPLAY_ERROR_PARAM;
    }

    App_Display_NormalizeLine(normalized_line, text);
    if((s_app_display_line_cache_valid == 0) ||
       (strncmp(s_app_display_line_cache[line - 1], normalized_line, APP_DISPLAY_LINE_LENGTH) != 0))
    {
        APP_DISPLAY_RETURN_IF_OLED_ERROR(OLED_ShowLineString(line, normalized_line));
        memcpy(s_app_display_line_cache[line - 1], normalized_line, APP_DISPLAY_LINE_BUFFER_SIZE);
        s_app_display_line_cache_valid = 1;
    }

    return APP_DISPLAY_OK;
}

static void App_Display_FormatTempX100(char *buffer, uint8_t buffer_size, int16_t temp_x100)
{
    int32_t temp_abs;
    uint32_t integer_part;
    uint32_t decimal_part;

    if((buffer == 0) || (buffer_size == 0))
    {
        return;
    }

    if(temp_x100 < 0)
    {
        temp_abs = -(int32_t)temp_x100;
    }
    else
    {
        temp_abs = temp_x100;
    }

    integer_part = (uint32_t)(temp_abs / 100);
    decimal_part = (uint32_t)(temp_abs % 100);
    if(integer_part > 99)
    {
        integer_part = 99;
        decimal_part = 99;
    }

    if(temp_x100 < 0)
    {
        (void)snprintf(buffer, buffer_size, "-%02lu.%02lu", (unsigned long)integer_part, (unsigned long)decimal_part);
    }
    else
    {
        (void)snprintf(buffer, buffer_size, "%02lu.%02lu", (unsigned long)integer_part, (unsigned long)decimal_part);
    }
}

static uint32_t App_Display_ClampDistanceCm(uint32_t distance_cm)
{
    if(distance_cm > 999)
    {
        return 999;
    }

    return distance_cm;
}

static const char *App_Display_GetUploadStatusText(UploadStatus_t status)
{
    if(status == UPLOAD_STATUS_OK)
    {
        return "OK";
    }

    return "FAIL";
}

static const char *App_Display_GetUploadStatusShortText(UploadStatus_t status)
{
    if(status == UPLOAD_STATUS_OK)
    {
        return "OK";
    }

    return "--";
}

static const char *App_Display_GetHumanText(uint8_t valid, uint8_t detected)
{
    if(valid == 0)
    {
        return "--";
    }

    if(detected)
    {
        return "YES";
    }

    return "NO";
}

static const char *App_Display_GetMonitorTaskShortName(AppTask_Id_t task_id)
{
    switch(task_id)
    {
        case APP_TASK_ID_SYSTEM:
            return "SYS";
        case APP_TASK_ID_UPLOAD:
            return "UPL";
        case APP_TASK_ID_DISPLAY:
            return "DIS";
        case APP_TASK_ID_MONITOR:
            return "MON";
        case APP_TASK_ID_SCAN:
            return "SCN";
        default:
            return "UNK";
    }
}

/*
 * @brief   初始化应用层显示模块。
 * @note    当前只初始化 OLED 并显示启动提示，页面内容由 DisplayTask 按当前页面刷新。
 * @retval  AppDisplay_Status_t:
 *              - APP_DISPLAY_OK: 正常
 *              - APP_DISPLAY_ERROR_PARAM: 参数错误
 *              - APP_DISPLAY_ERROR_OLED: OLED.*异常
 */
AppDisplay_Status_t App_Display_Init(void)
{
    s_app_display_ready = 0;

    APP_DISPLAY_RETURN_IF_OLED_ERROR(OLED_Init());

    s_app_display_line_cache_valid = 0;
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(1, "STM32 Monitor"));
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(2, "Display Ready"));

    s_app_display_ready = 1;

    return APP_DISPLAY_OK;
}

/*
 * @brief   刷新 OLED 自动扫描状态页。
 * @param   system_scan_data: 自动扫描数据指针。
 * @param   system_upload_status: 上传状态指针；为空时显示扫描有效性。
 * @note    第 1 行显示当前角度和当前扫描点人员检测结果。
 *          第 2 行显示当前方向距离和最高温。
 *          第 3 行显示热区像素数量和整轮 All 汇总结果。
 *          第 4 行优先显示扫描点序号、WiFi/TCP 状态；无上传状态时显示扫描有效性。
 *          All 只在 SUMMARY 快照后有效；扫描过程中 scan.valid 为 0，因此显示 "--"。
 * @retval  AppDisplay_Status_t
 */
AppDisplay_Status_t App_Display_UpdateScanWithUpload(const System_ScanTaskData_t *system_scan_data,
                                                     const System_UploadStatus_t *system_upload_status)
{
    const System_ScanPoint_t *point;
    char line_text[APP_DISPLAY_LINE_BUFFER_SIZE];
    char temp_text[8];

    if(system_scan_data == 0)
    {
        return APP_DISPLAY_ERROR_PARAM;
    }

    if(s_app_display_ready == 0)
    {
        return APP_DISPLAY_ERROR_OLED;
    }
    
    if(system_scan_data->servo.current_index >= SYSTEM_SCAN_POINT_COUNT)
    {
        return APP_DISPLAY_ERROR_PARAM;
    }

    point = &system_scan_data->points[system_scan_data->servo.current_index];

    (void)snprintf(line_text, sizeof(line_text), "A:%03lu H:%s",
                   (unsigned long)point->point_angle,
                   App_Display_GetHumanText(point->point_valid, point->point_detected.human_detected));
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(1, line_text));

    if(point->point_distance.hcsr04_data.valid)
    {
        if(point->point_thermal.amg8833_data.valid)
        {
            App_Display_FormatTempX100(temp_text, sizeof(temp_text), point->point_thermal.amg8833_data.temp_summary.max_temp_x100);
        }
        else
        {
            (void)snprintf(temp_text, sizeof(temp_text), "%s", APP_DISPLAY_TEMP_INVALID);
        }
        (void)snprintf(line_text, sizeof(line_text), "D:%03lu T:%s",
                       (unsigned long)App_Display_ClampDistanceCm(point->point_distance.hcsr04_data.distance_cm),
                       temp_text);
    }
    else
    {
        if(point->point_thermal.amg8833_data.valid)
        {
            App_Display_FormatTempX100(temp_text, sizeof(temp_text), point->point_thermal.amg8833_data.temp_summary.max_temp_x100);
        }
        else
        {
            (void)snprintf(temp_text, sizeof(temp_text), "%s", APP_DISPLAY_TEMP_INVALID);
        }
        (void)snprintf(line_text, sizeof(line_text), "D:--- T:%s", temp_text);
    }
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(2, line_text));

    if(point->point_thermal.amg8833_data.valid)
    {
        (void)snprintf(line_text, sizeof(line_text), "Hot:%02lu All:%s",
                       (unsigned long)point->point_thermal.amg8833_data.temp_summary.hot_pixel_count,
                       App_Display_GetHumanText(system_scan_data->valid, system_scan_data->detect.human_detected));
    }
    else
    {
        (void)snprintf(line_text, sizeof(line_text), "Hot:-- All:%s",
                       App_Display_GetHumanText(system_scan_data->valid, system_scan_data->detect.human_detected));
    }
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(3, line_text));

    if(system_upload_status != 0)
    {
        (void)snprintf(line_text, sizeof(line_text), "I:%lu W:%s T:%s",
                       (unsigned long)system_scan_data->servo.current_index,
                       App_Display_GetUploadStatusShortText(system_upload_status->wifi_connected),
                       App_Display_GetUploadStatusShortText(system_upload_status->tcp_connected));
    }
    else
    {
        (void)snprintf(line_text, sizeof(line_text), "I:%lu Valid:%lu",
                       (unsigned long)system_scan_data->servo.current_index,
                       (unsigned long)system_scan_data->valid);
    }
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(4, line_text));

    return APP_DISPLAY_OK;
}

/*
 * @brief   刷新 OLED 上传状态页。
 * @param   system_upload_status: 系统上传状态指针。
 * @note    本页面用于显示 WiFi/TCP 上传状态：
 *          第 1 行显示 WiFi 和 TCP 连接状态，格式为 W:OK   T:OK。
 *          第 2 行显示最近一次上传状态。
 *          第 3 行显示上传成功次数。
 *          第 4 行显示上传失败次数。
 *          计数超过 9999 时固定显示 9999。
 *          本函数不获取 I2C mutex，调用方必须在 I2C mutex 保护下调用。
 * @retval  AppDisplay_Status_t
 */
AppDisplay_Status_t App_Display_UpdateUpload(const System_UploadStatus_t *system_upload_status)
{
    char line_text[APP_DISPLAY_LINE_BUFFER_SIZE];
    uint32_t upload_count;
    uint32_t upload_fail_count;

    if(system_upload_status == 0)
    {
        return APP_DISPLAY_ERROR_PARAM;
    }

    if(s_app_display_ready == 0)
    {
        return APP_DISPLAY_ERROR_OLED;
    }

    upload_count = system_upload_status->upload_count;
    upload_fail_count = system_upload_status->upload_fail_count;
    if(upload_count > 9999)
    {
        upload_count = 9999;
    }
    if(upload_fail_count > 9999)
    {
        upload_fail_count = 9999;
    }

    (void)snprintf(line_text, sizeof(line_text), "W:%s T:%s",
                   App_Display_GetUploadStatusText(system_upload_status->wifi_connected),
                   App_Display_GetUploadStatusText(system_upload_status->tcp_connected));
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(1, line_text));

    (void)snprintf(line_text, sizeof(line_text), "Last:%s",
                   App_Display_GetUploadStatusText(system_upload_status->last_upload_status));
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(2, line_text));

    (void)snprintf(line_text, sizeof(line_text), "OK:%04lu", (unsigned long)upload_count);
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(3, line_text));

    (void)snprintf(line_text, sizeof(line_text), "Fail:%04lu", (unsigned long)upload_fail_count);
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(4, line_text));

    return APP_DISPLAY_OK;
}

/*
 * @brief   刷新 OLED 任务监控详细页。
 * @param   monitor_snapshot: 任务监控快照指针。
 * @note    本接口用于显示 MonitorTask 任务监控快照数据。
 *          每行显示一个任务的状态和 age。
 * @retval  AppDisplay_Status_t
 */
AppDisplay_Status_t App_Display_UpdateMonitor(const AppTask_MonitorSnapshot_t *monitor_snapshot)
{
    char line_text[APP_DISPLAY_LINE_BUFFER_SIZE];
    static const AppTask_Id_t s_monitor_display_tasks[APP_DISPLAY_LINE_COUNT] =
    {
        APP_TASK_ID_SYSTEM,
        APP_TASK_ID_SCAN,
        APP_TASK_ID_DISPLAY,
        APP_TASK_ID_UPLOAD
    };
    uint8_t i;
    AppTask_Id_t task_id;
    uint8_t display_count;
    uint32_t age_ms;

    if(monitor_snapshot == 0)
    {
        return APP_DISPLAY_ERROR_PARAM;
    }

    if(s_app_display_ready == 0)
    {
        return APP_DISPLAY_ERROR_OLED;
    }

    display_count = APP_TASK_ID_COUNT;
    if(display_count > APP_DISPLAY_LINE_COUNT)
    {
        display_count = APP_DISPLAY_LINE_COUNT;
    }

    for(i = 0; i < display_count; i++)
    {
        task_id = s_monitor_display_tasks[i];
        if(task_id >= APP_TASK_ID_COUNT)
        {
            return APP_DISPLAY_ERROR_PARAM;
        }

        age_ms = (uint32_t)monitor_snapshot->items[task_id].age_tick;
        if(age_ms > 99999)
        {
            age_ms = 99999;
        }

        (void)snprintf(line_text, sizeof(line_text), "%s %s %05lums",
                       App_Display_GetMonitorTaskShortName(monitor_snapshot->items[task_id].task_id),
                       (monitor_snapshot->items[task_id].health == APP_TASK_HEALTH_ALIVE) ? "OK" : "ER",
                       (unsigned long)age_ms);
        APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(i + 1, line_text));
    }

    return APP_DISPLAY_OK;
}

/*
 * @brief   更新 OLED servo manual 页。
 * @param   manual_data: ManualTask 发布的手动控制和当前方向检测数据。
 * @param   input_data: InputTask 发布的编码器输入数据。
 * @param   encoder_delta: DisplayTask 根据 encoder_total 计算出的本次显示消费增量。
 * @note    本页面用于编码器手动控制舵机和单方向检测：
 *          第 1 行显示手动模式、当前角度和 SW 按下状态。
 *          第 2 行显示当前方向距离和最高温。
 *          第 3 行显示平均温和热区像素数量。
 *          第 4 行显示人员检测结果和编码器增量。
 * @retval  AppDisplay_Status_t
 */
AppDisplay_Status_t App_Display_UpdateServoManual(const System_ManualTaskData_t *manual_data,
                                                  const System_InputTaskData_t *input_data,
                                                  int16_t encoder_delta)
{
    char line_text[APP_DISPLAY_LINE_BUFFER_SIZE];
    char temp_text[8];
    int32_t encoder_delta_abs;
    char encoder_delta_sign;
    uint8_t button_pressed;

    if(manual_data == 0 || input_data == 0)
    {
        return APP_DISPLAY_ERROR_PARAM;
    }

    if(s_app_display_ready == 0)
    {
        return APP_DISPLAY_ERROR_OLED;
    }

    button_pressed = (input_data->encoder_data.button_pressed != 0) ? 1 : 0;

    if(manual_data->valid && manual_data->servo.valid && (manual_data->servo.servo_status == SERVO_OK))
    {
        (void)snprintf(line_text, sizeof(line_text), "Manual A:%03lu P:%u",
                       (unsigned long)manual_data->servo.current_angle,
                       button_pressed);
    }
    else
    {
        (void)snprintf(line_text, sizeof(line_text), "Manual A:--- P:%u",
                       button_pressed);
    }
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(1, line_text));

    if(manual_data->thermal.amg8833_data.valid)
    {
        App_Display_FormatTempX100(temp_text, sizeof(temp_text), manual_data->thermal.amg8833_data.temp_summary.max_temp_x100);
    }
    else
    {
        (void)snprintf(temp_text, sizeof(temp_text), "%s", APP_DISPLAY_TEMP_INVALID);
    }
    if(manual_data->distance.hcsr04_data.valid)
    {
        (void)snprintf(line_text, sizeof(line_text), "D:%03lu T:%s",
                       (unsigned long)App_Display_ClampDistanceCm(manual_data->distance.hcsr04_data.distance_cm),
                       temp_text);
    }
    else
    {
        (void)snprintf(line_text, sizeof(line_text), "D:--- T:%s", temp_text);
    }
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(2, line_text));

    if(manual_data->thermal.amg8833_data.valid)
    {
        App_Display_FormatTempX100(temp_text, sizeof(temp_text), manual_data->thermal.amg8833_data.temp_summary.avg_temp_x100);
        (void)snprintf(line_text, sizeof(line_text), "A:%s H:%02lu",
                       temp_text,
                       (unsigned long)((manual_data->thermal.amg8833_data.temp_summary.hot_pixel_count > 99) ?
                                       99 :
                                       manual_data->thermal.amg8833_data.temp_summary.hot_pixel_count));
    }
    else
    {
        (void)snprintf(line_text, sizeof(line_text), "A:%s H:--", APP_DISPLAY_TEMP_INVALID);
    }
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(3, line_text));

    if(encoder_delta < 0)
    {
        encoder_delta_sign = '-';
        encoder_delta_abs = -(int32_t)encoder_delta;
    }
    else
    {
        encoder_delta_sign = '+';
        encoder_delta_abs = encoder_delta;
    }
    if(encoder_delta_abs > 999)
    {
        encoder_delta_abs = 999;
    }
    (void)snprintf(line_text, sizeof(line_text), "H:%s De:%c%03lu",
                   App_Display_GetHumanText((uint8_t)(manual_data->distance.hcsr04_data.valid &&
                                                       manual_data->thermal.amg8833_data.valid),
                                             manual_data->detect.human_detected),
                   encoder_delta_sign,
                   (unsigned long)encoder_delta_abs);
    APP_DISPLAY_RETURN_IF_ERROR(App_Display_WriteLine(4, line_text));

    return APP_DISPLAY_OK;
}
