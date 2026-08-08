#include "app_task.h"
#include <stdio.h>
#include <string.h>
#include "queue.h"
#include "semphr.h"


#include "system_data.h"
#include "app_upload.h"
#include "app_log.h"
#include "app_display.h"
#include "app_system.h"
#include "app_menu.h"
#include "app_scan.h"
#include "app_sensor.h"
#include "app_input.h"
#include "app_servo_control.h"
#include "app_detect.h"
#include "bsp_iwdg.h"
#include "app_ota_confirm.h"
#include "app_ota_download.h"
#include "app_ota_config.h"
#include "upgrade_config_if.h"


/*
 * 文件职责：
 * 1. 实现 FreeRTOS 应用任务函数和任务间通信资源。
 * 2. 维护 System/Input/Scan/Manual/Env/Display/Upload/Monitor 任务协作。
 * 3. 统一管理快照队列、模式切换 ACK、I2C 互斥量和任务心跳。
 *
 * 当前状态：
 * - SystemTask 负责模式协调，业务任务拥有各自数据并发布快照。
 * - MonitorTask 根据任务心跳决定是否集中喂 IWDG。
 */

#define QUEUE_INIT_STATUS_CHECK(handle, length, type)               \
            do                                                      \
            {                                                       \
                if(handle == NULL)                                  \
                {                                                   \
                    handle = xQueueCreate(length, sizeof(type));         \
                    if(handle == NULL)                              \
                    {                                               \
                        return APP_TASK_ERROR_QUEUE_INIT;           \
                    }                                               \
                }                                                   \
            } while (0)


/* ******************************* UploadTask相关宏  ******************************* */
/*
 * - UploadTask运行时间间隔
 * - Wifi、TCP重连基础时间间隔
 * - 最大重连时间间隔
 * - UploadTask连接异常时进入网络低活动退避状态
 */
#define UPLOAD_TASK_RUN_INTERVAL_MS             5000
#define UPLOAD_RECONNECT_INTERVAL_MS            3000
#define UPLOAD_RECONNECT_MAX_INTERVAL_MS        20000
#define UPLOAD_ESP01_MUTEX_WAIT_MS              1000

/* ******************************* OtaDownloadTask相关宏  ******************************* */
/*
 * - OTA 下载请求通知 bit。
 * - 当前只定义一个请求 bit，后续可扩展取消、查询状态或强制重试 bit。
 */
#define OTA_DOWNLOAD_NOTIFY_REQUEST             (1UL << 0)
#define OTA_DOWNLOAD_NOTIFY_ALL                 OTA_DOWNLOAD_NOTIFY_REQUEST

/* ****************************** DisplayTask相关宏  ******************************* */
/*
 * - 等待I2C互斥锁超时时间
 * - DisplayTask重新尝试初始化相关硬件时间间隔
 * - DisplayTask空闲降刷新阈值与空闲刷新周期
 */
#define DISPLAY_MUTEX_WAIT_MS                   100
#define DISPLAY_TASK_RUN_INTERVAL_MS            100
#define DISPLAY_TASK_STARTUP_DELAY_MS           300
#define DISPLAY_TASK_REINIT_INTERVAL_MS         500
#define DISPLAY_IDLE_TIMEOUT_MS                 3000
#define DISPLAY_IDLE_REFRESH_INTERVAL_MS        1000

/* ******************************* MonitorTask相关宏  ******************************* */
/*
 * - 系统资源记录时间间隔
 * - MonitorTask运行时间间隔
 * - SystemTask超时阈值
 * - DisplayTask超时阈值
 * - UploadTask超时阈值
 * - MonitorTask超时阈值
 * - ScanTask超时阈值
 * - EnvTask超时阈值
 * - InputTask超时阈值
 * - ManualTask超时阈值
 */
#define MONITOR_RESOURCE_LOG_PERIOD_MS          10000
#define MONITOR_TASK_RUN_INTERVAL_MS            5000
#define MONITOR_SYSTEM_WARN_THRESHOLD_MS        1000
#define MONITOR_DISPLAY_WARN_THRESHOLD_MS       3000
#define MONITOR_UPLOAD_WARN_THRESHOLD_MS        30000
#define MONITOR_OTA_DOWNLOAD_WARN_THRESHOLD_MS  30000
#define MONITOR_MONITOR_WARN_THRESHOLD_MS       6000
#define MONITOR_SCAN_WARN_THRESHOLD_MS          3000
#define MONITOR_ENV_WARN_THRESHOLD_MS           3000
#define MONITOR_INPUT_WARN_THRESHOLD_MS         40
#define MONITOR_MANUAL_THRESHOLD_MS             3000
/* ******************************* ScanTask相关宏  ******************************* */
/*
 * - ScanTask运行时间间隔
 * - ScanTask重新尝试初始化相关硬件时间间隔
 * - 两轮完整自动扫描之间的等待间隔
 */
#define SCAN_TASK_RUN_INTERVAL_MS               100
#define SCAN_TASK_REINIT_INTERVAL_MS            500
#define SCAN_TASK_SCAN_INTERVAL_MS              3000

/* ******************************* EnvTask相关宏  ******************************* */
/*
 * - EnvTask运行时间间隔
 * - EnvTask重新尝试初始化相关硬件时间间隔
 */
#define ENV_TASK_RUN_INTERVAL_MS                2000
#define ENV_TASK_REINIT_INTERVAL_MS             500

/* ******************************* InputTask相关宏  ******************************* */
/*
 * - InputTask运行时间间隔
 * - InputTask重新尝试初始化相关硬件时间间隔
 */
#define INPUT_TASK_RUN_INTERVAL_MS              20
#define INPUT_TASK_REINIT_INTERVAL_MS           500

/* ******************************* ManualTask相关宏  ******************************* */
/*
 * - ManualTask运行时间间隔
 * - ManualTask方向传感器与检测刷新间隔
 * - ManualTask重新尝试初始化相关硬件时间间隔
 */
#define MANUAL_TASK_RUN_INTERVAL_MS              20
#define MANUAL_TASK_SENSOR_INTERVAL_MS           200
#define MANUAL_TASK_REINIT_INTERVAL_MS           500

/* ******************************* SystemTask相关宏  ******************************* */
/*
 * - InputTask运行时间间隔
 */
#define SYSTEM_TASK_RUN_INTERVAL_MS              100
#define SYSTEM_TASK_ACK_WAIT_MS                 1000

/* ********************************* 任务句柄  ********************************* */
/*
 * - SystemTask
 * - UploadTask
 * - DisplayTask
 * - MonitorTask
 * - ScanTask
 * - EnvTask
 * - InputTask
 * - ManualTask
 *
 */
extern TaskHandle_t system_task_handle;
extern TaskHandle_t upload_task_handle;
extern TaskHandle_t ota_download_task_handle;
extern TaskHandle_t display_task_handle;
extern TaskHandle_t monitor_task_handle;
extern TaskHandle_t scan_task_handle;
extern TaskHandle_t env_task_handle;
extern TaskHandle_t input_task_handle;
extern TaskHandle_t manual_task_handle;
/* ********************************* 队列句柄  ********************************* */
/*
 * - 系统数据快照
 * - 显示数据快照
 * - 上传任务状态快照
 * - 任务监视数据快照
 * - 多角度扫描数据快照
 * - 环境数据快照
 */
static QueueHandle_t queuehandle_system_data = NULL;
static QueueHandle_t queuehandle_upload_snapshot = NULL;
static QueueHandle_t queuehandle_monitor_snapshot = NULL;
static QueueHandle_t queuehandle_scan_snapshot = NULL;
static QueueHandle_t queuehandle_env_snapshot = NULL;
static QueueHandle_t queuehandle_input_snapshot = NULL;
static QueueHandle_t queuehandle_manual_snapshot = NULL;
static QueueHandle_t queuehandle_system_ack = NULL;

/* ********************************* 互斥锁  ********************************* */
/*
 * - I2C互斥锁：保护 OLED/传感器等 I2C 总线访问。
 * - ESP01互斥锁：保护 USART3、AT Parser 和 ESP01 TCP 命令序列。
 */
static SemaphoreHandle_t mutexhandle_i2c = NULL;
static SemaphoreHandle_t mutexhandle_esp01 = NULL;

/* ********************************* OTA请求  ********************************* */
/*
 * OtaDownloadTask 通过 task notification 被唤醒；PATH/VER 等请求参数保存在这里。
 * 写入和取出都放在临界区内，避免 UploadTask 更新请求时 OtaDownloadTask 读到半帧数据。
 */
static AppOtaDownload_Request_t ota_download_pending_request;

/* ********************************* 心跳Tick  ********************************* */
/*
 * - 心跳数组
 */
static TickType_t task_heartbeat_ticks[APP_TASK_ID_COUNT] = {0};

/* ********************************* 相关数值转字符串  ********************************* */
/*
 * @brief   将app_task.* 状态码转换为可读字符串。
 * @param   status: AppTask_Status_t 状态码。
 * @retval  const char *: 状态名称字符串。
 */
const char *AppTaskStatusName(AppTask_Status_t status)
{
    switch(status)
    {
        case APP_TASK_OK:
            return "OK";
        case APP_TASK_ERROR_PARAM:
            return "PARAM";
        case APP_TASK_ERROR_QUEUE_INIT:
            return "ERR_QUEUE_INIT";
        case APP_TASK_ERROR_QUEUE_WRITE:
            return "ERR_QUEUE_WRITE";
        case APP_TASK_ERROR_QUEUE_READ:
            return "ERR_QUEUE_READ";
        case APP_TASK_ERROR_MUTEX_INIT:
            return "ERR_MUTEX_INIT";
        case APP_TASK_ERROR_MUTEX_TAKE:
            return "ERR_MUTEX_TAKE";
        case APP_TASK_ERROR_MUTEX_GIVE:
            return "ERR_MUTEX_GIVE";
        default:
            return "UNKNOWN";
    }
}

/*
 * @brief   判断任务是否属于当前完整业务任务集。
 * @param   task_id: AppTask_Id_t 任务 ID。
 * @retval  1: 任务会创建并由 MonitorTask 监控。
 *          0: task_id 非法。
 */
uint8_t App_Task_IsTaskEnabled(AppTask_Id_t task_id)
{
    return (task_id < APP_TASK_ID_COUNT) ? 1U : 0U;
}

/*
 * @brief   任务ID转 任务名 字符串。
 * @param   id: 任务ID。
 * @retval  const char *任务名字符串。
 */
static const char *AppTaskIdName(AppTask_Id_t id)
{
    switch (id)
    {
        case APP_TASK_ID_SYSTEM:
            return "SystemTask";
        case APP_TASK_ID_UPLOAD:
            return "UploadTask";
        case APP_TASK_ID_OTA_DOWNLOAD:
            return "OtaDownloadTask";
        case APP_TASK_ID_DISPLAY:
            return "DisplayTask";
        case APP_TASK_ID_MONITOR:
            return "MonitorTask";
        case APP_TASK_ID_SCAN:
            return "ScanTask";
        case APP_TASK_ID_ENV:
            return "EnvTask";
        case APP_TASK_ID_INPUT:
            return "InputTask";
        case APP_TASK_ID_MANUAL:
            return "ManualTask";
        default:
            return "UnknownTask";
    }
}

/*
 * @brief   任务状态转字符串
 * @param   health: 任务状态值。
 * @retval  const char *任务状态字符串。
 */
static const char *MonitorTask_HealthName(AppTask_Health_t health)
{
    switch(health)
    {
        case APP_TASK_HEALTH_ALIVE:
            return "OK";
        case APP_TASK_HEALTH_STALE:
            return "STALE";
        default:
            return "UNKNOWN";
    }
}

/*
 * @brief   显示页面转字符串。
 * @param   page: 显示页面枚举值。
 * @retval  const char *显示页面名称。
 */
static const char *DisplayTask_PageName(DisplayPage_t page)
{
    switch(page)
    {
        case DISPLAY_PAGE_AUTO_SCAN:
            return "AUTO_SCAN";
        case DISPLAY_PAGE_UPLOAD:
            return "UPLOAD";
        case DISPLAY_PAGE_MONITOR:
            return "MONITOR";
        default:
            return "UNKNOWN";
    }
}

/*
 * @brief   上传连接状态转日志字符串。
 * @param   status: UploadStatus_t 状态码。
 * @retval  const char *: OK / FAIL / UNKNOWN。
 * @note    用于让 UploadTask 的 WiFi/TCP 状态日志保持 NAME(code) 格式。
 */
static const char *UploadTask_ConnectionStatusName(UploadStatus_t status)
{
    switch(status)
    {
        case UPLOAD_STATUS_OK:
            return "OK";
        case UPLOAD_STATUS_FAIL:
            return "FAIL";
        default:
            return "UNKNOWN";
    }
}


/*
 * @brief   初始化应用任务间通信资源。
 * @param   无。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 队列资源创建成功或已经创建。
 *          - APP_TASK_ERROR_QUEUE_INIT: 系统数据队列或显示快照队列创建失败。
 * @note    当前创建多个任务间通信对象：
 *          1. SystemTask_Data_t 状态队列用于发布最新运行态，DisplayTask/UploadTask 均以 peek 方式读取。
 *          2. Scan/Monitor/Manual 快照队列只保存生产任务持有的快照指针，消费任务只读且不释放。
 *          3. System ACK 队列只用于 ScanTask/ManualTask 对 SystemTask 命令的回应。
 */
