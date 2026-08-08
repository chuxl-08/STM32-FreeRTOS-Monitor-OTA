#include "amg8833.h"
#include "bsp_i2c.h"
#include "Delay.h"
#include <string.h>

/*
 * 文件职责：
 * 1. 实现 AMG8833 I2C 驱动。
 * 2. 初始化 AMG8833 工作模式、复位状态和帧率。
 * 3. 读取片上热敏电阻温度和 64 个红外像素温度。
 * 4. 使用 x100 定点数输出温度，避免在 STM32F103 上依赖 float。
 *
 * 当前状态：
 * - 已完成设备就绪检测、初始化、热敏电阻读取和 8x8 像素读取。
 */

#define AMG8833_ADDRESS        0x69

static uint8_t s_amg8833_init_status = 0;

 /**
  * @brief  检测 AMG8833 是否在 I2C 总线上响应。
  * @param  None
  * @retval AMG8833_Status_t:
  *         - AMG8833_OK：设备有 ACK 响应。
  *         - AMG8833_ERROR_DEVICE_NOT_READY：设备无响应。
  */
 AMG8833_Status_t AMG8833_IsReady(void)
{
    if(BSP_I2C_IsDeviceReady(AMG8833_ADDRESS) != BSP_I2C_OK)
    {
        return AMG8833_ERROR_DEVICE_NOT_READY;
    }
    else
    {
        return AMG8833_OK;
    }
}

/**
  * @brief  向 AMG8833 指定寄存器写入一个命令字节。
  * @param  reg_address: AMG8833 寄存器地址。
  * @param  command: 写入寄存器的命令或配置值。
  * @retval AMG8833_Status_t:
  *         - AMG8833_OK：写入成功。
  *         - AMG8833_ERROR_I2C：I2C 写入失败。
  */
