#ifndef __APP_DISPLAY_H
#define __APP_DISPLAY_H
#include "error_code.h"
#include "system_data.h"
#include "app_task.h"

/*
 * 文件职责：
 * 1. 声明应用层 OLED 页面刷新接口。
 * 2. 负责自动扫描页、上传状态页、任务监控页和手动控制页的页面组织。
 * 3. 对 DisplayTask 屏蔽 OLED 底层绘制、行宽规范化和重复行缓存细节。
 *
 * 当前状态：
 * - DisplayTask 负责读取快照和申请 I2C 互斥量，本模块只负责页面内容渲染。
 */

const char *AppDisplayStatusName(AppDisplay_Status_t status);
AppDisplay_Status_t App_Display_Init(void);
AppDisplay_Status_t App_Display_UpdateScanWithUpload(const System_ScanTaskData_t *system_scan_data,
                                                     const System_UploadStatus_t *system_upload_status);
AppDisplay_Status_t App_Display_UpdateUpload(const System_UploadStatus_t *system_upload_status);
AppDisplay_Status_t App_Display_UpdateMonitor(const AppTask_MonitorSnapshot_t *monitor_snapshot);
AppDisplay_Status_t App_Display_UpdateServoManual(const System_ManualTaskData_t *manual_data,
                                                  const System_InputTaskData_t *input_data,
                                                  int16_t encoder_delta);

#endif /* __APP_DISPLAY_H */
