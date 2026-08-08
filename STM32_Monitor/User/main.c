#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "app_system.h"
#include "bsp_gpio.h"
#include "app_task.h"
#include "app_log.h"
#include "app_input.h"
#include "bsp_usart.h"
#include "bsp_iwdg.h"

#ifndef APP_VECTOR_BASE_ADDR
#error "APP_VECTOR_BASE_ADDR must be defined by Keil target"
#endif

TaskHandle_t system_task_handle = NULL;
TaskHandle_t upload_task_handle = NULL;
TaskHandle_t ota_download_task_handle = NULL;
TaskHandle_t display_task_handle = NULL;
TaskHandle_t monitor_task_handle = NULL;
TaskHandle_t scan_task_handle = NULL;
TaskHandle_t env_task_handle = NULL;
TaskHandle_t input_task_handle = NULL;
TaskHandle_t manual_task_handle = NULL;

static void Main_FaultStop(uint16_t fault_led)
{
    taskDISABLE_INTERRUPTS();
    BSP_GPIO_LED_Off(D3 | D4 | D5);
    BSP_GPIO_LED_On(fault_led);

    while(1)
    {
    }
}

void vApplicationMallocFailedHook(void)
{
    Main_FaultStop(D5);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    Main_FaultStop(D4 | D5);
}

int main(void)
{
	SCB->VTOR = APP_VECTOR_BASE_ADDR;
    __DSB();
    __ISB();
    __enable_irq();

    BaseType_t task_create_status;
    AppTask_Status_t queue_create_status;
    BSP_IWDG_RST_REASON rst_reason;

    BSP_GPIO_LED_Init();
    BSP_GPIO_LED_Off(D3 | D4 | D5);
    BSP_USART1_Init();
    App_Log_Init();

    /*
     * 先读取并清除上次复位原因，再启动 IWDG。
     * 启动后的喂狗动作统一交给 MonitorTask 的任务健康检查。
     */
    rst_reason = BSP_IWDG_ResetReason();
    BSP_IWDG_Init();
    App_LogPrintf("[BOOT] Reset_reason=%s\r\n", ResetReasonName(rst_reason));

    queue_create_status = App_Task_Init();
    if(queue_create_status != APP_TASK_OK)
    {
        Main_FaultStop(D4);
    }

	IWDG_ReloadCounter();

    task_create_status = xTaskCreate(SystemTask, "System", 256, (void *)(uint32_t)SYSTEM_MODE_AUTO_SCAN, 4, &system_task_handle);
    if(task_create_status != pdPASS)
    {
        Main_FaultStop(D4);
    }

    task_create_status = xTaskCreate(InputTask, "Input", 256, NULL, 3, &input_task_handle);
    if(task_create_status != pdPASS)
    {
        Main_FaultStop(D4);
    }

	IWDG_ReloadCounter();

    task_create_status = xTaskCreate(ScanTask, "Scan", 512, NULL, 2, &scan_task_handle);
    if(task_create_status != pdPASS)
    {
        Main_FaultStop(D4);
    }

    task_create_status = xTaskCreate(ManualTask, "Manual", 384, NULL, 2, &manual_task_handle);
    if(task_create_status != pdPASS)
    {
        Main_FaultStop(D4);
    }

	IWDG_ReloadCounter();

    task_create_status = xTaskCreate(DisplayTask, "Display", 384, NULL, 1, &display_task_handle);
    if(task_create_status != pdPASS)
    {
        Main_FaultStop(D4);
    }

    task_create_status = xTaskCreate(UploadTask, "Upload", 384, NULL, 1, &upload_task_handle);
    if(task_create_status != pdPASS)
    {
        Main_FaultStop(D4);
    }

    task_create_status = xTaskCreate(OtaDownloadTask, "OtaDl", 512, NULL, 2, &ota_download_task_handle);
    if(task_create_status != pdPASS)
    {
        Main_FaultStop(D4);
    }

	IWDG_ReloadCounter();

    task_create_status = xTaskCreate(MonitorTask, "Monitor", 384, NULL, 1, &monitor_task_handle);
    if(task_create_status != pdPASS)
    {
        Main_FaultStop(D4);
    }

    task_create_status = xTaskCreate(EnvTask, "Env", 256, NULL, 1, &env_task_handle);
    if(task_create_status != pdPASS)
    {
        Main_FaultStop(D4);
    }

	IWDG_ReloadCounter();

    vTaskStartScheduler();

    Main_FaultStop(D4);
}
