#include "app_log.h"
#include <stdarg.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "bsp_usart.h"

/*
 * 文件职责：
 * 1. 提供 App 层统一日志输出入口。
 * 2. 调度器启动后使用互斥量保护格式化缓冲区和 USART 输出。
 * 3. 调度器启动前允许直接输出启动阶段日志。
 */

#define APP_LOG_BUFFER_SIZE 256

static SemaphoreHandle_t app_log_mutex = NULL;
static char app_log_buffer[APP_LOG_BUFFER_SIZE];

void App_Log_Init(void)
{
    if(app_log_mutex == NULL)
    {
        app_log_mutex = xSemaphoreCreateMutex();
    }
}

void App_LogPrintf(const char *format, ...)
{
    va_list args;
    int len;
    uint8_t mutex_taken = 0;

    if(format == NULL)
    {
        return;
    }

    if((app_log_mutex != NULL) &&
       (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING))
    {
        if(xSemaphoreTake(app_log_mutex, portMAX_DELAY) == pdTRUE)
        {
            mutex_taken = 1;
        }
    }

    va_start(args, format);
    len = vsnprintf(app_log_buffer, APP_LOG_BUFFER_SIZE, format, args);
    va_end(args);

    if(len > 0)
    {
        app_log_buffer[APP_LOG_BUFFER_SIZE - 1] = '\0';
        BSP_USART1_SendString(app_log_buffer);
    }

    if(mutex_taken)
    {
        xSemaphoreGive(app_log_mutex);
    }
}
