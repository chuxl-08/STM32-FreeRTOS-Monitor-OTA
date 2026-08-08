#ifndef __BSP_I2C_H
#define __BSP_I2C_H
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明 I2C1 底层初始化和基础读写接口。
 * 2. 为 OLED 和 AMG8833 等 I2C 设备提供寄存器读写、就绪检测和总线扫描接口。
 * 3. 统一暴露超时、NACK 和接收字节错误状态。
 *
 * 当前状态：
 * - 已实现 I2C1 初始化、单字节/多字节读写、设备就绪检测和扫描接口。
 */

BSP_I2C_Status_t BSP_I2C_Init(void);
BSP_I2C_Status_t BSP_I2C_WriteByte(uint8_t deviceAddress, uint8_t registerAddress, uint8_t data);
BSP_I2C_Status_t BSP_I2C_WriteBytes(uint8_t deviceAddress, uint8_t registerAddress, const uint8_t *data, uint16_t bytes_num);
BSP_I2C_Status_t BSP_I2C_ReadByte(uint8_t deviceAddress, uint8_t registerAddress, uint8_t *data);
BSP_I2C_Status_t BSP_I2C_ReadBytes(uint8_t deviceAddress, uint8_t registerAddress, uint8_t *arr, uint16_t bytes_num);
BSP_I2C_Status_t BSP_I2C_IsDeviceReady(uint8_t deviceAddress);
void BSP_I2C_ScanBus(void);

#endif /* __BSP_I2C_H */
