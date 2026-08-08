#ifndef __AMG8833_H
#define __AMG8833_H
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明 AMG8833 红外热阵列驱动接口。
 * 2. 负责 I2C 寄存器读写、8x8 像素温度读取和数据转换。
 * 3. 提供最高温、最低温、平均温、热区像素数量和最高温坐标等摘要数据。
 *
 * 当前状态：
 * - 已提供设备就绪检测、初始化、热敏电阻读取、像素矩阵读取和摘要统计接口。
 * - 当前模块 I2C 7 位地址按 0x69 规划。
 */

#define PCTL       0x00    // 电源控制
#define RST        0x01    // 软件复位
#define FPSC       0x02    // 帧率
#define STAT       0x04    // 状态
#define SCLR       0x05    // 状态清除
#define TTHL       0x0E    // 热敏电阻低字节
#define TTHH       0x0F    // 热敏电阻高字节
#define PIXEL_START    0x80    // 像素温度起始
#define PIXEL_END      0xFF    // 64 个像素温度，每个像素 2 字节

#define AMG8833_PIXEL_ROWS     8
#define AMG8833_PIXEL_COLS     8
#define AMG8833_PIXEL_COUNT    (AMG8833_PIXEL_ROWS * AMG8833_PIXEL_COLS)
#define AMG8833_TEMP_THRESHOLD 3000     // 热区温度阈值

/*
 * @brief   AMG8833_Summary_t: AMG8833 热阵列摘要统计结果
 * @note
 *          - max_temp_x100: 最高温，单位 0.01 C
 *          - min_temp_x100: 最低温，单位 0.01 C
 *          - avg_temp_x100: 平均温，单位 0.01 C
 *          - max_row: 最高温所在行，范围 0~7
 *          - max_col: 最高温所在列，范围 0~7
 *          - hot_pixel_count: 超过阈值的热像素数量
 */
typedef struct
{
    int16_t max_temp_x100;
    int16_t min_temp_x100;
    int16_t avg_temp_x100;
    uint8_t max_row;
    uint8_t max_col;
    uint8_t hot_pixel_count;
} AMG8833_Summary_t;

/*
 * @brief   AMG8833_Data_t: AMG8833 对外输出数据
 * @note
 *          - temp_summary: 热阵列摘要统计结果
 *          - valid: 数据有效性
 *              1 ： 有效
 *              0 ： 无效
 */
typedef struct
{
    AMG8833_Summary_t temp_summary;
    uint8_t valid;
} AMG8833_Data_t;


AMG8833_Status_t AMG8833_IsReady(void);
AMG8833_Status_t AMG8833_Init(void);
AMG8833_Status_t AMG8833_ReadThermistor(int16_t *ther_temp_x100);
AMG8833_Status_t AMG8833_ReadPixels(int16_t *temp_x100);
AMG8833_Status_t AMG8833_CalcSummary(const int16_t *temp_x100, int16_t hot_threshold_x100, AMG8833_Summary_t *summary);
AMG8833_Status_t AMG8833_Read(AMG8833_Data_t *data);

#endif /* __AMG8833_H */
