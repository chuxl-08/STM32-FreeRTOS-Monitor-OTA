#include "stm32f10x.h"
#include "oled.h"
#include "oled_font.h"
#include "bsp_i2c.h"
#include "Delay.h"

/*
 * 文件职责：
 * 1. 实现 OLED SSD1306 I2C 驱动。
 * 2. 处理设备就绪检测、初始化序列、清屏、定位、字符和字符串显示。
 * 3. 通过 BSP I2C 接口访问 PB6/PB7 总线，并对单字节/批量写入做有限重试。
 * 4. 提供固定 16 字符行刷新接口，降低显示任务的整行刷新耗时。
 *
 * 当前状态：
 * - 已实现初始化、清屏、字符/字符串/数字显示和整行批量刷新。
 * - 页面组织由 App/app_display.c 负责，本文件只提供底层显示能力。
 */


#define OLED_RETURN_IF_ERROR(expr)             \
          do                                    \
          {                                     \
            OLED_Status_t status = (expr);     \
            if(status != OLED_OK)               \
            {                                   \
              return status;                    \
            }                                   \
          } while (0)

#define OLED_I2C_RETRY_COUNT          3
#define OLED_I2C_RETRY_DELAY_MS       2
#define OLED_READY_RETRY_COUNT        5
#define OLED_READY_RETRY_DELAY_MS     20
#define OLED_LINE_BYTE_COUNT          128

OLED_Status_t OLED_IsReady(void)
{
    uint8_t retry_index;

    for(retry_index = 0; retry_index < OLED_READY_RETRY_COUNT; retry_index++)
    {
        BSP_I2C_Init();
        if(BSP_I2C_IsDeviceReady(OLED_I2C_ADDRESS) == BSP_I2C_OK)
        {
            return OLED_OK;
        }
        Delay_ms(OLED_READY_RETRY_DELAY_MS);
    }

    return OLED_ERROR_INIT;
}

static OLED_Status_t OLED_WriteByteWithRetry(uint8_t control_byte, uint8_t data)
{
    uint8_t retry_index;

    for(retry_index = 0; retry_index < OLED_I2C_RETRY_COUNT; retry_index++)
    {
        if(BSP_I2C_WriteByte(OLED_I2C_ADDRESS, control_byte, data) == BSP_I2C_OK)
        {
            return OLED_OK;
        }
        Delay_ms(OLED_I2C_RETRY_DELAY_MS);
    }

    return OLED_ERROR_I2C;
}

static OLED_Status_t OLED_WriteCommand(uint8_t command)
{
    return OLED_WriteByteWithRetry(0x00, command);
}

static OLED_Status_t OLED_WriteData(uint8_t data)
{
    return OLED_WriteByteWithRetry(0x40, data);
}

static OLED_Status_t OLED_WriteDataBufferWithRetry(const uint8_t *data, uint16_t length)
{
    uint8_t retry_index;

    if((data == 0) || (length == 0))
    {
        return OLED_ERROR_PARAM;
    }

    for(retry_index = 0; retry_index < OLED_I2C_RETRY_COUNT; retry_index++)
    {
        if(BSP_I2C_WriteBytes(OLED_I2C_ADDRESS, 0x40, data, length) == BSP_I2C_OK)
        {
            return OLED_OK;
        }
        Delay_ms(OLED_I2C_RETRY_DELAY_MS);
    }

    return OLED_ERROR_I2C;
}

static char OLED_NormalizeChar(const char *String, uint8_t char_index, uint8_t *string_end)
{
    char current_char;

    if(*string_end)
    {
        current_char = ' ';
    }
    else
    {
        current_char = String[char_index];
    }

    if(current_char == '\0')
    {
        current_char = ' ';
        *string_end = 1;
    }

    if((current_char < ' ') || (current_char > '~'))
    {
        current_char = ' ';
    }

    return current_char;
}

/**
  * @brief  OLED设置光标位置
  * @param  Y 以左上角为原点，向下方向的坐标，范围：0~7
  * @param  X 以左上角为原点，向右方向的坐标，范围：0~127
  * @retval OLED_Status_t
  */
OLED_Status_t OLED_SetCursor(uint8_t Y, uint8_t X)
{
	OLED_RETURN_IF_ERROR(OLED_WriteCommand(0xB0 | Y));				//设置Y位置
	OLED_RETURN_IF_ERROR(OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4)));	//设置X位置高4位
	OLED_RETURN_IF_ERROR(OLED_WriteCommand(0x00 | (X & 0x0F)));			//设置X位置低4位

  return OLED_OK;
}

/**
  * @brief  OLED清屏
  * @param  无
  * @retval OLED_Status_t
  */
OLED_Status_t OLED_Clear(void)
{  
	uint8_t j;
	static const uint8_t zero_line[OLED_LINE_BYTE_COUNT] = {0};

	for (j = 0; j < 8; j++)
	{
		OLED_RETURN_IF_ERROR(OLED_SetCursor(j, 0));
		OLED_RETURN_IF_ERROR(OLED_WriteDataBufferWithRetry(zero_line, OLED_LINE_BYTE_COUNT));
	}

  return OLED_OK;
}

/**
  * @brief  OLED显示一个字符
  * @param  Line 行位置，范围：1~4
  * @param  Column 列位置，范围：1~16
  * @param  Char 要显示的一个字符，范围：ASCII可见字符
  * @retval OLED_Status_t
  */
OLED_Status_t OLED_ShowChar(uint8_t Line, uint8_t Column,char Char)
{      	
	uint8_t i;
	OLED_RETURN_IF_ERROR(OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8));		//设置光标位置在上半部分
	for (i = 0; i < 8; i++)
	{
		OLED_RETURN_IF_ERROR(OLED_WriteData(OLED_F8x16[Char - ' '][i]));			//显示上半部分内容
	}
	OLED_RETURN_IF_ERROR(OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8));	//设置光标位置在下半部分
	for (i = 0; i < 8; i++)
	{
		OLED_RETURN_IF_ERROR(OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]));		//显示下半部分内容
	}

  return OLED_OK;
}

