#ifndef __APP_TASK_H
#define __APP_TASK_H
#include "error_code.h"
#include "system_data.h"
#include "app_ota_download.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

/*
 * 文件职责：
 * 1. 声明 FreeRTOS 应用任务入口、任务 ID 和任务间通信接口。
 * 2. 定义 System/Input/Scan/Manual/Env/Display/Upload/Monitor 相关快照类型。
 * 3. 隔离队列、任务通知、I2C 互斥量和心跳接口，避免业务模块分散依赖 RTOS 细节。
 *
 * 当前状态：
 * - User/main.c 创建任务句柄。
 * - App/app_task.c 实现任务函数、快照发布/读取和模式切换 ACK。
 */

/*
 * @brief   任务ID。
 * @note    用于MonitorTask中区分各个任务
 *          APP_TASK_ID_COUNT：任务ID数
 */
typedef enum
{
    APP_TASK_ID_SYSTEM = 0,
    APP_TASK_ID_UPLOAD,
    APP_TASK_ID_OTA_DOWNLOAD,
    APP_TASK_ID_DISPLAY,
    APP_TASK_ID_MONITOR,
    APP_TASK_ID_SCAN,
    APP_TASK_ID_ENV,
    APP_TASK_ID_INPUT,
    APP_TASK_ID_MANUAL,
    APP_TASK_ID_COUNT
} AppTask_Id_t;

/*
 * @brief   UploadTask上传状态快照。
 * @note    AppTask_UploadStatusSnapshot_t:
 *          - upload_status: 上传状态数据
 *          - update_tick: 快照更新时间
 */
typedef struct
{
    System_UploadStatus_t upload_status;
    TickType_t update_tick;
} AppTask_UploadStatusSnapshot_t;

/*
 * @brief   任务状态。
 * @note    AppTask_Health_t:
 *          - APP_TASK_HEALTH_ALIVE: 任务最近更新过心跳
 *          - APP_TASK_HEALTH_STALE: 任务超过阈值没有更新心跳
 */
typedef enum
{
    APP_TASK_HEALTH_ALIVE = 0,
    APP_TASK_HEALTH_STALE
} AppTask_Health_t;

/*
 * @brief   任务监控数据。
 * @note    AppTask_MonitorItem_t:
 *          - task_id: 任务ID
 *          - last_tick: 上一次心跳更新tick值
 *          - age_tick: 心跳更新tick间隔
 *          - timeout_tick：该任务超时阈值
 *          - health：当前任务状态
 */
typedef struct
{
    AppTask_Id_t task_id;
    TickType_t last_tick;
    TickType_t age_tick;
    TickType_t timeout_tick;
    AppTask_Health_t health;
    TaskHandle_t task_handle;
} AppTask_MonitorItem_t;

/*
 * @brief   MonitorTask任务监控数据快照。
 * @note    AppTask_MonitorSnapshot_t:
 *          - items: 任务监控数据数组
 *          - update_tick: 快照更新时间
 */
typedef struct
{
    AppTask_MonitorItem_t items[APP_TASK_ID_COUNT];
    TickType_t update_tick;
} AppTask_MonitorSnapshot_t;

/*
 * @brief   ScanTask数据快照。
 * @note    AppTask_ScanSnapshot_t:
 *          - scan: 多角度扫描结果
 *          - update_tick: 数据更新tick
 */
typedef struct
{
    System_ScanTaskData_t scan;
    TickType_t update_tick;
} AppTask_ScanSnapshot_t;

/*
 * @brief   EnvTask数据快照。
 * @note    AppTask_EnvSnapshot_t:
 *          - env: 环境检测结果
 *          - update_tick: 数据更新tick
 */
typedef struct
{
    System_EnvTaskData_t env;
    TickType_t update_tick;
} AppTask_EnvSnapshot_t;

/*
 * @brief   InputTask数据快照。
 * @note    AppTask_InputSnapshot_t:
 *          - input_data: 编码器输入数据。
 *          - update_tick: 数据更新tick。
 *          - last_activity_tick: 最近一次按键或编码器输入tick。
 */
typedef struct
{
    System_InputTaskData_t input_data;
    TickType_t update_tick;
    TickType_t last_activity_tick;
} AppTask_InputSnapshot_t;

/*
 * @brief   ManualTask数据快照。
 * @note    AppTask_ManualSnapshot_t:
 *          - manual_data: 手动控制任务数据。
 *          - update_tick: 数据更新tick。
 */
typedef struct
{
    System_ManualTaskData_t manual_data;
    TickType_t update_tick;
} AppTask_ManualSnapshot_t;