AppTask_Status_t App_Task_Init(void)
{
    if((queuehandle_system_data != NULL)
        && (mutexhandle_i2c != NULL)
        && (mutexhandle_esp01 != NULL)
        && (queuehandle_upload_snapshot != NULL)
        && (queuehandle_monitor_snapshot != NULL)
        && (queuehandle_scan_snapshot != NULL)
        && (queuehandle_env_snapshot != NULL)
        && (queuehandle_input_snapshot != NULL)
        && (queuehandle_manual_snapshot != NULL)
        && (queuehandle_system_ack != NULL)
    )
    {
        return APP_TASK_OK;
    }

    // 队列初始化检查与初始化
    QUEUE_INIT_STATUS_CHECK(queuehandle_system_data, 1, SystemTask_Data_t);
    QUEUE_INIT_STATUS_CHECK(queuehandle_upload_snapshot, 1, AppTask_UploadStatusSnapshot_t);
    QUEUE_INIT_STATUS_CHECK(queuehandle_monitor_snapshot, 1, const AppTask_MonitorSnapshot_t *);
    QUEUE_INIT_STATUS_CHECK(queuehandle_scan_snapshot, 1, const AppTask_ScanSnapshot_t *);
    QUEUE_INIT_STATUS_CHECK(queuehandle_env_snapshot, 1, AppTask_EnvSnapshot_t);
    QUEUE_INIT_STATUS_CHECK(queuehandle_input_snapshot, 1, AppTask_InputSnapshot_t);
    QUEUE_INIT_STATUS_CHECK(queuehandle_manual_snapshot, 1, const AppTask_ManualSnapshot_t *);
    QUEUE_INIT_STATUS_CHECK(queuehandle_system_ack, 4, AppTask_Ack_t);

    if(mutexhandle_i2c == NULL)
    {
        mutexhandle_i2c = xSemaphoreCreateMutex();
        if(mutexhandle_i2c == NULL)
        {
            return APP_TASK_ERROR_MUTEX_INIT;
        }
    }

    if(mutexhandle_esp01 == NULL)
    {
        mutexhandle_esp01 = xSemaphoreCreateMutex();
        if(mutexhandle_esp01 == NULL)
        {
            return APP_TASK_ERROR_MUTEX_INIT;
        }
    }

    return APP_TASK_OK;
}


/*
 * @brief   申请 I2C 互斥锁并完成显示模块初始化。
 * @param   无。
 * @retval  AppDisplay_Status_t:
 *          - APP_DISPLAY_OK: 显示模块初始化成功。
 *          - APP_DISPLAY_ERROR_I2C_MUTEX: I2C 互斥锁申请或释放失败。
 *          - 其他 AppDisplay_Status_t 错误码: 显示模块初始化失败。
 * @note    在显示任务初始化阶段对 App_Display_Init() 进行封装。
 *          函数内部先申请 I2C 互斥锁，初始化完成后释放互斥锁。
 */
static AppDisplay_Status_t DisplayTask_InitWithI2C(void)
{
    AppTask_Status_t i2c_mutex_status;
    AppDisplay_Status_t display_status;

    i2c_mutex_status = App_Task_TakeI2C(portMAX_DELAY);
    if(i2c_mutex_status != APP_TASK_OK)
    {
        App_LogPrintf("[DISPLAY] Take_i2c_mutex_fail:%s(%d)\r\n",
                      AppTaskStatusName(i2c_mutex_status),
                      i2c_mutex_status);

        return APP_DISPLAY_ERROR_I2C_MUTEX;
    }

    display_status = App_Display_Init();

    i2c_mutex_status = App_Task_GiveI2C();
    if(i2c_mutex_status != APP_TASK_OK)
    {
        App_LogPrintf("[DISPLAY] Release_i2c_mutex_fail:%s(%d)\r\n",
                      AppTaskStatusName(i2c_mutex_status),
                      i2c_mutex_status);
        return APP_DISPLAY_ERROR_I2C_MUTEX;
    }

    return display_status;
}

/* ******************************* SystemTask相关函数  ******************************* */
/*
 * @brief   发布 SystemTask 运行态快照。
 * @param   data: 待发布的 SystemTask_Data_t 数据指针。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 快照发布成功。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 系统数据队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_WRITE: 队列写入失败。
 * @note    使用 xQueueOverwrite()，队列中始终只保留最新一帧运行态数据。
 */
