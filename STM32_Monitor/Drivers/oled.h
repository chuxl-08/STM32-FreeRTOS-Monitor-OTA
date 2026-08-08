#ifndef __OLED_H
#define __OLED_H
#include "error_code.h"

/*
 * 文件职责：
 * 1. 声明 0.96 寸四针 I2C OLED 驱动接口。
 * 2. 负责 SSD1306 就绪检测、初始化、命令发送、数据发送和文本显示。
 * 3. 页面组织由 App/app_display 负责，本文件只处理底层显示能力。
 *
 * 当前状态：
 * - 已提供清屏、定位、字符/字符串/数字显示和固定行刷新接口。
 */

#define OLED_I2C_ADDRESS 0x3C

OLED_Status_t OLED_IsReady(void);

OLED_Status_t OLED_SetCursor(uint8_t Y, uint8_t X);
OLED_Status_t OLED_Clear(void);
OLED_Status_t OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
OLED_Status_t OLED_ShowString(uint8_t Line, uint8_t Column, const char *String);
OLED_Status_t OLED_ShowLineString(uint8_t Line, const char *String);
OLED_Status_t OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
OLED_Status_t OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
OLED_Status_t OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
OLED_Status_t OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
OLED_Status_t OLED_Init(void);


#endif /* __OLED_H */
