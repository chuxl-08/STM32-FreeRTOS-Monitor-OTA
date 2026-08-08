#ifndef __APP_SCAN_H
#define __APP_SCAN_H
#include "error_code.h"
#include "system_data.h"

/*
 * 文件职责：
 * 1. 声明应用层舵机固定角度扫描接口。
 * 2. 负责调度 SG90 舵机转到固定角度，并在稳定后读取方向相关传感器。
 * 3. 保存每个固定角度下的距离、热阵列摘要和人员检测结果。
 * 4. 用快照类型区分扫描过程快照和整轮汇总快照。
 */

typedef enum
{
    /* 设置当前扫描角度，并记录舵机稳定等待起点。 */
    APP_SCAN_STATE_SET_ANGLE = 0,

    /* 等待舵机转到目标角度后稳定，等待期间不阻塞任务。 */
    APP_SCAN_STATE_WAIT_STABLE,

    /* 读取当前方向传感器，并执行人员检测。 */
    APP_SCAN_STATE_READ_SENSOR,

    /* 全角度扫描完成，更新整轮汇总信息。 */
    APP_SCAN_STATE_SUMMARY
} AppScan_State_t;

/* 本轮 App_Scan_Update() 没有产生需要发布的新快照。 */
#define APP_SCAN_SNAPSHOT_NONE       0U
/* 扫描正在进行，当前点数据可更新，但整轮 All 汇总应保持无效。 */
#define APP_SCAN_SNAPSHOT_PROGRESS   1U
/* 一整轮扫描完成，All 汇总结果已经刷新，可在 OLED 和上传中使用。 */
#define APP_SCAN_SNAPSHOT_SUMMARY    2U

const char *AppScanStatusName(AppScan_Status_t status);
AppScan_Status_t App_Scan_Init(System_ScanTaskData_t *system_data);
AppScan_Status_t App_Scan_Update(System_ScanTaskData_t *system_scan_data, uint8_t *snapshot_ready);

#endif /* __APP_SCAN_H */