/**
  * @brief  OLED显示字符串
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的字符串，范围：ASCII可见字符
  * @retval OLED_Status_t
  */
OLED_Status_t OLED_ShowString(uint8_t Line, uint8_t Column, const char *String)
{
  if(String == 0)
  {
    return OLED_ERROR_PARAM;
  }

	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		OLED_RETURN_IF_ERROR(OLED_ShowChar(Line, Column + i, String[i]));
	}

  return OLED_OK;
}

OLED_Status_t OLED_ShowLineString(uint8_t Line, const char *String)
{
  uint8_t char_index;
  uint8_t byte_index;
  uint8_t string_end;
  uint8_t line_buffer[OLED_LINE_BYTE_COUNT];
  char current_char;

  if((String == 0) || (Line < 1) || (Line > 4))
  {
    return OLED_ERROR_PARAM;
  }

  OLED_RETURN_IF_ERROR(OLED_SetCursor((Line - 1) * 2, 0));
  string_end = 0;
  for(char_index = 0; char_index < 16; char_index++)
  {
    current_char = OLED_NormalizeChar(String, char_index, &string_end);
    for(byte_index = 0; byte_index < 8; byte_index++)
    {
      line_buffer[char_index * 8 + byte_index] = OLED_F8x16[current_char - ' '][byte_index];
    }
  }
  OLED_RETURN_IF_ERROR(OLED_WriteDataBufferWithRetry(line_buffer, OLED_LINE_BYTE_COUNT));

  OLED_RETURN_IF_ERROR(OLED_SetCursor((Line - 1) * 2 + 1, 0));
  string_end = 0;
  for(char_index = 0; char_index < 16; char_index++)
  {
    current_char = OLED_NormalizeChar(String, char_index, &string_end);
    for(byte_index = 0; byte_index < 8; byte_index++)
    {
      line_buffer[char_index * 8 + byte_index] = OLED_F8x16[current_char - ' '][byte_index + 8];
    }
  }
  OLED_RETURN_IF_ERROR(OLED_WriteDataBufferWithRetry(line_buffer, OLED_LINE_BYTE_COUNT));

  return OLED_OK;
}

/**
  * @brief  OLED次方函数
  * @retval 返回值等于X的Y次方
  */
static uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
  * @brief  OLED显示数字（十进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~4294967295
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval OLED_Status_t
  */
OLED_Status_t OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)							
	{
		OLED_RETURN_IF_ERROR(OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0'));
	}

  return OLED_OK;
}


/**
  * @brief  OLED显示数字（十进制，带符号数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：-2147483648~2147483647
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval OLED_Status_t
  */
OLED_Status_t OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t Number1;
	if (Number >= 0)
	{
		OLED_RETURN_IF_ERROR(OLED_ShowChar(Line, Column, '+'));
		Number1 = Number;
	}
	else
	{
		OLED_RETURN_IF_ERROR(OLED_ShowChar(Line, Column, '-'));
		Number1 = -Number;
	}
	for (i = 0; i < Length; i++)							
	{
		OLED_RETURN_IF_ERROR(OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0'));
	}

  return OLED_OK;
}

/**
  * @brief  OLED显示数字（十六进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~0xFFFFFFFF
  * @param  Length 要显示数字的长度，范围：1~8
  * @retval OLED_Status_t
  */
OLED_Status_t OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i, SingleNumber;
	for (i = 0; i < Length; i++)							
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		if (SingleNumber < 10)
		{
			OLED_RETURN_IF_ERROR(OLED_ShowChar(Line, Column + i, SingleNumber + '0'));
		}
		else
		{
			OLED_RETURN_IF_ERROR(OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A'));
		}
	}

  return OLED_OK;
}

/**
  * @brief  OLED显示数字（二进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~1111 1111 1111 1111
  * @param  Length 要显示数字的长度，范围：1~16
  * @retval OLED_Status_t
  */
OLED_Status_t OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)							
	{
		OLED_RETURN_IF_ERROR(OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0'));
	}
  return OLED_OK;
}

/**
  * @brief  OLED批量写命令表
  * @param  commands  命令表数组指针
  * @param  length 命令表数组长度
  * @retval OLED_Status_t
  */
static OLED_Status_t OLED_WriteCommandList(const uint8_t *commands, uint16_t length)
{
    uint16_t i;

    if(commands == 0)
    {
        return OLED_ERROR_PARAM;
    }

    for(i = 0; i < length; i++)
    {
        OLED_RETURN_IF_ERROR(OLED_WriteCommand(commands[i]));
    }

    return OLED_OK;
}

/**
  * @brief  OLED初始化
  * @param  无
  * @retval OLED_Status_t
  *         
  */
OLED_Status_t OLED_Init(void)
{
    // 上电稳定延时
    Delay_ms(100);
    if(OLED_IsReady() != OLED_OK)
    {
        return OLED_ERROR_INIT;
    }

    static const uint8_t oled_init_commands[] =
    {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0xCF,
        0xD9, 0xF1,
        0xDB, 0x30,
        0xA4,
        0xA6,
        0x8D, 0x14,
        0xAF
    };

    // 批量执行OLED 初始化序列
    OLED_RETURN_IF_ERROR(OLED_WriteCommandList(oled_init_commands, sizeof(oled_init_commands)));
    
    Delay_ms(100); // 等待 OLED 稳定

    OLED_RETURN_IF_ERROR(OLED_Clear());				//OLED清屏

    return OLED_OK;
}