static AMG8833_Status_t AMG8833_Write_Command(uint8_t reg_address, uint8_t command)
{
    if(BSP_I2C_WriteByte(AMG8833_ADDRESS, reg_address, command) != BSP_I2C_OK)
    {
        return AMG8833_ERROR_I2C;
    }
    else
    {
        return AMG8833_OK;
    }
}


 /**
  * @brief  初始化 AMG8833。
  * @param  None
  * @retval AMG8833_Status_t:
  *         - AMG8833_OK：初始化成功。
  *         - AMG8833_ERROR_I2C：I2C 初始化或寄存器写入失败。
  *         - AMG8833_ERROR_DEVICE_NOT_READY：设备无 ACK 响应。
  * @note   当前初始化流程：
  *         1. 初始化 I2C1。
  *         2. 检测 0x69 地址设备是否在线。
  *         3. PCTL 写 0x00，进入 Normal mode。
  *         4. RST 写 0x3F，执行 Initial reset。
  *         5. FPSC 写 0x00，配置为 10 FPS。
  */
 AMG8833_Status_t AMG8833_Init(void)
 {
    if(s_amg8833_init_status)
    {
        return AMG8833_OK;
    }
    else
    {
        if(BSP_I2C_Init() != BSP_I2C_OK)
        {
            return AMG8833_ERROR_I2C;
        }

        if(AMG8833_IsReady() != AMG8833_OK)
        {
            return AMG8833_ERROR_DEVICE_NOT_READY;
        }

        // 电源控制 模式配置 Normal mode
        if(AMG8833_Write_Command(PCTL, 0x00) != AMG8833_OK)
        {
            return AMG8833_ERROR_I2C;
        }

        // 软件复位 Initial reset
        if(AMG8833_Write_Command(RST, 0x3F) != AMG8833_OK)
        {
            return AMG8833_ERROR_I2C;
        }

        Delay_ms(50);   // 等待复位完成

        // 帧率 10 FPS
        if(AMG8833_Write_Command(FPSC, 0x00) != AMG8833_OK)
        {
            return AMG8833_ERROR_I2C;
        }

        // 状态清除
        if(AMG8833_Write_Command(SCLR, 0x00) != AMG8833_OK)
        {
            return AMG8833_ERROR_I2C;
        }

        s_amg8833_init_status = 1;

        return AMG8833_OK;
    }

 }

 /**
  * @brief  转换 AMG8833 像素原始数据。
  * @param  raw_H: 像素温度高字节。
  * @param  raw_L: 像素温度低字节。
  * @param  signed_raw: 输出的有符号原始值，单位为 0.25 C/LSB。
  * @retval None
  * @note   像素温度寄存器 0x80~0xFF 使用 12 bit 二补码格式：
  *         - raw[10:0] 为温度数值。
  *         - raw[11] 为符号位。
  *         - 1 LSB = 0.25 C。
  */
 static void AMG8833_ConvertPixelRaw(uint8_t raw_H, uint8_t raw_L, int16_t * signed_raw)
 {
    uint16_t raw;
    raw = ((uint16_t)(raw_H) << 8) | raw_L;
    raw &= 0x0FFF;
    *signed_raw = ((int16_t) (raw << 4)) >> 4;   // 左移让符号位到最高位,右移自动符号扩展
 }

 /**
  * @brief  转换 AMG8833 片上热敏电阻原始数据。
  * @param  raw_H: 热敏电阻温度高字节。
  * @param  raw_L: 热敏电阻温度低字节。
  * @param  temp_x100: 输出温度，单位为 0.01 C。
  * @retval None
  * @note   热敏电阻寄存器 0x0E/0x0F 使用“符号位 + 绝对值”格式，
  *         bit11 为符号位，低 11 bit 为绝对值，1 LSB = 0.0625 C。
  */
 static void AMG8833_ConvertThermistorRaw(uint8_t raw_H, uint8_t raw_L, int16_t *temp_x100)
 {
    uint16_t abs_raw;
    uint8_t is_negative;

    is_negative = raw_H & 0x08;
    abs_raw = (((uint16_t)(raw_H & 0x07)) << 8) | raw_L;

    *temp_x100 = (int16_t)(abs_raw * 625 / 100);   // 1 LSB = 0.0625 C，结果放大 100 倍
    if(is_negative)
    {
        *temp_x100 = -*temp_x100;
    }
 }


 /**
  * @brief  读取 AMG8833 片上热敏电阻温度。
  * @param  ther_temp_x100: 输出温度，单位为 0.01 C。
  * @retval AMG8833_Status_t:
  *         - AMG8833_OK：读取成功。
  *         - AMG8833_ERROR_PARAM：输出参数为空。
  *         - AMG8833_ERROR_I2C：I2C 读取失败。
  * @note   热敏电阻温度 传感器本体温度
  */
 AMG8833_Status_t AMG8833_ReadThermistor(int16_t *ther_temp_x100)
 {
    if(ther_temp_x100 == 0)
    {
        return AMG8833_ERROR_PARAM;
    }

    uint8_t raw_H;
    uint8_t raw_L;

    if(BSP_I2C_ReadByte(AMG8833_ADDRESS, TTHL, &raw_L) != BSP_I2C_OK)
    {
        return AMG8833_ERROR_I2C;
    }

    if(BSP_I2C_ReadByte(AMG8833_ADDRESS, TTHH, &raw_H) != BSP_I2C_OK)
    {
        return AMG8833_ERROR_I2C;
    }

    AMG8833_ConvertThermistorRaw(raw_H, raw_L, ther_temp_x100);

    return AMG8833_OK;
  }

 /**
  * @brief  将 128 字节像素原始数据转换为 64 个 x100 温度值。
  * @param  raw_array: 原始像素数据数组，低字节在前，高字节在后。
  * @param  array: 输出温度数组，长度至少为 64，单位为 0.01 C。
  * @retval None
  * @note   AMG8833 像素温度从 0x80 开始，每个像素 2 字节：
  *         Pixel[n] = raw_array[n*2] + raw_array[n*2+1]。
  *         像素分辨率为 0.25 C，因此转换为 x100 时乘以 25。
  */
 static void raw_array_process(uint8_t * raw_array, int16_t * array)
 {
    uint16_t index;
    for(index=0; index < AMG8833_PIXEL_COUNT; index++)
    {
        AMG8833_ConvertPixelRaw(raw_array[index*2+1], raw_array[index*2], &array[index]);
        array[index] = array[index] * 25;
    }
 }

 /**
  * @brief  读取 AMG8833 8x8 像素温度矩阵。
  * @param  temp_x100: 输出温度数组，长度至少为 64，单位为 0.01 C。
  * @retval AMG8833_Status_t:
  *         - AMG8833_OK：读取成功。
  *         - AMG8833_ERROR_PARAM：输出参数为空。
  *         - AMG8833_ERROR_I2C：I2C 连续读取失败。
  * @note   当前通过 BSP_I2C_ReadBytes() 从 0x80 连续读取 128 字节，
  *         再转换为 64 个像素温度。数组顺序与手册 Pixel 1~64 顺序一致。
  */
 AMG8833_Status_t AMG8833_ReadPixels(int16_t *temp_x100)
 {
    if(temp_x100 == 0)
    {
        return AMG8833_ERROR_PARAM;
    }

    uint8_t raw[AMG8833_PIXEL_COUNT * 2];

    if(BSP_I2C_ReadBytes(AMG8833_ADDRESS, PIXEL_START, raw, AMG8833_PIXEL_COUNT * 2) != BSP_I2C_OK)
    {
        return AMG8833_ERROR_I2C;
    }

    raw_array_process(raw, temp_x100);

    return AMG8833_OK;
 }