AppTask_Status_t App_Task_ReleaseSystemTaskData(const SystemTask_Data_t *data)
{
    BaseType_t overwrite_status;
    if(data == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_system_data == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    overwrite_status = xQueueOverwrite(queuehandle_system_data, data);
    if(overwrite_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_WRITE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   等待并读取 SystemTask 发布的运行态快照。
 * @param   data: 用于保存接收到的 SystemTask_Data_t 数据指针。
 * @param   wait_ticks: 等待队列数据的最大 Tick 数。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功读取快照。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 系统数据队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_READ: 等待超时或队列读取失败。
 * @note    使用 xQueuePeek()，读取后不移除队列内容，避免 DisplayTask 和 UploadTask
 *          多消费者读取同一个运行态队列时互相消耗数据。
 */
AppTask_Status_t App_Task_WaitSystemTaskData(SystemTask_Data_t *data,
TickType_t wait_ticks)
{
    BaseType_t peek_status;
    if(data == 0)
    {
        return  APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_system_data == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    peek_status = xQueuePeek(queuehandle_system_data, data, wait_ticks);
    if(peek_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_READ;
    }

    return APP_TASK_OK;
}

/*
 * @brief   向 SystemTask 回传命令处理完成 ACK。
 * @param   task_id: 回应命令的任务 ID。
 * @param   cmd: 已处理完成的命令。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: ACK 已写入队列。
 *          - 其他值: 参数错误、ACK 队列未创建或队列写入失败。
 * @note    ACK 使用独立队列，不复用 SystemTask 的任务通知通道。
 *          SystemTask 的任务通知只表示 InputTask 按键事件；ScanTask/ManualTask
 *          的 RUN/PAUSE 回应放入 ACK 队列，避免通知值和命令值互相混淆。
 */
static AppTask_Status_t App_Task_ReleaseTaskAck(AppTask_Id_t task_id, AppTask_Cmd cmd)
{
    AppTask_Ack_t ack;
    BaseType_t send_status;

    if((task_id >= APP_TASK_ID_COUNT) || (cmd == TASK_CMD_NONE))
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_system_ack == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    ack.task_id = task_id;
    ack.cmd = cmd;
    ack.update_tick = xTaskGetTickCount();

    send_status = xQueueSend(queuehandle_system_ack, &ack, 0);
    if(send_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_WRITE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   等待指定任务对指定命令的 ACK。
 * @param   expected_task_id: 期望回应的任务 ID。
 * @param   expected_cmd: 期望回应的命令。
 * @param   wait_ticks: 最大等待 Tick 数。
 * @retval  1: 收到匹配 ACK；0: 参数错误、队列未创建、超时或收到非匹配 ACK。
 * @note    队列中如果存在其他任务/命令的 ACK，会被丢弃并继续等待目标 ACK。
 *          当前 SystemTask 串行发送命令，因此正常情况下不会出现交错 ACK；
 *          这里保留匹配判断，便于后续扩展或异常日志定位。
 */
static uint8_t SystemTask_WaitTaskAck(AppTask_Id_t expected_task_id,
                                      AppTask_Cmd expected_cmd,
                                      TickType_t wait_ticks)
{
    AppTask_Ack_t ack;
    TickType_t start_tick;
    TickType_t elapsed_tick;
    TickType_t remain_tick;

    if((expected_task_id >= APP_TASK_ID_COUNT) || (expected_cmd == TASK_CMD_NONE))
    {
        return 0;
    }

    if(queuehandle_system_ack == NULL)
    {
        return 0;
    }

    start_tick = xTaskGetTickCount();
    while(1)
    {
        elapsed_tick = xTaskGetTickCount() - start_tick;
        if(elapsed_tick >= wait_ticks)
        {
            return 0;
        }

        remain_tick = wait_ticks - elapsed_tick;
        if(xQueueReceive(queuehandle_system_ack, &ack, remain_tick) != pdPASS)
        {
            return 0;
        }

        if((ack.task_id == expected_task_id) && (ack.cmd == expected_cmd))
        {
            return 1;
        }
    }
}

/*
 * @brief   发布 SystemTask 维护的系统运行态。
 * @note    SystemTask 只负责 mode/mode_valid/mode_changed/last_systemstatus。
 */
static void SystemTask_PublishRuntime(const SystemTask_Data_t *system_data)
{
    AppTask_Status_t task_status;

    if(system_data == 0)
    {
        return;
    }

    task_status = App_Task_ReleaseSystemTaskData(system_data);
    if(task_status != APP_TASK_OK)
    {
        App_LogPrintf("[SYSTEM] Release_system_task_data_fail:%s(%d)\r\n",
                      AppTaskStatusName(task_status),
                      task_status);
    }
}

/*
 * @brief   向目标任务发送控制命令并等待 ACK。
 * @param   task_handle: 目标任务句柄。
 * @param   task_id: 目标任务 ID，用于匹配 ACK。
 * @param   cmd: 待发送的 RUN/PAUSE 命令。
 * @retval  1: 命令发送并收到匹配 ACK；0: 发送失败或等待超时。
 * @note    SystemTask 通过任务通知向 ScanTask/ManualTask 下发命令；
 *          目标任务处理完命令后通过 ACK 队列回应。
 */
static uint8_t SystemTask_SendCommandAndWait(TaskHandle_t task_handle,
                                             AppTask_Id_t task_id,
                                             AppTask_Cmd cmd)
{
    if(task_handle == NULL)
    {
        App_LogPrintf("[SYSTEM] Send_cmd_fail:%s handle_null cmd=%d\r\n",
                      AppTaskIdName(task_id),
                      cmd);
        return 0;
    }

    if(xTaskNotify(task_handle, cmd, eSetValueWithOverwrite) != pdPASS)
    {
        App_LogPrintf("[SYSTEM] Send_cmd_fail:%s cmd=%d\r\n",
                      AppTaskIdName(task_id),
                      cmd);
        return 0;
    }

    if(SystemTask_WaitTaskAck(task_id, cmd, pdMS_TO_TICKS(SYSTEM_TASK_ACK_WAIT_MS)) == 0)
    {
        App_LogPrintf("[SYSTEM] Wait_ack_timeout:%s cmd=%d\r\n",
                      AppTaskIdName(task_id),
                      cmd);
        return 0;
    }

    return 1;
}


/* ******************************* UploadTask相关函数  ***************************** */
/*
 * @brief   发布 AppTask_UploadStatusSnapshot_t
 * @param   snapshot: 待发布的 AppTask_UploadStatusSnapshot_t 快照指针。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 快照发布成功。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 上传状态快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_WRITE: 队列写入失败。
 * @note    上传快照包含 System_UploadStatus_t 以及 TickType_t用于记录更新tick
 */
AppTask_Status_t App_Task_ReleaseUploadStatus(const AppTask_UploadStatusSnapshot_t *snapshot)
{
    BaseType_t overwrite_status;

    if(snapshot == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_upload_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    overwrite_status = xQueueOverwrite(queuehandle_upload_snapshot, snapshot);
    if(overwrite_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_WRITE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   构建 AppTask_UploadStatusSnapshot_t
 * @param   uploadstatus_snapshot: 用于保存上传状态快照指针。
 * @param   uploadstatus: 状态快照更新tick值
 * @retval  AppTask_UploadStatusSnapshot_t： 指向上传状态快照的指针
 * @note    构建更新AppTask_UploadStatusSnapshot_t，并返回指向该数据的指针
 */
static AppTask_UploadStatusSnapshot_t *App_Task_BuildUploadStatusSnapshot(AppTask_UploadStatusSnapshot_t *uploadstatus_snapshot, const System_UploadStatus_t *uploadstatus)
{
    uploadstatus_snapshot->upload_status = *uploadstatus;
    uploadstatus_snapshot->update_tick = xTaskGetTickCount();

    return uploadstatus_snapshot;
}

/*
 * @brief   等待并获取 UploadStatusSnapshot
 * @param   snapshot: 用于保存接收到的上传状态快照指针。
 * @param   wait_ticks: 等待队列数据的最大 Tick 数。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功读取快照。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 上传状态快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_READ: 等待超时或队列读取失败。
 */
AppTask_Status_t App_Task_PeekUploadStatus(AppTask_UploadStatusSnapshot_t *snapshot, TickType_t wait_ticks)
{
    BaseType_t peek_status;

    if(snapshot == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_upload_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    peek_status = xQueuePeek(queuehandle_upload_snapshot, snapshot, wait_ticks);
    if(peek_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_READ;
    }

    return APP_TASK_OK;
}

/* ******************************* I2C_mutex相关函数  ***************************** */
/*
 * @brief   等待获取I2C互斥锁
 * @param   wait_ticks: 等待互斥锁的最大 Tick 数。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功。
 *          - APP_TASK_ERROR_MUTEX_INIT: I2C互斥锁未创建。
 *          - APP_TASK_ERROR_MUTEX_TAKE: 等待超时或获取互斥锁失败。
 */
AppTask_Status_t App_Task_TakeI2C(TickType_t wait_ticks)
{
    BaseType_t take_status;

    if(mutexhandle_i2c == NULL)
    {
        return APP_TASK_ERROR_MUTEX_INIT;
    }

    take_status = xSemaphoreTake(mutexhandle_i2c, wait_ticks);
    if(take_status != pdTRUE)
    {
        return APP_TASK_ERROR_MUTEX_TAKE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   释放I2C互斥锁
 * @param   无
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功。
 *          - APP_TASK_ERROR_MUTEX_INIT: I2C互斥锁未创建。
 *          - APP_TASK_ERROR_MUTEX_GIVE: 释放互斥锁失败。
 */
AppTask_Status_t App_Task_GiveI2C(void)
{
    BaseType_t give_status;

    if(mutexhandle_i2c == NULL)
    {
        return APP_TASK_ERROR_MUTEX_INIT;
    }

    give_status = xSemaphoreGive(mutexhandle_i2c);
    if(give_status != pdTRUE)
    {
        return APP_TASK_ERROR_MUTEX_GIVE;
    }

    return APP_TASK_OK;
}

/* ******************************* ESP01_mutex相关函数  ***************************** */
/*
 * @brief   等待获取 ESP01 互斥锁。
 * @param   wait_ticks: 等待互斥锁的最大 Tick 数。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功。
 *          - APP_TASK_ERROR_MUTEX_INIT: ESP01互斥锁未创建。
 *          - APP_TASK_ERROR_MUTEX_TAKE: 等待超时或获取互斥锁失败。
 * @note    ESP01 上传和 OTA 下载共用 USART3 RingBuffer、AT Parser 和 ESP01 TCP 会话。
 *          获取该锁后，调用方应完成一段连续 AT 命令序列再释放，避免响应串台。
 */
AppTask_Status_t App_Task_TakeEsp01(TickType_t wait_ticks)
{
    BaseType_t take_status;

    if(mutexhandle_esp01 == NULL)
    {
        return APP_TASK_ERROR_MUTEX_INIT;
    }

    take_status = xSemaphoreTake(mutexhandle_esp01, wait_ticks);
    if(take_status != pdTRUE)
    {
        return APP_TASK_ERROR_MUTEX_TAKE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   释放 ESP01 互斥锁。
 * @param   无。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功。
 *          - APP_TASK_ERROR_MUTEX_INIT: ESP01互斥锁未创建。
 *          - APP_TASK_ERROR_MUTEX_GIVE: 释放互斥锁失败。
 * @note    只有已成功获取 ESP01 互斥锁的任务才能调用释放。
 */
AppTask_Status_t App_Task_GiveEsp01(void)
{
    BaseType_t give_status;

    if(mutexhandle_esp01 == NULL)
    {
        return APP_TASK_ERROR_MUTEX_INIT;
    }

    give_status = xSemaphoreGive(mutexhandle_esp01);
    if(give_status != pdTRUE)
    {
        return APP_TASK_ERROR_MUTEX_GIVE;
    }

    return APP_TASK_OK;
}

/* ******************************* OtaDownloadTask相关函数  ***************************** */
/*
 * @brief   请求 OtaDownloadTask 执行一次 OTA 下载。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 请求通知已发送。
 *          - APP_TASK_ERROR_PARAM: OtaDownloadTask 未启用或任务句柄尚未创建。
 *          - APP_TASK_ERROR_QUEUE_WRITE: FreeRTOS 任务通知发送失败。
 * @note    本函数只负责投递请求，不直接执行 HTTP 下载。
 *          服务器响应、串口命令或菜单入口都应调用该接口，再由 OtaDownloadTask
 *          统一检查 Config 状态、申请 ESP01 互斥锁并执行下载。
 */
AppTask_Status_t App_Task_RequestOtaDownload(const AppOtaDownload_Request_t *request)
{
    BaseType_t notify_status;

    if((App_Task_IsTaskEnabled(APP_TASK_ID_OTA_DOWNLOAD) == 0U) ||
       (ota_download_task_handle == NULL))
    {
        return APP_TASK_ERROR_PARAM;
    }

    taskENTER_CRITICAL();
    if(request != 0)
    {
        ota_download_pending_request = *request;
    }
    else
    {
        memset(&ota_download_pending_request, 0, sizeof(ota_download_pending_request));
    }
    taskEXIT_CRITICAL();

    notify_status = xTaskNotify(ota_download_task_handle,
                                OTA_DOWNLOAD_NOTIFY_REQUEST,
                                eSetBits);
    if(notify_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_WRITE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   取出最近一次 OTA 请求参数。
 * @param   request: 输出请求参数。
 * @retval  无。
 * @note    当前只保留最近一次请求。服务器重复下发 OTA 时，后到请求覆盖前一请求。
 */
static void OtaDownloadTask_CopyPendingRequest(AppOtaDownload_Request_t *request)
{
    if(request == 0)
    {
        return;
    }

    taskENTER_CRITICAL();
    *request = ota_download_pending_request;
    taskEXIT_CRITICAL();
}

/* ************************************* MonitorTask相关函数  *********************************** */
/*
 * @brief   任务心跳更新
 * @param   task_id：任务ID
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功。
 *          - APP_TASK_ERROR_PARAM: 参数错误。
 */
AppTask_Status_t App_Task_UpdateHeartbeat(AppTask_Id_t task_id)
{
    if(task_id >= APP_TASK_ID_COUNT)
    {
        return APP_TASK_ERROR_PARAM;
    }

    task_heartbeat_ticks[task_id] = xTaskGetTickCount();
    return APP_TASK_OK;
}

/*
 * @brief   获取对应任务心跳值
 * @param   task_id：任务ID
 * @param   heartbeat_tick：返回心跳值的指针
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功。
 *          - APP_TASK_ERROR_PARAM: 参数错误。
 */
static AppTask_Status_t MonitorTask_GetHeartbeatTick(AppTask_Id_t task_id, TickType_t *heartbeat_tick)
{
    if(task_id >= APP_TASK_ID_COUNT || heartbeat_tick == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    *heartbeat_tick = task_heartbeat_ticks[task_id];
    return APP_TASK_OK;
}

/*
 * @brief   系统运行资源打印
 * @param   task_id：任务ID。
 * @param   task_age: 运行间隔。
 * @retval  无。
 */
static void MonitorTask_LogResourceIfDue(TickType_t *last_resource_log_tick, AppTask_MonitorItem_t *monitor_item)
{
    TickType_t now_tick;
    AppTask_Id_t task_id;

    now_tick = xTaskGetTickCount();
    if((now_tick - *last_resource_log_tick) >= pdMS_TO_TICKS(MONITOR_RESOURCE_LOG_PERIOD_MS))
    {
        App_LogPrintf("[RTOS] ");

        for(task_id = 0; task_id < APP_TASK_ID_COUNT; task_id++)
        {
            if(App_Task_IsTaskEnabled(task_id))
            {
                App_LogPrintf("%s_stack=%lu words, ", AppTaskIdName(task_id),(unsigned long)uxTaskGetStackHighWaterMark(monitor_item[task_id].task_handle));
            }
        }

        App_LogPrintf("heap_free=%lu bytes, heap_min=%lu bytes\r\n",
                      (unsigned long)xPortGetFreeHeapSize(),
                      (unsigned long)xPortGetMinimumEverFreeHeapSize());

        *last_resource_log_tick = now_tick;
    }
}

/*
 * @brief   更新单个任务监控项。
 * @param   item：任务监控项。
 * @retval  无。
 * @note    获取任务上一次heartbeat失败时，更新任务状态为Stale并返回。
 *
 */
static void MonitorTask_UpdateItem(AppTask_MonitorItem_t *item)
{
    AppTask_Status_t task_status;
    TickType_t nowtick;

    if(item == 0)
    {
        return;
    }

    task_status = MonitorTask_GetHeartbeatTick(item->task_id, &item->last_tick);
    if(task_status != APP_TASK_OK)
    {
        App_LogPrintf("[Monitor] Get_%s_heartbeattick_fail:%s(%d)\r\n", AppTaskIdName(item->task_id), AppTaskStatusName(task_status), task_status);
        item->health = APP_TASK_HEALTH_STALE;
        return;
    }

    nowtick = xTaskGetTickCount();
    item->age_tick = nowtick - item->last_tick;
    if(item->age_tick >= item->timeout_tick)
    {
        item->health = APP_TASK_HEALTH_STALE;
    }
    else
    {
        item->health =APP_TASK_HEALTH_ALIVE;
    }
}

/*
 * @brief   任务状态打印。
 * @param   item：任务监控项。
 * @retval  无。
 * @note    打印信息：
 *          [MONITOR] {任务名} {任务状态} age={任务更新间隔} ms
 *
 */
static void MonitorTask_LogItem(const AppTask_MonitorItem_t *item)
{
    if(item == 0)
    {
        return;
    }

    App_LogPrintf("[MONITOR] %s %s age=%lu ms\r\n",
                AppTaskIdName(item->task_id),
                MonitorTask_HealthName(item->health),
                (unsigned long)item->age_tick);
}

/*
 * @brief   构建 AppTask_MonitorSnapshot_t
 * @param   monitor_snapshot: 用于保存系统任务监视数据快照指针。
 * @param   items: 系统任务监视数据指针。
 * @retval  无。
 * @note    仅填充调用方传入的快照缓冲区；快照内存由 MonitorTask 持有。
 */
static void App_Task_BuildMonitorSnapshot(AppTask_MonitorSnapshot_t *monitor_snapshot, const AppTask_MonitorItem_t *items)
{
    uint8_t i;

    for(i = 0; i < APP_TASK_ID_COUNT; i++)
    {
        monitor_snapshot->items[i] = items[i];
    }
    monitor_snapshot->update_tick = xTaskGetTickCount();

}

/*
 * @brief   根据任务健康状态决定是否喂 IWDG。
 * @param   items: 系统任务监视数据指针。
 * @retval  无。
 * @note    IWDG 只允许由 MonitorTask 集中喂狗。
 *          任一任务进入 STALE 状态时停止喂狗，让 IWDG 负责最终复位恢复；
 *          全部任务 ALIVE 时才刷新计数器，避免业务任务各自无条件喂狗掩盖卡死。
 */
static void MonitorTask_FeedIWDG(const AppTask_MonitorItem_t *items)
{
    uint8_t task_id;
    uint8_t any_task_stale = 0;

    for(task_id=0; task_id<APP_TASK_ID_COUNT ; task_id++)
    {
        if(App_Task_IsTaskEnabled((AppTask_Id_t)task_id) == 0U)
        {
            continue;
        }

        if(items[task_id].health == APP_TASK_HEALTH_STALE)
        {
            any_task_stale = 1;
            App_LogPrintf("[MONITOR] %s_is_stale,age:%lu, timeout_tick:%lu\r\n",
                          AppTaskIdName(task_id),
                          (unsigned long)items[task_id].age_tick,
                          (unsigned long)items[task_id].timeout_tick);
        }
    }

    if(any_task_stale == 0)
    {
        BSP_IWDG_Feed();
        App_LogPrintf("[MONITOR] Feed_IWDG_once\r\n");
    }
}

/*
 * @brief   发布 AppTask_MonitorSnapshot_t
 * @param   snapshot: 待发布的 AppTask_MonitorSnapshot_t 快照指针。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 快照发布成功。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 系统任务监视快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_WRITE: 队列写入失败。
 * @note    队列只保存 MonitorTask 双缓冲中的快照指针，消费者只读且不释放。
 */
AppTask_Status_t App_Task_ReleaseMonitorSnapshot(const AppTask_MonitorSnapshot_t *snapshot)
{
    BaseType_t overwrite_status;

    if(snapshot == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_monitor_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    overwrite_status = xQueueOverwrite(queuehandle_monitor_snapshot, &snapshot);
    if(overwrite_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_WRITE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   非破坏性读取最新任务监控快照指针。
 * @param   snapshot_ptr: 用于保存 MonitorTask 快照指针的二级指针。
 * @param   wait_ticks: 等待队列数据的最大 Tick 数。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功读取快照。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 任务监控快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_READ: 等待超时或队列读取失败。
 */
AppTask_Status_t App_Task_PeekMonitorSnapshot(const AppTask_MonitorSnapshot_t **snapshot_ptr, TickType_t wait_ticks)
{
    BaseType_t peek_status;
    const AppTask_MonitorSnapshot_t *snapshot_ptr_temp;

    if(snapshot_ptr == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_monitor_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    peek_status = xQueuePeek(queuehandle_monitor_snapshot, &snapshot_ptr_temp, wait_ticks);
    if(peek_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_READ;
    }

    *snapshot_ptr = snapshot_ptr_temp;

    return APP_TASK_OK;
}

/* ************************************* ScanTask相关函数  *********************************** */
/*
 * @brief   发布扫描数据快照。
 * @param   snapshot: 待发布的 AppTask_ScanSnapshot_t 快照指针。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 快照发布成功。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 扫描快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_WRITE: 队列写入失败。
 * @note    队列只保存 ScanTask 双缓冲中的快照指针，消费者只读且不释放。
 */
AppTask_Status_t App_Task_ReleaseScanSnapshot(const AppTask_ScanSnapshot_t *snapshot)
{
    BaseType_t overwrite_status;

    if(snapshot == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_scan_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    overwrite_status = xQueueOverwrite(queuehandle_scan_snapshot, &snapshot);
    if(overwrite_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_WRITE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   非破坏性读取最新扫描数据快照指针。
 * @param   snapshot_ptr: 用于保存 ScanTask 快照指针的二级指针。
 * @param   wait_ticks: 等待队列数据的最大 Tick 数。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功读取快照。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 扫描快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_READ: 等待超时或队列读取失败。
 * @note    使用 xQueuePeek() 读取生产者持有的最新快照指针，不移除队列元素；
 *          返回指针只在本轮读取中使用，消费者不得修改或释放。
 */
AppTask_Status_t App_Task_PeekScanSnapshot(const AppTask_ScanSnapshot_t **snapshot_ptr, TickType_t wait_ticks)
{
    BaseType_t peek_status;
    const AppTask_ScanSnapshot_t *snapshot_ptr_temp;

    if(snapshot_ptr == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_scan_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    peek_status = xQueuePeek(queuehandle_scan_snapshot, &snapshot_ptr_temp, wait_ticks);
    if(peek_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_READ;
    }

    *snapshot_ptr = snapshot_ptr_temp;

    return APP_TASK_OK;
}

static void ScanTask_BuildScanSnapshot(AppTask_ScanSnapshot_t *scan_snapshot, const System_ScanTaskData_t *scan_task_data)
{
    scan_snapshot->scan = *scan_task_data;
    scan_snapshot->update_tick = xTaskGetTickCount();
}

/* ************************************* EnvTask相关函数  *********************************** */
/*
 * @brief   发布环境数据快照。
 * @param   snapshot: 待发布的 AppTask_EnvSnapshot_t 快照指针。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 快照发布成功。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 环境快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_WRITE: 队列写入失败。
 * @note    使用 xQueueOverwrite()，队列中始终只保留最新一帧环境快照。
 */
AppTask_Status_t App_Task_ReleaseEnvSnapshot(const AppTask_EnvSnapshot_t *snapshot)
{
    BaseType_t overwrite_status;

    if(snapshot == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_env_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    overwrite_status = xQueueOverwrite(queuehandle_env_snapshot, snapshot);
    if(overwrite_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_WRITE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   非破坏性读取最新环境数据快照。
 * @param   snapshot: 用于保存读取结果的 AppTask_EnvSnapshot_t 指针。
 * @param   wait_ticks: 等待队列数据的最大 Tick 数。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功读取快照。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 环境快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_READ: 等待超时或队列读取失败。
 * @note    使用 xQueuePeek()，读取后不移除队列中的快照，适合 DisplayTask
 *          和 UploadTask 读取最新环境状态。
 */
AppTask_Status_t App_Task_PeekEnvSnapshot(AppTask_EnvSnapshot_t *snapshot, TickType_t wait_ticks)
{
    BaseType_t peek_status;

    if(snapshot == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_env_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    peek_status = xQueuePeek(queuehandle_env_snapshot, snapshot, wait_ticks);
    if(peek_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_READ;
    }

    return APP_TASK_OK;
}

/* ************************************ InputTask相关函数 *********************************** */
/*
 * @brief   发布输入数据快照。
 * @param   snapshot: 待发布的 AppTask_InputSnapshot_t 快照指针。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 快照发布成功。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 输入快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_WRITE: 队列写入失败。
 * @note    使用 xQueueOverwrite()，队列中始终只保留最新一帧输入快照。
 *          输入快照同时保存 encoder_delta 和 encoder_total；消费者应优先使用
 *          encoder_total 差值恢复完整旋转输入，避免高频采样时 delta 被快照覆盖。
 *          按键点击通过 task notification 通知 SystemTask，避免边沿事件丢失。
 */
AppTask_Status_t App_Task_ReleaseInputSnapshot(const AppTask_InputSnapshot_t *snapshot)
{
    BaseType_t overwrite_status;

    if(snapshot == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_input_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    overwrite_status = xQueueOverwrite(queuehandle_input_snapshot, snapshot);
    if(overwrite_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_WRITE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   非破坏性读取最新输入数据快照。
 * @param   snapshot: 用于保存读取结果的 AppTask_InputSnapshot_t 指针。
 * @param   wait_ticks: 等待队列数据的最大 Tick 数。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功读取快照。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 输入快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_READ: 等待超时或队列读取失败。
 * @note    使用 xQueuePeek()，读取后不移除队列中的快照，适合 SystemTask、
 *          DisplayTask 和后续 ManualTask 读取最新输入状态。旋转控制类消费者
 *          需要保存各自上次消费的 encoder_total。
 */
AppTask_Status_t App_Task_PeekInputSnapshot(AppTask_InputSnapshot_t *snapshot, TickType_t wait_ticks)
{
    BaseType_t peek_status;

    if(snapshot == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_input_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    peek_status = xQueuePeek(queuehandle_input_snapshot, snapshot, wait_ticks);
    if(peek_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_READ;
    }

    return APP_TASK_OK;
}

/* ************************************ ManualTask相关函数 *********************************** */
/*
 * @brief   发布手动控制数据快照。
 * @param   snapshot: 待发布的 AppTask_ManualSnapshot_t 快照指针。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 快照发布成功。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 手动控制快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_WRITE: 队列写入失败。
 * @note    队列只保存 ManualTask 双缓冲中的快照指针，消费者只读且不释放。
 */
AppTask_Status_t App_Task_ReleaseManualSnapshot(const AppTask_ManualSnapshot_t *snapshot_ptr)
{
    BaseType_t overwrite_status;

    if(snapshot_ptr == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_manual_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    overwrite_status = xQueueOverwrite(queuehandle_manual_snapshot, &snapshot_ptr);
    if(overwrite_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_WRITE;
    }

    return APP_TASK_OK;
}

/*
 * @brief   非破坏性读取最新手动控制数据快照指针。
 * @param   snapshot_ptr: 用于保存 ManualTask 快照指针的二级指针。
 * @param   wait_ticks: 等待队列数据的最大 Tick 数。
 * @retval  AppTask_Status_t:
 *          - APP_TASK_OK: 成功读取快照。
 *          - APP_TASK_ERROR_PARAM: 参数为空。
 *          - APP_TASK_ERROR_QUEUE_INIT: 手动控制快照队列尚未创建。
 *          - APP_TASK_ERROR_QUEUE_READ: 等待超时或队列读取失败。
 * @note    使用 xQueuePeek() 读取生产者持有的最新快照指针，不移除队列元素；
 *          返回指针只在本轮读取中使用，消费者不得修改或释放。
 */
AppTask_Status_t App_Task_PeekManualSnapshot(const AppTask_ManualSnapshot_t **snapshot_ptr, TickType_t wait_ticks)
{
    BaseType_t peek_status;
    const AppTask_ManualSnapshot_t *snapshot_ptr_temp;

    if(snapshot_ptr == 0)
    {
        return APP_TASK_ERROR_PARAM;
    }

    if(queuehandle_manual_snapshot == NULL)
    {
        return APP_TASK_ERROR_QUEUE_INIT;
    }

    peek_status = xQueuePeek(queuehandle_manual_snapshot, &snapshot_ptr_temp, wait_ticks);
    if(peek_status != pdPASS)
    {
        return APP_TASK_ERROR_QUEUE_READ;
    }

    *snapshot_ptr = snapshot_ptr_temp;

    return APP_TASK_OK;
}


/* ************************************ 任务函数 ************************************** */
/*
 * @brief   上传任务函数。
 * @param   argument: FreeRTOS 任务参数，当前未使用。
 * @retval  无。
 * @note    UploadTask 独立维护 WiFi/TCP 连接状态，阻塞等待 SystemTask 发布的运行态快照。
 *          发送前读取 EnvSnapshot 和当前模式快照，Scan/Manual 指针会先复制到本任务静态副本。
 *          上传失败后标记连接状态，并在任务内部执行退避重连，避免网络异常阻塞 SystemTask。
 *          所有 ESP01 操作通过 App_Task_TakeEsp01()/App_Task_GiveEsp01() 串行化，
 *          避免与 OtaDownloadTask 的 AT 命令和 +IPD 数据接收互相干扰。
 */
void UploadTask(void *argument)
{
    App_Task_UpdateHeartbeat(APP_TASK_ID_UPLOAD);
    (void)argument;

    AppUpload_Status_t upload_status;
    AppTask_Status_t task_status;
    AppTask_Status_t scan_snapshot_status;
    AppTask_Status_t env_snapshot_status;
    AppTask_Status_t manual_snapshot_status;
    AppTask_Status_t esp01_mutex_status;
    AppTask_Status_t ota_request_status;
    AppOtaDownload_Request_t ota_request;

    System_UploadStatus_t system_upload_status;
    AppUpload_ServerCommand_t upload_server_command;

    const AppTask_ManualSnapshot_t *manual_snapshot_ptr;
    const AppTask_ScanSnapshot_t *scan_snapshot_ptr;

    SystemTask_Data_t system_data_snapshot;
    AppUpload_Data_t upload_data;
    AppTask_EnvSnapshot_t env_snapshot;
    AppTask_UploadStatusSnapshot_t upload_status_snapshot;
    AppOtaDownload_Report_t upload_ota_report;
    static System_ManualTaskData_t upload_manual_copy;
    static System_ScanTaskData_t upload_scan_copy;
    uint16_t reconnect_time = UPLOAD_RECONNECT_INTERVAL_MS;
    uint8_t upload_payload_ready;
    uint8_t upload_network_backoff = 0;

    system_upload_status.wifi_connected = UPLOAD_STATUS_FAIL;
    system_upload_status.tcp_connected = UPLOAD_STATUS_FAIL;
    system_upload_status.last_upload_status = UPLOAD_STATUS_FAIL;
    system_upload_status.upload_count = 0;
    system_upload_status.upload_fail_count = 0;

    esp01_mutex_status = App_Task_TakeEsp01(pdMS_TO_TICKS(UPLOAD_ESP01_MUTEX_WAIT_MS));
    if(esp01_mutex_status == APP_TASK_OK)
    {
        upload_status = App_Upload_Init(&system_upload_status);
        esp01_mutex_status = App_Task_GiveEsp01();
        if(esp01_mutex_status != APP_TASK_OK)
        {
            App_LogPrintf("[UPLOAD] Give_esp01_mutex_init_fail:%s(%d)\r\n",
                          AppTaskStatusName(esp01_mutex_status),
                          esp01_mutex_status);
        }
    }
    else
    {
        upload_status = APP_UPLOAD_ERROR_INIT;
        App_LogPrintf("[UPLOAD] Take_esp01_mutex_init_fail:%s(%d)\r\n",
                      AppTaskStatusName(esp01_mutex_status),
                      esp01_mutex_status);
    }
    App_LogPrintf("[UPLOAD] Init_status:%s(%d)\r\n",
                  AppUploadStatusName(upload_status),
                  upload_status);
    task_status = App_Task_ReleaseUploadStatus(App_Task_BuildUploadStatusSnapshot(&upload_status_snapshot, &system_upload_status));
    if(task_status != APP_TASK_OK)
    {
        App_LogPrintf("[UPLOAD] Release_init_status:%s(%d)\r\n",
                  AppTaskStatusName(task_status),
                  task_status);
    }

    while(upload_status == APP_UPLOAD_ERROR_INIT)
    {
        App_Task_UpdateHeartbeat(APP_TASK_ID_UPLOAD);
        esp01_mutex_status = App_Task_TakeEsp01(pdMS_TO_TICKS(UPLOAD_ESP01_MUTEX_WAIT_MS));
        if(esp01_mutex_status == APP_TASK_OK)
        {
            upload_status = App_Upload_Init(&system_upload_status);
            esp01_mutex_status = App_Task_GiveEsp01();
            if(esp01_mutex_status != APP_TASK_OK)
            {
                App_LogPrintf("[UPLOAD] Give_esp01_mutex_retry_init_fail:%s(%d)\r\n",
                              AppTaskStatusName(esp01_mutex_status),
                              esp01_mutex_status);
            }
        }
        else
        {
            upload_status = APP_UPLOAD_ERROR_INIT;
            App_LogPrintf("[UPLOAD] Take_esp01_mutex_retry_init_fail:%s(%d)\r\n",
                          AppTaskStatusName(esp01_mutex_status),
                          esp01_mutex_status);
        }
        App_LogPrintf("[UPLOAD] Retry_init_status:%s(%d)\r\n",
                      AppUploadStatusName(upload_status),
                      upload_status);
        task_status = App_Task_ReleaseUploadStatus(App_Task_BuildUploadStatusSnapshot(&upload_status_snapshot, &system_upload_status));
        if(task_status != APP_TASK_OK)
        {
            App_LogPrintf("[UPLOAD] Release_retry_init_status:%s(%d)\r\n",
                    AppTaskStatusName(task_status),
                    task_status);
        }

        App_LogPrintf("[POWER] upload_backoff reason=init retry=%lu ms\r\n",
                      (unsigned long)reconnect_time);
        vTaskDelay(pdMS_TO_TICKS(reconnect_time));
        if(reconnect_time < UPLOAD_RECONNECT_MAX_INTERVAL_MS)
        {
            reconnect_time *= 2;
            if(reconnect_time > UPLOAD_RECONNECT_MAX_INTERVAL_MS)
            {
                reconnect_time = UPLOAD_RECONNECT_MAX_INTERVAL_MS;
            }
        }
    }

    reconnect_time = UPLOAD_RECONNECT_INTERVAL_MS;
    while(1)
    {
        App_Task_UpdateHeartbeat(APP_TASK_ID_UPLOAD);

        if(system_upload_status.wifi_connected == UPLOAD_STATUS_FAIL || system_upload_status.tcp_connected == UPLOAD_STATUS_FAIL)
        {
            /*
             * 网络连接异常只让 UploadTask 自己进入退避重连。
             * 心跳仍持续更新，避免 ESP01 网络问题被误判为系统卡死。
             */
            if(upload_network_backoff == 0)
            {
                upload_network_backoff = 1;
                App_LogPrintf("[POWER] upload_backoff wifi=%s(%d) tcp=%s(%d) retry=%lu ms\r\n",
                              UploadTask_ConnectionStatusName(system_upload_status.wifi_connected),
                              system_upload_status.wifi_connected,
                              UploadTask_ConnectionStatusName(system_upload_status.tcp_connected),
                              system_upload_status.tcp_connected,
                              (unsigned long)reconnect_time);
            }
            else
            {
                App_LogPrintf("[POWER] upload_backoff_retry wifi=%s(%d) tcp=%s(%d) retry=%lu ms\r\n",
                              UploadTask_ConnectionStatusName(system_upload_status.wifi_connected),
                              system_upload_status.wifi_connected,
                              UploadTask_ConnectionStatusName(system_upload_status.tcp_connected),
                              system_upload_status.tcp_connected,
                              (unsigned long)reconnect_time);
            }
            task_status = App_Task_ReleaseUploadStatus(App_Task_BuildUploadStatusSnapshot(&upload_status_snapshot, &system_upload_status));
            if(task_status != APP_TASK_OK)
            {
                App_LogPrintf("[UPLOAD] Release_before_reconnect_status:%s(%d)\r\n",
                        AppTaskStatusName(task_status),
                        task_status);
            }

            esp01_mutex_status = App_Task_TakeEsp01(pdMS_TO_TICKS(UPLOAD_ESP01_MUTEX_WAIT_MS));
            if(esp01_mutex_status == APP_TASK_OK)
            {
                upload_status = App_Upload_Reconnect(&system_upload_status);
                esp01_mutex_status = App_Task_GiveEsp01();
                if(esp01_mutex_status != APP_TASK_OK)
                {
                    App_LogPrintf("[UPLOAD] Give_esp01_mutex_reconnect_fail:%s(%d)\r\n",
                                  AppTaskStatusName(esp01_mutex_status),
                                  esp01_mutex_status);
                }
            }
            else
            {
                upload_status = APP_UPLOAD_ERROR_ESP01_BUSY;
                App_LogPrintf("[UPLOAD] reconnect skipped: ESP01 busy by OTA, mutex=%s(%d)\r\n",
                              AppTaskStatusName(esp01_mutex_status),
                              esp01_mutex_status);
            }
            App_LogPrintf("[UPLOAD] Retry_connect_status:%s(%d)\r\n",
                          AppUploadStatusName(upload_status),
                          upload_status);

            task_status = App_Task_ReleaseUploadStatus(App_Task_BuildUploadStatusSnapshot(&upload_status_snapshot, &system_upload_status));
            if(task_status != APP_TASK_OK)
            {
                App_LogPrintf("[UPLOAD] Release_after_reconnect_status:%s(%d)\r\n",
                        AppTaskStatusName(task_status),
                        task_status);
            }

            if(upload_status == APP_UPLOAD_OK)
            {
                upload_network_backoff = 0;
                reconnect_time = UPLOAD_RECONNECT_INTERVAL_MS;
                App_LogPrintf("[POWER] upload_active retry=%lu ms\r\n",
                              (unsigned long)UPLOAD_RECONNECT_INTERVAL_MS);
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(reconnect_time));
                if(reconnect_time < UPLOAD_RECONNECT_MAX_INTERVAL_MS)
                {
                    reconnect_time *= 2;
                    if(reconnect_time > UPLOAD_RECONNECT_MAX_INTERVAL_MS)
                    {
                        reconnect_time = UPLOAD_RECONNECT_MAX_INTERVAL_MS;
                    }
                }
            }
        }

        // wifi、tcp连接正常，进行数据上传
        else
        {
            reconnect_time = UPLOAD_RECONNECT_INTERVAL_MS;
            task_status = App_Task_WaitSystemTaskData(&system_data_snapshot, portMAX_DELAY);
            if(task_status == APP_TASK_OK)
            {
                memset(&upload_data, 0, sizeof(upload_data));
                upload_data.runtime = system_data_snapshot.runtime;
                AppOtaDownload_GetReport(&upload_ota_report);
                upload_data.ota_report = &upload_ota_report;
                manual_snapshot_ptr = 0;
                scan_snapshot_ptr = 0;
                upload_payload_ready = 0;

                env_snapshot_status = App_Task_PeekEnvSnapshot(&env_snapshot, 0);
                if(env_snapshot_status == APP_TASK_OK)
                {
                    upload_data.env = env_snapshot.env;
                }
                else if(env_snapshot_status != APP_TASK_ERROR_QUEUE_READ)
                {
                    App_LogPrintf("[UPLOAD] Peek_env_snapshot_fail:%s(%d)\r\n",
                                  AppTaskStatusName(env_snapshot_status),
                                  env_snapshot_status);
                }

                if(system_data_snapshot.runtime.mode == SYSTEM_MODE_MANUAL_SERVO)
                {
                    manual_snapshot_status = App_Task_PeekManualSnapshot(&manual_snapshot_ptr, 0);
                    if((manual_snapshot_status == APP_TASK_OK) && (manual_snapshot_ptr != 0))
                    {
                        // 复制到上传本地副本，保证本次协议打包期间数据稳定。
                        taskENTER_CRITICAL();
                        upload_manual_copy = manual_snapshot_ptr->manual_data;
                        taskEXIT_CRITICAL();
                        upload_data.manual = &upload_manual_copy;
                        upload_payload_ready = 1;
                    }
                    else if(manual_snapshot_status != APP_TASK_ERROR_QUEUE_READ)
                    {
                        App_LogPrintf("[UPLOAD] Peek_manual_snapshot_fail:%s(%d)\r\n",
                                      AppTaskStatusName(manual_snapshot_status),
                                      manual_snapshot_status);
                    }
                }
                else if(system_data_snapshot.runtime.mode == SYSTEM_MODE_AUTO_SCAN)
                {
                    scan_snapshot_status = App_Task_PeekScanSnapshot(&scan_snapshot_ptr, 0);
                    if((scan_snapshot_status == APP_TASK_OK) && (scan_snapshot_ptr != 0))
                    {
                        // 复制到上传本地副本，保证本次协议打包期间数据稳定。
                        taskENTER_CRITICAL();
                        upload_scan_copy = scan_snapshot_ptr->scan;
                        taskEXIT_CRITICAL();
                        upload_data.scan = &upload_scan_copy;
                        upload_payload_ready = 1;
                    }
                    else if(scan_snapshot_status != APP_TASK_ERROR_QUEUE_READ)
                    {
                        App_LogPrintf("[UPLOAD] Peek_scan_snapshot_fail:%s(%d)\r\n",
                                      AppTaskStatusName(scan_snapshot_status),
                                      scan_snapshot_status);
                    }
                }
                else
                {
                    App_LogPrintf("[UPLOAD] Skip_unknown_mode:%d\r\n",
                                  system_data_snapshot.runtime.mode);
                }

                if(upload_payload_ready)
                {
                    memset(&upload_server_command, 0, sizeof(upload_server_command));
                    upload_server_command.type = APP_UPLOAD_SERVER_CMD_NONE;
                    esp01_mutex_status = App_Task_TakeEsp01(pdMS_TO_TICKS(UPLOAD_ESP01_MUTEX_WAIT_MS));
                    if(esp01_mutex_status == APP_TASK_OK)
                    {
                        upload_status = App_Upload_Send(&upload_data,
                                                        &system_upload_status,
                                                        &upload_server_command);
                        esp01_mutex_status = App_Task_GiveEsp01();
                        if(esp01_mutex_status != APP_TASK_OK)
                        {
                            App_LogPrintf("[UPLOAD] Give_esp01_mutex_send_fail:%s(%d)\r\n",
                                          AppTaskStatusName(esp01_mutex_status),
                                          esp01_mutex_status);
                        }
                    }
                    else
                    {
                        upload_status = APP_UPLOAD_ERROR_ESP01_BUSY;
                        system_upload_status.last_upload_status = UPLOAD_STATUS_FAIL;
                        App_LogPrintf("[UPLOAD] send skipped: ESP01 busy by OTA, mutex=%s(%d)\r\n",
                                      AppTaskStatusName(esp01_mutex_status),
                                      esp01_mutex_status);
                    }
                    App_LogPrintf("[UPLOAD] Send_status:%s(%d)\r\n",
                                  AppUploadStatusName(upload_status),
                                  upload_status);

                    if((upload_status == APP_UPLOAD_OK) &&
                       (upload_server_command.type == APP_UPLOAD_SERVER_CMD_OTA_REQUEST))
                    {
                        memset(&ota_request, 0, sizeof(ota_request));
                        if(upload_server_command.has_ota_path != 0U)
                        {
                            (void)snprintf(ota_request.http_path,
                                           sizeof(ota_request.http_path),
                                           "%s",
                                           upload_server_command.ota_path);
                            ota_request.has_http_path = 1U;
                        }
                        if(upload_server_command.has_ota_version != 0U)
                        {
                            ota_request.has_expected_version = 1U;
                            ota_request.expected_version = upload_server_command.ota_version;
                        }

                        ota_request_status = App_Task_RequestOtaDownload(&ota_request);
                        if(ota_request.has_http_path && ota_request.has_expected_version)
                        {
                            App_LogPrintf("[UPLOAD] ota_request:%s(%d) path=%s ver=%lu\r\n",
                                          AppTaskStatusName(ota_request_status),
                                          ota_request_status,
                                          ota_request.http_path,
                                          (unsigned long)ota_request.expected_version);
                        }
                        else if(ota_request.has_http_path)
                        {
                            App_LogPrintf("[UPLOAD] ota_request:%s(%d) path=%s ver=NONE\r\n",
                                          AppTaskStatusName(ota_request_status),
                                          ota_request_status,
                                          ota_request.http_path);
                        }
                        else if(ota_request.has_expected_version)
                        {
                            App_LogPrintf("[UPLOAD] ota_request:%s(%d) path=DEFAULT ver=%lu\r\n",
                                          AppTaskStatusName(ota_request_status),
                                          ota_request_status,
                                          (unsigned long)ota_request.expected_version);
                        }
                        else
                        {
                            App_LogPrintf("[UPLOAD] ota_request:%s(%d) path=DEFAULT ver=NONE\r\n",
                                          AppTaskStatusName(ota_request_status),
                                          ota_request_status);
                        }
                    }
                }
                task_status = App_Task_ReleaseUploadStatus(App_Task_BuildUploadStatusSnapshot(&upload_status_snapshot, &system_upload_status));
                if(task_status != APP_TASK_OK)
                {
                    App_LogPrintf("[UPLOAD] Release_upload_status:%s(%d)\r\n",
                    AppTaskStatusName(task_status),
                    task_status);
                }
            }
            else
            {
                App_LogPrintf("[UPLOAD] Wait_systemdata_status:%s(%d)\r\n",
                              AppTaskStatusName(task_status),
                              task_status);
            }

            vTaskDelay(pdMS_TO_TICKS(UPLOAD_TASK_RUN_INTERVAL_MS));
        }
    }
}

/*
 * @brief   ESP01 HTTP OTA 下载任务。
 * @param   argument: 当前未使用。
 * @retval  无。
 * @note    任务默认等待 App_Task_RequestOtaDownload() 投递通知。
 *          收到请求后先确认 Config 处于 CONFIRMED 稳定态，再独占 ESP01 执行下载。
 *          TESTING 验证启动只允许 MonitorTask 完成确认，不允许链式触发下一轮 OTA。
 */
void OtaDownloadTask(void *argument)
{
    AppOtaDownload_Status_t ota_status;
    AppTask_Status_t esp01_mutex_status;
    UpgradeConfig_t ota_start_cfg;
    AppOtaDownload_Request_t ota_request;
    UpgradeConfigIfErrorCode_t ota_cfg_status;
    BaseType_t notify_wait_status;
    uint32_t notify_value;

    (void)argument;
    App_Task_UpdateHeartbeat(APP_TASK_ID_OTA_DOWNLOAD);

    App_LogPrintf("[OTA-DL] wait request\r\n");

    while(1)
    {
        App_Task_UpdateHeartbeat(APP_TASK_ID_OTA_DOWNLOAD);
        notify_wait_status = xTaskNotifyWait(0,
                                             OTA_DOWNLOAD_NOTIFY_ALL,
                                             &notify_value,
                                             pdMS_TO_TICKS(MONITOR_TASK_RUN_INTERVAL_MS));
        if((notify_wait_status == pdTRUE) &&
           ((notify_value & OTA_DOWNLOAD_NOTIFY_REQUEST) != 0U))
        {
            OtaDownloadTask_CopyPendingRequest(&ota_request);
            AppOtaDownload_ReportStart(&ota_request);

            ota_cfg_status = UpgradeConfig_Load(&ota_start_cfg);
            if(ota_cfg_status != UpgradeConfigIf_OK)
            {
                App_LogPrintf("[OTA-DL] request skip cfg=%s\r\n",
                              UpgradeConfigIfErrorCode_String(ota_cfg_status));
                AppOtaDownload_ReportFinish(APP_OTA_DOWNLOAD_ERR_CONFIG);
                continue;
            }

            if(ota_start_cfg.state != UPGRADE_STATE_CONFIRMED)
            {
                App_LogPrintf("[OTA-DL] request skip boot_state=%s\r\n",
                              UpgradState_String(ota_start_cfg.state));
                AppOtaDownload_ReportFinish(APP_OTA_DOWNLOAD_ERR_STATE);
                continue;
            }

            App_LogPrintf("[OTA-DL] request accepted\r\n");
            esp01_mutex_status = App_Task_TakeEsp01(portMAX_DELAY);
            if(esp01_mutex_status == APP_TASK_OK)
            {
                ota_status = AppOtaDownload_RunOnce(&ota_request);
                esp01_mutex_status = App_Task_GiveEsp01();
                if(esp01_mutex_status != APP_TASK_OK)
                {
                    App_LogPrintf("[OTA-DL] Give_esp01_mutex_fail:%s(%d)\r\n",
                                  AppTaskStatusName(esp01_mutex_status),
                                  esp01_mutex_status);
                }
            }
            else
            {
                ota_status = APP_OTA_DOWNLOAD_ERR_ESP01;
                App_LogPrintf("[OTA-DL] Take_esp01_mutex_fail:%s(%d)\r\n",
                              AppTaskStatusName(esp01_mutex_status),
                              esp01_mutex_status);
            }
            AppOtaDownload_ReportFinish(ota_status);
            App_LogPrintf("[OTA-DL] task result=%s(%d)\r\n",
                          AppOtaDownload_StatusName(ota_status),
                          ota_status);
        }
    }
}

/*
 * @brief   显示任务函数。
 * @note    DisplayTask 负责 OLED 初始化与页面刷新。
 *          通过 SystemTask_Data_t 状态队列读取当前运行模式和 mode_changed。
 *          输入、上传、监控、扫描、手动数据均由 DisplayTask 直接读取对应任务快照。
 *          通过 upload status 队列非阻塞读取上传状态。
 *          所有 OLED I2C 访问必须在 I2C mutex 保护下执行。
 */
void DisplayTask(void *argument)
{
    App_Task_UpdateHeartbeat(APP_TASK_ID_DISPLAY);
    (void)argument;
    SystemTask_Data_t system_data_snapshot;
    const AppTask_MonitorSnapshot_t *monitor_snapshot_ptr;
    const AppTask_ManualSnapshot_t *manual_snapshot_ptr;
    const AppTask_ScanSnapshot_t *scan_snapshot_ptr;


    AppTask_InputSnapshot_t input_snapshot;
    AppTask_UploadStatusSnapshot_t upload_status_snapshot;

    System_InputTaskData_t input_display_data;
    static System_ScanTaskData_t display_scan_fallback;
    static System_ManualTaskData_t display_manual_fallback;
    const System_ScanTaskData_t *display_scan_data;
    const System_ManualTaskData_t *display_manual_data;

    AppTask_Status_t task_status;
    AppTask_Status_t upload_snapshot_status;
    AppTask_Status_t monitor_snapshot_status;
    AppTask_Status_t scan_snapshot_status;
    AppTask_Status_t input_snapshot_status;
    AppTask_Status_t manual_snapshot_status;
    AppTask_Status_t display_i2cmutex_status;

    AppDisplay_Status_t display_status = APP_DISPLAY_ERROR_OLED;
    const System_UploadStatus_t *display_upload_status;
    DisplayPage_t display_page;
    DisplayPage_t last_display_page = DISPLAY_PAGE_COUNT;
    int32_t last_menu_encoder_total = 0;
    int32_t current_menu_encoder_total;
    int16_t menu_encoder_delta;
    uint8_t menu_encoder_total_valid = 0;

    int32_t last_manual_encoder_total = 0;
    int32_t current_manual_encoder_total;
    int16_t manual_encoder_delta;
    uint8_t manual_encoder_total_valid = 0;

    TickType_t last_display_input_tick;
    TickType_t display_delay_ticks;
    TickType_t now_tick;
    uint8_t display_idle_refresh = 0;
    uint8_t next_display_idle_refresh;

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_TASK_STARTUP_DELAY_MS));
    display_status = DisplayTask_InitWithI2C();
    while(display_status != APP_DISPLAY_OK)
    {
        App_LogPrintf("[DISPLAY] Init_fail:%s(%d)\r\n",
                    AppDisplayStatusName(display_status),
                    display_status);

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_TASK_REINIT_INTERVAL_MS));

        display_status = DisplayTask_InitWithI2C();
        if(display_status == APP_DISPLAY_OK)
        {
            App_LogPrintf("[DISPLAY] Reinit_success:%s(%d)\r\n",
                        AppDisplayStatusName(display_status),
                        display_status);
        }

        App_Task_UpdateHeartbeat(APP_TASK_ID_DISPLAY);
    }

    last_display_input_tick = xTaskGetTickCount();
    display_delay_ticks = pdMS_TO_TICKS(DISPLAY_TASK_RUN_INTERVAL_MS);

    while(1)
    {
        App_Task_UpdateHeartbeat(APP_TASK_ID_DISPLAY);
        task_status = App_Task_WaitSystemTaskData(&system_data_snapshot, portMAX_DELAY);
        if(task_status == APP_TASK_OK)
        {
            memset(&input_display_data, 0, sizeof(input_display_data));
            monitor_snapshot_ptr = 0;
            scan_snapshot_ptr = 0;
            manual_snapshot_ptr = 0;
            display_scan_data = &display_scan_fallback;
            display_manual_data = &display_manual_fallback;

            input_snapshot_status = App_Task_PeekInputSnapshot(&input_snapshot, 0);
            if((input_snapshot_status == APP_TASK_OK) && input_snapshot.input_data.valid)
            {
                input_display_data = input_snapshot.input_data;
                /*
                 * InputTask 持续保存最近一次输入活动 tick，DisplayTask 即使降刷新
                 * 也不会丢失短按按键这类瞬时事件。
                 */
                if(input_snapshot.last_activity_tick != last_display_input_tick)
                {
                    last_display_input_tick = input_snapshot.last_activity_tick;
                    if(display_idle_refresh)
                    {
                        display_idle_refresh = 0;
                        App_LogPrintf("[POWER] display_active refresh=%lu ms\r\n",
                                      (unsigned long)DISPLAY_TASK_RUN_INTERVAL_MS);
                    }
                }
            }
            else if(input_snapshot_status != APP_TASK_ERROR_QUEUE_READ)
            {
                App_LogPrintf("[DISPLAY] Peek_input_snapshot_error:%s(%d)\r\n",
                              AppTaskStatusName(input_snapshot_status),
                              input_snapshot_status);
            }

            switch (system_data_snapshot.runtime.mode)
            {
                // 自动模式系统数据显示
                case SYSTEM_MODE_AUTO_SCAN:
                {
                    display_upload_status = 0;
                    upload_snapshot_status = App_Task_PeekUploadStatus(&upload_status_snapshot, 0);
                    if(upload_snapshot_status == APP_TASK_OK)
                    {
                        display_upload_status = &upload_status_snapshot.upload_status;
                    }

                    monitor_snapshot_status = App_Task_PeekMonitorSnapshot(&monitor_snapshot_ptr, 0);
                    if((monitor_snapshot_status != APP_TASK_OK) && (monitor_snapshot_status != APP_TASK_ERROR_QUEUE_READ))
                    {
                        App_LogPrintf("[DISPLAY] Peek_monitor_snapshot_error:%s(%d)\r\n",
                                      AppTaskStatusName(monitor_snapshot_status),
                                      monitor_snapshot_status);
                    }

                    scan_snapshot_status = App_Task_PeekScanSnapshot(&scan_snapshot_ptr, 0);
                    if((scan_snapshot_status == APP_TASK_OK) && (scan_snapshot_ptr != 0))
                    {
                        display_scan_data = &scan_snapshot_ptr->scan;
                    }
                    else if(scan_snapshot_status != APP_TASK_ERROR_QUEUE_READ)
                    {
                        App_LogPrintf("[DISPLAY] Peek_scan_snapshot_error:%s(%d)\r\n",
                                      AppTaskStatusName(scan_snapshot_status),
                                      scan_snapshot_status);
                    }

                    /*
                     * 自动模式下，编码器旋转只用于页面切换。
                     * DisplayTask 读取 InputSnapshot 中的 encoder_total，并按自身
                     * 上次消费位置计算菜单切页增量，避免 20ms 快照覆盖导致增量丢失。
                    */
                    if(input_display_data.valid)
                    {
                        current_menu_encoder_total = input_display_data.encoder_total;
                        if((menu_encoder_total_valid == 0) || system_data_snapshot.runtime.mode_changed)
                        {
                            last_menu_encoder_total = current_menu_encoder_total;
                            menu_encoder_total_valid = 1;
                        }
                        else
                        {
                            menu_encoder_delta = (int16_t)(current_menu_encoder_total - last_menu_encoder_total);
                            last_menu_encoder_total = current_menu_encoder_total;
                            App_Menu_UpdateByEncoderDelta(menu_encoder_delta);
                        }
                    }
                    manual_encoder_total_valid = 0;
                    display_page = App_Menu_GetCurrentPage();
                    if(display_page != last_display_page)
                    {
                        App_LogPrintf("[MENU] page=%s\r\n", DisplayTask_PageName(display_page));
                        last_display_page = display_page;
                    }

                    display_i2cmutex_status = App_Task_TakeI2C(pdMS_TO_TICKS(DISPLAY_MUTEX_WAIT_MS));
                    if(display_i2cmutex_status == APP_TASK_OK)
                    {
                        switch(display_page)
                        {
                            case DISPLAY_PAGE_AUTO_SCAN:
                            {
                                display_status = App_Display_UpdateScanWithUpload(display_scan_data, display_upload_status);
                                break;
                            }

                            case DISPLAY_PAGE_UPLOAD:
                            {
                                /*
                                 * 上传页依赖 UploadTask 发布的快照。
                                 * 启动初期暂无上传快照时回退到自动扫描页，避免显示错误页。
                                */
                                if(display_upload_status != 0)
                                {
                                    display_status = App_Display_UpdateUpload(display_upload_status);
                                }
                                else
                                {
                                    display_status = App_Display_UpdateScanWithUpload(display_scan_data, display_upload_status);
                                }
                                break;
                            }

                            case DISPLAY_PAGE_MONITOR:
                            {
                                /*
                                 * 监控页依赖 MonitorTask 发布的快照。
                                 * 启动初期暂无监控快照时回退到自动扫描页。
                                */
                                if((monitor_snapshot_status == APP_TASK_OK) && (monitor_snapshot_ptr != 0))
                                {
                                    display_status = App_Display_UpdateMonitor(monitor_snapshot_ptr);
                                }
                                else
                                {
                                    display_status = App_Display_UpdateScanWithUpload(display_scan_data, display_upload_status);
                                }
                                break;
                            }

                            default:
                            {
                                App_Menu_ResetToAutoPage();
                                display_status = App_Display_UpdateScanWithUpload(display_scan_data, display_upload_status);
                                break;
                            }
                        }

                        display_i2cmutex_status = App_Task_GiveI2C();

                        if(display_status != APP_DISPLAY_OK)
                        {
                            App_LogPrintf("[DISPLAY] Display_fail:%s(%d)\r\n", AppDisplayStatusName(display_status), display_status);
                        }
                        if(display_i2cmutex_status != APP_TASK_OK)
                        {
                            App_LogPrintf("[DISPLAY] Release_i2c_mutex_fail:%s(%d)\r\n", AppTaskStatusName(display_i2cmutex_status), display_i2cmutex_status);
                        }
                    }
                    else
                    {
                        App_LogPrintf("[DISPLAY] Take_i2c_mutex_fail:%s(%d)\r\n", AppTaskStatusName(display_i2cmutex_status), display_i2cmutex_status);
                    }
                    break;
                }

                // 手动控制模式
                case SYSTEM_MODE_MANUAL_SERVO:
                {
                    manual_encoder_delta = 0;
                    manual_snapshot_status = App_Task_PeekManualSnapshot(&manual_snapshot_ptr, 0);
                    if((manual_snapshot_status == APP_TASK_OK) && (manual_snapshot_ptr != 0))
                    {
                        display_manual_data = &manual_snapshot_ptr->manual_data;
                    }
                    else if(manual_snapshot_status != APP_TASK_ERROR_QUEUE_READ)
                    {
                        App_LogPrintf("[DISPLAY] Peek_manual_snapshot_error:%s(%d)\r\n",
                                      AppTaskStatusName(manual_snapshot_status),
                                      manual_snapshot_status);
                    }

                    if(input_display_data.valid)
                    {
                        current_manual_encoder_total = input_display_data.encoder_total;
                        if((manual_encoder_total_valid == 0) || system_data_snapshot.runtime.mode_changed)
                        {
                            last_manual_encoder_total = current_manual_encoder_total;
                            manual_encoder_total_valid = 1;
                        }
                        else
                        {
                            manual_encoder_delta = (int16_t)(current_manual_encoder_total - last_manual_encoder_total);
                            last_manual_encoder_total = current_manual_encoder_total;
                        }
                    }
                    menu_encoder_total_valid = 0;

                    display_i2cmutex_status = App_Task_TakeI2C(pdMS_TO_TICKS(DISPLAY_MUTEX_WAIT_MS));
                    if(display_i2cmutex_status == APP_TASK_OK)
                    {
                        display_status = App_Display_UpdateServoManual(
                            display_manual_data,
                            &input_display_data,
                            manual_encoder_delta
                        );
                        display_i2cmutex_status = App_Task_GiveI2C();

                        if(display_status != APP_DISPLAY_OK)
                        {
                            App_LogPrintf("[DISPLAY] Display_fail:%s(%d)\r\n", AppDisplayStatusName(display_status), display_status);
                        }
                        if(display_i2cmutex_status != APP_TASK_OK)
                        {
                            App_LogPrintf("[DISPLAY] Release_i2c_mutex_fail:%s(%d)\r\n", AppTaskStatusName(display_i2cmutex_status), display_i2cmutex_status);
                        }
                    }
                    else
                    {
                        App_LogPrintf("[DISPLAY] Take_i2c_mutex_fail:%s(%d)\r\n", AppTaskStatusName(display_i2cmutex_status), display_i2cmutex_status);
                    }
                    break;
                }

                // 异常：未知模式
                default:
                {
                    App_LogPrintf("[DISPLAY] Unknown_running_mode:%s(%d)\r\n", App_System_GetModeName(system_data_snapshot.runtime.mode), system_data_snapshot.runtime.mode);
                    break;
                }
            }
        }
        else
        {
            App_LogPrintf("[DISPLAY] Wait_system_runtime_fail:%s(%d)\r\n",
                          AppTaskStatusName(task_status),
                          task_status);
        }

        now_tick = xTaskGetTickCount();
        next_display_idle_refresh = (uint8_t)(((now_tick - last_display_input_tick) >=
                                               pdMS_TO_TICKS(DISPLAY_IDLE_TIMEOUT_MS)) ? 1U : 0U);
        if(next_display_idle_refresh != display_idle_refresh)
        {
            display_idle_refresh = next_display_idle_refresh;
            if(display_idle_refresh)
            {
                App_LogPrintf("[POWER] display_idle refresh=%lu ms\r\n",
                              (unsigned long)DISPLAY_IDLE_REFRESH_INTERVAL_MS);
            }
        }

        /*
         * 降低 OLED 刷新频率，不关闭显示和外设。
         */
        display_delay_ticks = display_idle_refresh ?
                              pdMS_TO_TICKS(DISPLAY_IDLE_REFRESH_INTERVAL_MS) :
                              pdMS_TO_TICKS(DISPLAY_TASK_RUN_INTERVAL_MS);
        vTaskDelay(display_delay_ticks);
    }
}

/*
 * @brief   系统运行过程监视任务函数。
 * @note
 */
void MonitorTask(void *argument)
{
    AppTask_MonitorItem_t monitor_items[APP_TASK_ID_COUNT] =
    {
        {APP_TASK_ID_SYSTEM,  0, 0, pdMS_TO_TICKS(MONITOR_SYSTEM_WARN_THRESHOLD_MS),  APP_TASK_HEALTH_STALE, system_task_handle},
        {APP_TASK_ID_UPLOAD,  0, 0, pdMS_TO_TICKS(MONITOR_UPLOAD_WARN_THRESHOLD_MS),  APP_TASK_HEALTH_STALE, upload_task_handle},
        {APP_TASK_ID_OTA_DOWNLOAD, 0, 0, pdMS_TO_TICKS(MONITOR_OTA_DOWNLOAD_WARN_THRESHOLD_MS), APP_TASK_HEALTH_STALE, ota_download_task_handle},
        {APP_TASK_ID_DISPLAY, 0, 0, pdMS_TO_TICKS(MONITOR_DISPLAY_WARN_THRESHOLD_MS), APP_TASK_HEALTH_STALE, display_task_handle},
        {APP_TASK_ID_MONITOR, 0, 0, pdMS_TO_TICKS(MONITOR_MONITOR_WARN_THRESHOLD_MS), APP_TASK_HEALTH_STALE, monitor_task_handle},
        {APP_TASK_ID_SCAN, 0, 0, pdMS_TO_TICKS(MONITOR_SCAN_WARN_THRESHOLD_MS), APP_TASK_HEALTH_STALE, scan_task_handle},
        {APP_TASK_ID_ENV, 0, 0, pdMS_TO_TICKS(MONITOR_ENV_WARN_THRESHOLD_MS), APP_TASK_HEALTH_STALE, env_task_handle},
        {APP_TASK_ID_INPUT, 0, 0, pdMS_TO_TICKS(MONITOR_INPUT_WARN_THRESHOLD_MS), APP_TASK_HEALTH_STALE, input_task_handle},
        {APP_TASK_ID_MANUAL, 0, 0, pdMS_TO_TICKS(MONITOR_MANUAL_THRESHOLD_MS), APP_TASK_HEALTH_STALE, manual_task_handle}
    };

    App_Task_UpdateHeartbeat(APP_TASK_ID_MONITOR);
    (void)argument;

    static AppTask_MonitorSnapshot_t monitor_snapshot_buffer[2];
    static uint8_t monitor_snapshot_index;
    AppTask_MonitorSnapshot_t *monitor_snapshot_ptr;
    TickType_t last_resource_log_tick;
    AppTask_Id_t task_id;
    AppTask_Status_t task_status;

    last_resource_log_tick = xTaskGetTickCount();
    while(1)
    {
        App_Task_UpdateHeartbeat(APP_TASK_ID_MONITOR);

        for(task_id=0; task_id<APP_TASK_ID_COUNT ; task_id++)
        {
            if(App_Task_IsTaskEnabled(task_id))
            {
                MonitorTask_UpdateItem(&monitor_items[task_id]);
                MonitorTask_LogItem(&monitor_items[task_id]);
            }
        }

        MonitorTask_FeedIWDG(monitor_items);
        AppOtaConfirm_TryConfirmFromMonitor(monitor_items, APP_TASK_ID_COUNT);

        monitor_snapshot_index ^= 1;
        monitor_snapshot_ptr = &monitor_snapshot_buffer[monitor_snapshot_index];

        App_Task_BuildMonitorSnapshot(monitor_snapshot_ptr, monitor_items);
        task_status = App_Task_ReleaseMonitorSnapshot(monitor_snapshot_ptr);
        if(task_status != APP_TASK_OK)
        {
            App_LogPrintf("[MONITOR] Release_snapshot_fail:%s(%d)\r\n",
                        AppTaskStatusName(task_status),
                        task_status);
        }
        MonitorTask_LogResourceIfDue(&last_resource_log_tick, monitor_items);

        vTaskDelay(pdMS_TO_TICKS(MONITOR_TASK_RUN_INTERVAL_MS));
    }
}

/*
 * @brief   扫描任务函数。
 * @note    ScanTask 拥有自动扫描数据，并通过 scan snapshot 队列发布：
 *          - PROGRESS 快照：扫描过程中发布，当前点可更新，All 汇总保持 "--"。
 *          - SUMMARY 快照：整轮扫描结束后发布，All 汇总显示多个点的最终结果。
 *          发布 SUMMARY 后等待 SCAN_TASK_SCAN_INTERVAL_MS，再开始下一轮扫描。
 *          等待期间仍更新心跳，并继续响应 SystemTask 发来的 PAUSE/RUN 通知。
 */

void ScanTask(void *argument)
{
    App_Task_UpdateHeartbeat(APP_TASK_ID_SCAN);

    (void)argument;
    static AppTask_ScanSnapshot_t scan_snapshot_buffer[2] = {0};
    static uint8_t scan_snapshot_index;
    AppTask_ScanSnapshot_t *scan_snapshot_ptr;
    System_ScanTaskData_t scan_task_data = {0};

    AppTask_Status_t task_status;
    AppScan_Status_t scan_status;
    uint32_t notify_value;
    AppTask_Cmd scan_cmd = TASK_CMD_NONE;
    uint8_t scan_enabled = 0;
    uint8_t scan_snapshot_ready = 0;
    TickType_t scan_interval_start_tick;

    scan_status = App_Scan_Init(&scan_task_data);
    while(scan_status != APP_SCAN_OK)
    {
        App_LogPrintf("[SCAN] Init_fail:%s(%d)\r\n", AppScanStatusName(scan_status), scan_status);
        vTaskDelay(pdMS_TO_TICKS(SCAN_TASK_REINIT_INTERVAL_MS));
        scan_status = App_Scan_Init(&scan_task_data);
        if(scan_status == APP_SCAN_OK)
        {
            App_LogPrintf("[SCAN] Reinit_success:%s(%d)\r\n", AppScanStatusName(scan_status), scan_status);
        }
        App_Task_UpdateHeartbeat(APP_TASK_ID_SCAN);
    }

    while(1)
    {
        App_Task_UpdateHeartbeat(APP_TASK_ID_SCAN);

        if(xTaskNotifyWait(0, 0xFFFFFFFFUL, &notify_value, 0) == pdPASS)
        {
            scan_cmd = (AppTask_Cmd)notify_value;
            if(scan_cmd == SCAN_TASK_CMD_RUN)
            {
                scan_enabled = 1;
                (void)App_Task_ReleaseTaskAck(APP_TASK_ID_SCAN, SCAN_TASK_CMD_RUN);
            }
            else if(scan_cmd == SCAN_TASK_CMD_PAUSE)
            {
                scan_enabled = 0;
                (void)App_Task_ReleaseTaskAck(APP_TASK_ID_SCAN, SCAN_TASK_CMD_PAUSE);
            }
        }

        while(scan_enabled == 0)
        {
            App_Task_UpdateHeartbeat(APP_TASK_ID_SCAN);

            if(xTaskNotifyWait(0, 0xFFFFFFFFUL, &notify_value, pdMS_TO_TICKS(1000)) == pdPASS)
            {
                scan_cmd = (AppTask_Cmd)notify_value;
                if(scan_cmd == SCAN_TASK_CMD_RUN)
                {
                    scan_enabled = 1;
                    (void)App_Task_ReleaseTaskAck(APP_TASK_ID_SCAN, SCAN_TASK_CMD_RUN);
                }
            }
        }

        scan_status = App_Scan_Update(&scan_task_data, &scan_snapshot_ready);
        if(scan_status != APP_SCAN_OK)
        {
            App_LogPrintf("[SCAN] Scan_update_error:%s(%d)\r\n",
                    AppScanStatusName(scan_status),
                    scan_status);
        }

        if(scan_snapshot_ready != APP_SCAN_SNAPSHOT_NONE)
        {
            scan_snapshot_index ^= 1;
            scan_snapshot_ptr = &scan_snapshot_buffer[scan_snapshot_index];

            ScanTask_BuildScanSnapshot(scan_snapshot_ptr, &scan_task_data);

            task_status = App_Task_ReleaseScanSnapshot(scan_snapshot_ptr);
            if(task_status != APP_TASK_OK)
            {
                App_LogPrintf("[SCAN] Release_scan_snapshot_fail:%s(%d)\r\n", AppTaskStatusName(task_status), task_status);
            }
        }

        if(scan_snapshot_ready == APP_SCAN_SNAPSHOT_SUMMARY)
        {
            scan_interval_start_tick = xTaskGetTickCount();
            while((xTaskGetTickCount() - scan_interval_start_tick) < pdMS_TO_TICKS(SCAN_TASK_SCAN_INTERVAL_MS))
            {
                App_Task_UpdateHeartbeat(APP_TASK_ID_SCAN);
                if(xTaskNotifyWait(0, 0xFFFFFFFFUL, &notify_value, 0) == pdPASS)
                {
                    scan_cmd = (AppTask_Cmd)notify_value;
                    if(scan_cmd == SCAN_TASK_CMD_PAUSE)
                    {
                        scan_enabled = 0;
                        (void)App_Task_ReleaseTaskAck(APP_TASK_ID_SCAN, SCAN_TASK_CMD_PAUSE);
                        break;
                    }
                    else if(scan_cmd == SCAN_TASK_CMD_RUN)
                    {
                        scan_enabled = 1;
                        (void)App_Task_ReleaseTaskAck(APP_TASK_ID_SCAN, SCAN_TASK_CMD_RUN);
                    }
                }

                vTaskDelay(pdMS_TO_TICKS(SCAN_TASK_RUN_INTERVAL_MS));
            }
        }


        vTaskDelay(pdMS_TO_TICKS(SCAN_TASK_RUN_INTERVAL_MS));
    }
}

/*
 * @brief   环境信息检测任务函数。
 * @note
 */
void EnvTask(void *argument)
{
    App_Task_UpdateHeartbeat(APP_TASK_ID_ENV);
    (void)argument;

    AppTask_Status_t task_status;
    AppSensor_Status_t sensor_status;
    System_EnvTaskData_t env_data = {0};
    AppTask_EnvSnapshot_t env_snapshot = {0};

    sensor_status = App_Sensor_EnvInit(&env_data.env_sensor_status);
    while(sensor_status != APP_SENSOR_OK)
    {
        App_LogPrintf("[ENV] Env_init_fail:%s(%d)\r\n",
                        AppSensorStatusName(sensor_status),
                        sensor_status);
        vTaskDelay(ENV_TASK_REINIT_INTERVAL_MS);
        sensor_status = App_Sensor_EnvInit(&env_data.env_sensor_status);
    }

    while(1)
    {
        App_Task_UpdateHeartbeat(APP_TASK_ID_ENV);

        sensor_status = App_Sensor_EnvUpdate(&env_data);
        if(sensor_status != APP_SENSOR_OK)
        {
            env_data.valid = 0;
            App_LogPrintf("[ENV] Env_data_update_fail:%s(%d)\r\n",
                        AppSensorStatusName(sensor_status),
                        sensor_status);
        }
        env_snapshot.update_tick = xTaskGetTickCount();
        env_snapshot.env = env_data;

        task_status = App_Task_ReleaseEnvSnapshot(&env_snapshot);
        if(task_status != APP_TASK_OK)
        {
            App_LogPrintf("[ENV] Release_env_data_fail:%s(%d)\r\n",
                        AppTaskStatusName(task_status),
                        task_status);
        }

        vTaskDelay(ENV_TASK_RUN_INTERVAL_MS);
    }
}

/*
 * @brief   编码器输入任务函数。
 * @note    周期读取编码器和按键状态，并发布最新输入快照。
 *          旋转增量由 AppInput 累加到 encoder_total；按键点击通过通知发送给 SystemTask。
 */
void InputTask(void *argument)
{
    App_Task_UpdateHeartbeat(APP_TASK_ID_INPUT);
    System_InputTaskData_t input_task_data = {0};
    AppTask_InputSnapshot_t input_snapshot = {0};
    AppInput_Status_t input_status;
    AppTask_Status_t task_status;
    TickType_t last_input_activity_tick;


    input_status = App_Input_Init(&input_task_data);
    while(input_status != APP_INPUT_OK)
    {
        App_LogPrintf("[INPUT] Init_fail:%s(%d)\r\n", AppInputStatusName(input_status), input_status);
        vTaskDelay(pdMS_TO_TICKS(INPUT_TASK_REINIT_INTERVAL_MS));
        input_status = App_Input_Init(&input_task_data);
        if(input_status == APP_INPUT_OK)
        {
            App_LogPrintf("[INPUT] Reinit_success:%s(%d)\r\n", AppInputStatusName(input_status), input_status);
        }
        App_Task_UpdateHeartbeat(APP_TASK_ID_INPUT);
    }

    last_input_activity_tick = xTaskGetTickCount();
    while(1)
    {
        App_Task_UpdateHeartbeat(APP_TASK_ID_INPUT);
        input_status = App_Input_Update(&input_task_data);
        if(input_status != APP_INPUT_OK)
        {
            App_LogPrintf("[INPUT] Input_data_update_fail:%s(%d)\r\n", AppInputStatusName(input_status), input_status);
        }
        else
        {
            if((input_task_data.encoder_data.encoder_delta != 0) ||
               (input_task_data.encoder_data.button_clicked != 0))
            {
                last_input_activity_tick = xTaskGetTickCount();
            }

            if(input_task_data.encoder_data.button_clicked)
            {
                xTaskNotifyGive(system_task_handle);
            }
            input_snapshot.input_data = input_task_data;
            input_snapshot.update_tick = xTaskGetTickCount();
            input_snapshot.last_activity_tick = last_input_activity_tick;

            task_status = App_Task_ReleaseInputSnapshot(&input_snapshot);
            if(task_status != APP_TASK_OK)
            {
                App_LogPrintf("[INPUT] Release_input_snapshot_fail:%s(%d)\r\n", AppTaskStatusName(task_status), task_status);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(INPUT_TASK_RUN_INTERVAL_MS));
    }

}

void ManualTask(void *argument)
{
    App_Task_UpdateHeartbeat(APP_TASK_ID_MANUAL);
    (void)argument;

    // 本任务内部数据
    static AppTask_ManualSnapshot_t manual_snapshot_buffer[2] = {0};
    static uint8_t manual_snapshot_index;
    AppTask_ManualSnapshot_t *manual_snapshot_ptr;

    System_ManualTaskData_t manual_data = {0};


    uint8_t manual_enabled = 0;
    uint32_t notify_value;
    AppTask_Cmd manual_cmd = TASK_CMD_NONE;
    AppTask_Status_t task_status;
    AppServoControl_Status_t servo_control_status;
    AppSensor_Status_t sensor_status;
    AppDetect_Status_t detect_status;
    TickType_t last_sensor_update_tick = 0;
    TickType_t now_tick;
    uint8_t sensor_update_due;


    // 源自其他任务的输入
    AppTask_InputSnapshot_t input_snapshot = {0};

    // 任务相关硬件初始化。
    servo_control_status = App_ServoControl_Init(&manual_data.servo);
    while(servo_control_status != APP_SERVO_CONTROL_OK)
    {
        App_LogPrintf("[MANUAL] Servo_init_fail:%s(%d)\r\n", AppServoControlStatusName(servo_control_status), servo_control_status);
        vTaskDelay(pdMS_TO_TICKS(MANUAL_TASK_REINIT_INTERVAL_MS));
        servo_control_status = App_ServoControl_Init(&manual_data.servo);
        if(servo_control_status == APP_SERVO_CONTROL_OK)
        {
            App_LogPrintf("[MANUAL] Servo_reinit_success:%s(%d)\r\n", AppServoControlStatusName(servo_control_status), servo_control_status);
        }
        App_Task_UpdateHeartbeat(APP_TASK_ID_MANUAL);
    }

    sensor_status = App_Sensor_DirectionInit(&manual_data.direction_sensor_status);
    while(sensor_status != APP_SENSOR_OK)
    {
        App_LogPrintf("[MANUAL] Direction_sensor_init_fail:%s(%d)\r\n", AppSensorStatusName(sensor_status), sensor_status);
        vTaskDelay(pdMS_TO_TICKS(MANUAL_TASK_REINIT_INTERVAL_MS));
        sensor_status = App_Sensor_DirectionInit(&manual_data.direction_sensor_status);
        if(sensor_status == APP_SENSOR_OK)
        {
            App_LogPrintf("[MANUAL] Direction_sensor_reinit_success:%s(%d)\r\n", AppSensorStatusName(sensor_status), sensor_status);
        }
        App_Task_UpdateHeartbeat(APP_TASK_ID_MANUAL);
    }

    while(1)
    {
        App_Task_UpdateHeartbeat(APP_TASK_ID_MANUAL);

        if(xTaskNotifyWait(0, 0xFFFFFFFFUL, &notify_value, 0) == pdPASS)
        {
            manual_cmd = (AppTask_Cmd)notify_value;
            if(manual_cmd == MANUAL_TASK_CMD_RUN)
            {
                manual_enabled = 1;
                last_sensor_update_tick = 0;
                App_ServoControl_ResetInputBaseline();
                (void)App_Task_ReleaseTaskAck(APP_TASK_ID_MANUAL, MANUAL_TASK_CMD_RUN);
            }
            else if(manual_cmd == MANUAL_TASK_CMD_PAUSE)
            {
                manual_enabled = 0;
                (void)App_Task_ReleaseTaskAck(APP_TASK_ID_MANUAL, MANUAL_TASK_CMD_PAUSE);
            }
        }

        while(manual_enabled == 0)
        {
            if(xTaskNotifyWait(0, 0xFFFFFFFFUL, &notify_value, pdMS_TO_TICKS(1000)) == pdPASS)
            {
                manual_cmd = (AppTask_Cmd)notify_value;
                if(manual_cmd == MANUAL_TASK_CMD_RUN)
                {
                    manual_enabled = 1;
                    last_sensor_update_tick = 0;
                    App_ServoControl_ResetInputBaseline();
                    (void)App_Task_ReleaseTaskAck(APP_TASK_ID_MANUAL, MANUAL_TASK_CMD_RUN);
                }
            }
            App_Task_UpdateHeartbeat(APP_TASK_ID_MANUAL);
        }

        task_status = App_Task_PeekInputSnapshot(&input_snapshot, 0);
        if(task_status == APP_TASK_OK)
        {
            if(input_snapshot.input_data.valid)
            {
                servo_control_status = App_ServoControl_Update(&manual_data.servo, input_snapshot.input_data.encoder_total);
                if(servo_control_status != APP_SERVO_CONTROL_OK)
                {
                    manual_data.servo.valid = 0;
                    App_LogPrintf("[MANUAL] Servo_control_update_fail:%s(%d)\r\n",
                                  AppServoControlStatusName(servo_control_status),
                                  servo_control_status);
                }
            }

        }
        else if(task_status != APP_TASK_ERROR_QUEUE_READ)
        {
            App_LogPrintf("[MANUAL] Peek_input_snapshot_error:%s(%d)\r\n",AppTaskStatusName(task_status), task_status);
        }

        now_tick = xTaskGetTickCount();
        sensor_update_due = (uint8_t)((last_sensor_update_tick == 0) ||
                                      ((now_tick - last_sensor_update_tick) >= pdMS_TO_TICKS(MANUAL_TASK_SENSOR_INTERVAL_MS)));
        if(sensor_update_due)
        {
            last_sensor_update_tick = now_tick;

            sensor_status = App_Sensor_DirectionUpdate_Manual(&manual_data);
            if(sensor_status != APP_SENSOR_OK)
            {
                manual_data.detect.human_detected = 0;
                App_LogPrintf("[MANUAL] Direction_sensor_update_fail:%s(%d)\r\n",AppSensorStatusName(sensor_status), sensor_status);
            }
            else
            {
                detect_status = App_Detect_Update(&manual_data.distance, &manual_data.thermal, &manual_data.detect);
                if(detect_status != APP_DETECT_OK)
                {
                    manual_data.detect.human_detected = 0;
                    App_LogPrintf("[MANUAL] Detect_update_fail:%s(%d)\r\n",AppDetectStatusName(detect_status), detect_status);
                }
            }
        }

        manual_data.valid = manual_data.servo.valid;

        if(manual_data.valid)
        {
            manual_snapshot_index ^= 1;
            manual_snapshot_ptr = &manual_snapshot_buffer[manual_snapshot_index];

            manual_snapshot_ptr->manual_data = manual_data;
            manual_snapshot_ptr->update_tick = xTaskGetTickCount();

            App_Task_ReleaseManualSnapshot(manual_snapshot_ptr);
        }

        vTaskDelay(pdMS_TO_TICKS(MANUAL_TASK_RUN_INTERVAL_MS));
    }
}

void SystemTask(void *argument)
{
    SystemTask_Data_t system_data = {0};
    System_Mode_t init_mode;
    System_Mode_t current_mode;
    System_Mode_t next_mode;
    uint32_t input_notify;
    uint8_t pause_ok;
    uint8_t switch_ok;

    init_mode = (System_Mode_t)(uint32_t)argument;
    if((init_mode != SYSTEM_MODE_AUTO_SCAN) && (init_mode != SYSTEM_MODE_MANUAL_SERVO))
    {
        init_mode = SYSTEM_MODE_AUTO_SCAN;
    }

    system_data.runtime.mode = init_mode;
    system_data.runtime.mode_valid = 0;
    system_data.runtime.mode_changed = 1;
    system_data.runtime.last_systemstatus = APP_SYSTEM_OK;

    /*
     * 上电后只启动初始模式对应的业务任务。
     * mode_valid 只有在目标任务确认 RUN 命令后才置 1，避免显示和上传使用未生效的模式。
     */
    if(init_mode == SYSTEM_MODE_AUTO_SCAN)
    {
        switch_ok = SystemTask_SendCommandAndWait(scan_task_handle, APP_TASK_ID_SCAN, SCAN_TASK_CMD_RUN);
    }
    else
    {
        switch_ok = SystemTask_SendCommandAndWait(manual_task_handle, APP_TASK_ID_MANUAL, MANUAL_TASK_CMD_RUN);
    }

    if(switch_ok)
    {
        system_data.runtime.mode_valid = 1;
        App_LogPrintf("[SYSTEM] Init_mode=%s(%d)\r\n",
                      App_System_GetModeName(system_data.runtime.mode),
                      system_data.runtime.mode);
    }
    else
    {
        system_data.runtime.last_systemstatus = APP_SYSTEM_ERROR_INIT;
    }

    SystemTask_PublishRuntime(&system_data);
    system_data.runtime.mode_changed = 0;

    while(1)
    {
        App_Task_UpdateHeartbeat(APP_TASK_ID_SYSTEM);

        /*
         * InputTask 只用任务通知告诉 SystemTask “发生了按键切换事件”。
         * 按键次数在这里合并为一次模式切换，防止切换等待期间累计通知导致来回抖动。
         */
        input_notify = ulTaskNotifyTake(pdTRUE, 0);
        if(input_notify)
        {
            current_mode = system_data.runtime.mode;
            next_mode = current_mode;
            system_data.runtime.mode_valid = 0;
            system_data.runtime.mode_changed = 0;
            pause_ok = 0;
            switch_ok = 0;

            /*
             * 模式切换采用两阶段顺序：
             * 1. 等当前模式任务 PAUSE ACK，确认旧业务已停下；
             * 2. 再向目标模式任务发送 RUN，并等待 RUN ACK。
             * 只有第二阶段成功后才更新 runtime.mode。
             */
            if(current_mode == SYSTEM_MODE_AUTO_SCAN)
            {
                if(SystemTask_SendCommandAndWait(scan_task_handle, APP_TASK_ID_SCAN, SCAN_TASK_CMD_PAUSE))
                {
                    pause_ok = 1;
                    next_mode = SYSTEM_MODE_MANUAL_SERVO;
                    switch_ok = SystemTask_SendCommandAndWait(manual_task_handle, APP_TASK_ID_MANUAL, MANUAL_TASK_CMD_RUN);
                }
            }
            else
            {
                if(SystemTask_SendCommandAndWait(manual_task_handle, APP_TASK_ID_MANUAL, MANUAL_TASK_CMD_PAUSE))
                {
                    pause_ok = 1;
                    next_mode = SYSTEM_MODE_AUTO_SCAN;
                    switch_ok = SystemTask_SendCommandAndWait(scan_task_handle, APP_TASK_ID_SCAN, SCAN_TASK_CMD_RUN);
                }
            }

            if(switch_ok)
            {
                system_data.runtime.mode = next_mode;
                system_data.runtime.mode_valid = 1;
                system_data.runtime.mode_changed = 1;
                system_data.runtime.last_systemstatus = APP_SYSTEM_OK;
                App_LogPrintf("[SYSTEM] Running_mode_changed=%s(%d)\r\n",
                              App_System_GetModeName(system_data.runtime.mode),
                              system_data.runtime.mode);
            }
            else
            {
                if(pause_ok == 0)
                {
                    system_data.runtime.last_systemstatus = (current_mode == SYSTEM_MODE_MANUAL_SERVO) ? APP_SYSTEM_ERROR_MANUAL : APP_SYSTEM_ERROR_AUTO;
                }
                else
                {
                    system_data.runtime.last_systemstatus = (next_mode == SYSTEM_MODE_MANUAL_SERVO) ? APP_SYSTEM_ERROR_MANUAL : APP_SYSTEM_ERROR_AUTO;
                }
                App_LogPrintf("[SYSTEM] Mode_change_fail current=%s(%d) target=%s(%d)\r\n",
                              App_System_GetModeName(current_mode),
                              current_mode,
                              App_System_GetModeName(next_mode),
                              next_mode);
            }
        }

        SystemTask_PublishRuntime(&system_data);
        system_data.runtime.mode_changed = 0;

        vTaskDelay(pdMS_TO_TICKS(SYSTEM_TASK_RUN_INTERVAL_MS));
    }
}
