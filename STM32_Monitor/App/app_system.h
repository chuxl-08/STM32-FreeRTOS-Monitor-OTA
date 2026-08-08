#ifndef __APP_SYSTEM_H
#define __APP_SYSTEM_H
#include "error_code.h"
#include "system_data.h"

/*
 * 文件职责：
 * 1. 声明系统运行状态和模式相关的日志辅助接口。
 * 2. SystemTask 的模式协调逻辑位于 app_task.c。
 *
 * 当前状态：
 * - 显示刷新、网络上传和 RTOS 互斥/队列由对应任务模块负责。
 */


const char *AppSystemStatusName(AppSystem_Status_t status);
const char *App_System_GetModeName(System_Mode_t mode);

#endif /* __APP_SYSTEM_H */