/*
 * @brief   统计 AMG8833 8x8 像素温度摘要。
 * @param   temp_x100: 64 个像素温度数组，单位 0.01 C。
 * @param   hot_threshold_x100: 热区判定阈值，单位 0.01 C。
 * @param   summary: 摘要统计结果输出指针。
 * @retval  AMG8833_Status_t:
 *          - AMG8833_OK: 统计成功。
 *          - AMG8833_ERROR_PARAM: 输入或输出参数为空。
 * @note    本函数不访问 I2C，只对已经读取到的像素数组做最高温、
 *          最低温、平均温、最高温坐标和热区像素数量统计。
 */
AMG8833_Status_t AMG8833_CalcSummary(const int16_t *temp_x100, int16_t hot_threshold_x100, AMG8833_Summary_t *summary)
{
    uint8_t row;
    uint8_t col;
    uint8_t index;
    int16_t temp;
    int16_t temp_max;
    int16_t temp_min;
    int32_t sum;

    if(temp_x100 == 0 || summary == 0)
    {
        return AMG8833_ERROR_PARAM;
    }

    temp_max = temp_x100[0];
    temp_min = temp_x100[0];
    sum = 0;

    summary->max_row = 0;
    summary->max_col = 0;
    summary->hot_pixel_count = 0;

    for(row = 0; row < AMG8833_PIXEL_ROWS; row++)
    {
        for(col = 0; col < AMG8833_PIXEL_COLS; col++)
        {
            index = row * AMG8833_PIXEL_COLS + col;
            temp = temp_x100[index];
            sum += temp;

            if(temp > temp_max)
            {
                temp_max = temp;
                summary->max_row = row;
                summary->max_col = col;
            }
            if(temp < temp_min)
            {
                temp_min = temp;
            }
            if(temp > hot_threshold_x100)
            {
                 summary->hot_pixel_count++;
            }
        }
    }

    summary->max_temp_x100 = temp_max;
    summary->min_temp_x100 = temp_min;
    summary->avg_temp_x100 = (int16_t)(sum / AMG8833_PIXEL_COUNT);

    return AMG8833_OK;
}

/*
 * @brief   读取 AMG8833 像素矩阵并输出摘要数据。
 * @param   data: AMG8833_Data_t 输出指针。
 * @retval  AMG8833_Status_t:
 *          - AMG8833_OK: 读取和统计成功。
 *          - AMG8833_ERROR_PARAM: 参数为空或摘要统计参数异常。
 *          - AMG8833_ERROR_I2C: 像素矩阵读取失败。
 * @note    当前接口不保存完整 8x8 温度矩阵，只输出摘要统计结果和 valid 标志；
 *          完整像素数组仅在函数内部临时使用。
 */
AMG8833_Status_t AMG8833_Read(AMG8833_Data_t *data)
{
    int16_t temp_x100[AMG8833_PIXEL_COUNT] = {0};

    if(data == 0)
    {
        return AMG8833_ERROR_PARAM;
    }

    memset(data, 0, sizeof(*data));

    if(AMG8833_ReadPixels(temp_x100) != AMG8833_OK)
    {
        return AMG8833_ERROR_I2C;
    }

    if(AMG8833_CalcSummary(temp_x100, AMG8833_TEMP_THRESHOLD, &data->temp_summary) != AMG8833_OK)
    {
        return AMG8833_ERROR_PARAM;
    }

    data->valid = 1;

    return AMG8833_OK;
}