/*
 * @brief   AppTask_Cmd 控制 ScanTask 和 ManualTask 运行状态。
 * @note    AppTask_Cmd:
 *          - TASK_CMD_NONE: 空。
 *          - TASK_CMD_RUN: 运行。
 *          - TASK_CMD_PAUSE: 暂停任务,使其进入阻塞态。
 */
typedef enum
{
    TASK_CMD_NONE = 0,
    SCAN_TASK_CMD_RUN,
    SCAN_TASK_CMD_PAUSE,
    MANUAL_TASK_CMD_RUN,
    MANUAL_TASK_CMD_PAUSE
} AppTask_Cmd;

typedef struct
{
    AppTask_Id_t task_id;
    AppTask_Cmd cmd;
    TickType_t update_tick;
} AppTask_Ack_t;


const char *AppTaskStatusName(AppTask_Status_t status);
AppTask_Status_t App_Task_Init(void);
/*
 * @brief   判断某个任务是否应该被 MonitorTask 监控。
 * @note    当前恢复完整业务任务集合，OtaDownloadTask 也纳入监控。
 */
uint8_t App_Task_IsTaskEnabled(AppTask_Id_t task_id);
// UploadTask ----------------------------------------------------------------------
AppTask_Status_t App_Task_ReleaseUploadStatus(const AppTask_UploadStatusSnapshot_t *snapshot);
AppTask_Status_t App_Task_PeekUploadStatus(AppTask_UploadStatusSnapshot_t *snapshot, TickType_t wait_ticks);
// I2C Mutex ----------------------------------------------------------------------
AppTask_Status_t App_Task_TakeI2C(TickType_t wait_ticks);
AppTask_Status_t App_Task_GiveI2C(void);
// ESP01 Mutex ----------------------------------------------------------------------
/*
 * @brief   保护 USART3/AT Parser/ESP01 命令序列的互斥锁接口。
 * @note    UploadTask 和 OtaDownloadTask 操作 ESP01 前必须先获取该锁。
 */
AppTask_Status_t App_Task_TakeEsp01(TickType_t wait_ticks);
AppTask_Status_t App_Task_GiveEsp01(void);
// OtaDownloadTask ----------------------------------------------------------------------
/*
 * @brief   请求 OtaDownloadTask 执行一次 App 侧 HTTP OTA 下载。
 * @param   request: 可选 OTA 请求参数；为空时使用 app_ota_config.h 默认路径。
 * @note    后续服务器响应、串口命令或菜单入口都应调用该接口，而不是直接调用下载函数。
 */
AppTask_Status_t App_Task_RequestOtaDownload(const AppOtaDownload_Request_t *request);
// MonitorTask ----------------------------------------------------------------------
AppTask_Status_t App_Task_UpdateHeartbeat(AppTask_Id_t task_id);
AppTask_Status_t App_Task_ReleaseMonitorSnapshot(const AppTask_MonitorSnapshot_t *snapshot);
AppTask_Status_t App_Task_PeekMonitorSnapshot(const AppTask_MonitorSnapshot_t **snapshot, TickType_t wait_ticks);
// ScanTask ----------------------------------------------------------------------
AppTask_Status_t App_Task_ReleaseScanSnapshot(const AppTask_ScanSnapshot_t *snapshot);
AppTask_Status_t App_Task_PeekScanSnapshot(const AppTask_ScanSnapshot_t **snapshot, TickType_t wait_ticks);
// EnvTask ----------------------------------------------------------------------
AppTask_Status_t App_Task_ReleaseEnvSnapshot(const AppTask_EnvSnapshot_t *snapshot);
AppTask_Status_t App_Task_PeekEnvSnapshot(AppTask_EnvSnapshot_t *snapshot, TickType_t wait_ticks);
// InputTask ----------------------------------------------------------------------
AppTask_Status_t App_Task_ReleaseInputSnapshot(const AppTask_InputSnapshot_t *snapshot);
AppTask_Status_t App_Task_PeekInputSnapshot(AppTask_InputSnapshot_t *snapshot, TickType_t wait_ticks);
// ManualTask ----------------------------------------------------------------------
AppTask_Status_t App_Task_ReleaseManualSnapshot(const AppTask_ManualSnapshot_t *snapshot);
AppTask_Status_t App_Task_PeekManualSnapshot(const AppTask_ManualSnapshot_t **snapshot, TickType_t wait_ticks);


void UploadTask(void *argument);
/*
 * @brief   App 侧 HTTP OTA 下载任务入口。
 * @note    由服务器命令或上层入口投递请求后执行下载。
 */
void OtaDownloadTask(void *argument);
void DisplayTask(void *argument);
void MonitorTask(void *argument);
void ScanTask(void *argument);
void EnvTask(void *argument);
void InputTask(void *argument);
void ManualTask(void *argument);
void SystemTask(void *argument);

#endif /* __APP_TASK_H */
