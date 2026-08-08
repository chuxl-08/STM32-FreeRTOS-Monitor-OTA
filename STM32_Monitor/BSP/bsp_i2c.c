#include "stm32f10x.h"
#include "bsp_i2c.h"
#include <stdio.h>
#include "Delay.h"

/*
 * 文件职责：
 * 1. 实现 I2C1 底层配置。
 * 2. 统一管理 PB6/PB7 的 I2C 复用和总线速率。
 * 3. 为 OLED、AMG8833、AT24C02 等驱动提供基础通信能力。
 *
 * 当前状态：
 * - 已实现 I2C1 初始化和基础读写接口。
 * - 已实现设备就绪监测、事件等待、错误诊断和软恢复机制。
 * - 总线扫描上报。
 */
#define I2C1_Port               GPIOB
#define I2C1_RCC                RCC_APB2Periph_GPIOB
#define I2C1_SCL_PIN            GPIO_Pin_6    // 灰
#define I2C1_SDA_PIN            GPIO_Pin_7    // 紫

#define OLED_I2C_ADDRESS        0x3C
#define AMG8833_I2C_ADDRESS     0x69

#define I2C_RESPON_TIMEOUT_US   5000U    /* timeout 5ms */

/*
 * @brief   初始化 I2C1
 * @retval  BSP_I2C_Status_t:
 *          - BSP_I2C_OK: 初始化成功
 */
BSP_I2C_Status_t BSP_I2C_Init(void)
{
    RCC_APB2PeriphClockCmd(I2C1_RCC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = I2C1_SCL_PIN | I2C1_SDA_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C1_Port, &GPIO_InitStruct);

    I2C_InitTypeDef I2C_InitStruct;
    I2C_StructInit(&I2C_InitStruct);
    I2C_InitStruct.I2C_ClockSpeed = 100000; // 100 kHz
    I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStruct.I2C_OwnAddress1 = 0x00; // 主机不需要地址
    I2C_InitStruct.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &I2C_InitStruct);

    I2C_Cmd(I2C1, ENABLE);
    return BSP_I2C_OK;
}

/*
 * @brief   打印 I2C1 状态寄存器。
 * @param   tag: 状态标签。
 * @param   event: 当前等待的 I2C 事件。
 */
static void BSP_I2C_PrintState(const char *tag, uint32_t event)
{
    printf("[I2C][%s] event=0x%08lX SR1=0x%04lX SR2=0x%04lX CR1=0x%04lX\r\n",
           tag,
           (unsigned long)event,
           (unsigned long)I2C1->SR1,
           (unsigned long)I2C1->SR2,
           (unsigned long)I2C1->CR1);
}

static void BSP_I2C_TimeoutHandler(uint32_t event)
{
    BSP_I2C_PrintState("TIMEOUT", event);
}

/*
 * @brief   I2C1 软恢复。
 * @note    用于 I2C 事件等待失败后恢复 STM32F1 I2C 外设状态机。
 */
static void BSP_I2C_Recover(void)
{
    I2C_GenerateSTOP(I2C1, ENABLE);

    I2C_ClearFlag(I2C1, I2C_FLAG_AF);
    I2C_ClearFlag(I2C1, I2C_FLAG_BERR);
    I2C_ClearFlag(I2C1, I2C_FLAG_ARLO);
    I2C_ClearFlag(I2C1, I2C_FLAG_OVR);

    I2C_Cmd(I2C1, DISABLE);
    I2C_DeInit(I2C1);
    BSP_I2C_Init();

    printf("[I2C][RECOVER]\r\n");
}

/*
 * @brief   等待指定 I2C 事件发生，并在错误或超时后执行软恢复。
 * @param   event: 需要等待的 I2C 事件标志。
 * @retval  BSP_I2C_Status_t:
 *          - BSP_I2C_OK: 事件成功发生。
 *          - BSP_I2C_ERROR_TIMEOUT: 等待事件发生超时。
 *          - BSP_I2C_ERROR_NACK: 捕获 AF 应答失败。
 */
static BSP_I2C_Status_t BSP_I2C_WaitForEvent(uint32_t event)
{
    uint32_t timeout = I2C_RESPON_TIMEOUT_US;

    while(I2C_CheckEvent(I2C1, event) == ERROR)
    {
        if(I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) == SET)
        {
            BSP_I2C_PrintState("AF", event);
            I2C_ClearFlag(I2C1, I2C_FLAG_AF);
            BSP_I2C_Recover();
            return BSP_I2C_ERROR_NACK;
        }

        if(I2C_GetFlagStatus(I2C1, I2C_FLAG_BERR) == SET)
        {
            BSP_I2C_PrintState("BERR", event);
            I2C_ClearFlag(I2C1, I2C_FLAG_BERR);
            BSP_I2C_Recover();
            return BSP_I2C_ERROR_TIMEOUT;
        }

        if(I2C_GetFlagStatus(I2C1, I2C_FLAG_ARLO) == SET)
        {
            BSP_I2C_PrintState("ARLO", event);
            I2C_ClearFlag(I2C1, I2C_FLAG_ARLO);
            BSP_I2C_Recover();
            return BSP_I2C_ERROR_TIMEOUT;
        }

        if(I2C_GetFlagStatus(I2C1, I2C_FLAG_OVR) == SET)
        {
            BSP_I2C_PrintState("OVR", event);
            I2C_ClearFlag(I2C1, I2C_FLAG_OVR);
            BSP_I2C_Recover();
            return BSP_I2C_ERROR_TIMEOUT;
        }

        Delay_us(1);

        if(timeout-- == 0)
        {
            BSP_I2C_TimeoutHandler(event);
            BSP_I2C_Recover();
            return BSP_I2C_ERROR_TIMEOUT;
        }
    }

    return BSP_I2C_OK;
}

/*
 * @brief   向 I2C 设备写入一个字节数据。
 * @param   deviceAddress: 设备的 7 位 I2C 地址。
 * @param   registerAddress: 目标寄存器地址。
 * @param   data: 要写入的数据字节。
 * @retval  BSP_I2C_Status_t:
 *          - BSP_I2C_OK: 写入成功。
 *          - BSP_I2C_ERROR_TIMEOUT: 通信超时。
 *          - BSP_I2C_ERROR_NACK: 设备未响应或拒绝应答。
 */
BSP_I2C_Status_t BSP_I2C_WriteByte(uint8_t deviceAddress, uint8_t registerAddress, uint8_t data)
{
    BSP_I2C_Status_t wait_status;

    I2C_GenerateSTART(I2C1, ENABLE);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_MODE_SELECT) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }

    I2C_Send7bitAddress(I2C1, deviceAddress << 1, I2C_Direction_Transmitter);
    wait_status = BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);
    if(wait_status != BSP_I2C_OK)
    {
        printf("[I2C][WRITE_ADDR_FAIL] dev=0x%02X reg=0x%02X data=0x%02X\r\n",
               deviceAddress,
               registerAddress,
               data);
        return wait_status;
    }

    I2C_SendData(I2C1, registerAddress);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTING) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }

    I2C_SendData(I2C1, data);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }
    else
    {
        I2C_GenerateSTOP(I2C1, ENABLE);
        return BSP_I2C_OK;
    }
}


/*
 * @brief   向 I2C 设备写入多个字节数据。
 * @param   deviceAddress: 设备的 7 位 I2C 地址。
 * @param   registerAddress: 目标寄存器地址。
 * @param   data: 要写入的数据字节。
 * @param   bytes_num: 数据字节数。
 * @retval  BSP_I2C_Status_t:
 *          - BSP_I2C_OK: 写入成功。
 *          - BSP_I2C_ERROR_TIMEOUT: 通信超时。
 *          - BSP_I2C_ERROR_NACK: 设备未响应或拒绝应答。
 */
BSP_I2C_Status_t BSP_I2C_WriteBytes(uint8_t deviceAddress, uint8_t registerAddress, const uint8_t *data, uint16_t bytes_num)
{
    uint16_t index;
    BSP_I2C_Status_t wait_status;

    if((data == 0) || (bytes_num == 0))
    {
        return BSP_I2C_ERROR_PARAM;
    }

    I2C_GenerateSTART(I2C1, ENABLE);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_MODE_SELECT) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }

    I2C_Send7bitAddress(I2C1, deviceAddress << 1, I2C_Direction_Transmitter);
    wait_status = BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);
    if(wait_status != BSP_I2C_OK)
    {
        printf("[I2C][WRITES_ADDR_FAIL] dev=0x%02X reg=0x%02X len=%u\r\n",
               deviceAddress,
               registerAddress,
               bytes_num);
        return wait_status;
    }

    I2C_SendData(I2C1, registerAddress);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTING) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }

    for(index = 0; index < bytes_num; index++)
    {
        I2C_SendData(I2C1, data[index]);
        if(index == (bytes_num - 1))
        {
            wait_status = BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED);
        }
        else
        {
            wait_status = BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTING);
        }

        if(wait_status != BSP_I2C_OK)
        {
            printf("[I2C][WRITES_DATA_FAIL] dev=0x%02X reg=0x%02X index=%u len=%u\r\n",
                   deviceAddress,
                   registerAddress,
                   index,
                   bytes_num);
            return wait_status;
        }
    }

    I2C_GenerateSTOP(I2C1, ENABLE);
    return BSP_I2C_OK;
}

/*
 * @brief   从 I2C 设备读取一个字节数据。
 * @param   deviceAddress: 设备的 7 位 I2C 地址。
 * @param   registerAddress: 目标寄存器地址。
 * @param   data: 指向存储读取数据的指针。
 * @retval  BSP_I2C_Status_t:
 *          - BSP_I2C_OK: 读取成功。
 *          - BSP_I2C_ERROR_PARAM: 参数错误（如无效地址）。
 *          - BSP_I2C_ERROR_TIMEOUT: 通信超时。
 *          - BSP_I2C_ERROR_NACK: 设备未响应或拒绝应答。
 */
BSP_I2C_Status_t BSP_I2C_ReadByte(uint8_t deviceAddress, uint8_t registerAddress, uint8_t *data)
{
    BSP_I2C_Status_t wait_status;

    if(data == 0)
    {
        return BSP_I2C_ERROR_PARAM;
    }

    I2C_GenerateSTART(I2C1, ENABLE);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_MODE_SELECT) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }

    I2C_Send7bitAddress(I2C1, deviceAddress << 1, I2C_Direction_Transmitter);
    wait_status = BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);
    if(wait_status != BSP_I2C_OK)
    {
        printf("[I2C][READ_REG_ADDR_FAIL] dev=0x%02X reg=0x%02X\r\n",
               deviceAddress,
               registerAddress);

        return wait_status;
    }

    I2C_SendData(I2C1, registerAddress);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTING) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }


    I2C_GenerateSTART(I2C1, ENABLE);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_MODE_SELECT) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }

    I2C_Send7bitAddress(I2C1, deviceAddress << 1, I2C_Direction_Receiver);
    wait_status = BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED);
    if(wait_status != BSP_I2C_OK)
    {
        printf("[I2C][READ_DATA_ADDR_FAIL] dev=0x%02X reg=0x%02X\r\n",
               deviceAddress,
               registerAddress);

        return wait_status;
    }

    /* 应答在最后一个字节之前被禁用，以便正确结束通信。 */
    I2C_AcknowledgeConfig(I2C1, DISABLE);
    I2C_GenerateSTOP(I2C1, ENABLE);

    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_BYTE_RECEIVED) != BSP_I2C_OK)
    {
        I2C_AcknowledgeConfig(I2C1, ENABLE);
        return BSP_I2C_ERROR_REC_BYTE;
    }
    else
    {
        *data = I2C_ReceiveData(I2C1);
        I2C_AcknowledgeConfig(I2C1, ENABLE);
        return BSP_I2C_OK;
    }
}

/*
 * @brief   从 I2C 设备读取多个字节数据。
 * @param   deviceAddress: 设备的 7 位 I2C 地址。
 * @param   registerAddress: 目标寄存器地址。
 * @param   arr: 指向存储读取数据数组的指针。
 * @param   bytes_num: 读取字节的数量
 * @retval  BSP_I2C_Status_t:
 *          - BSP_I2C_OK: 读取成功。
 *          - BSP_I2C_ERROR_PARAM: 参数错误。
 *          - BSP_I2C_ERROR_TIMEOUT: 通信超时。
 *          - BSP_I2C_ERROR_NACK: 设备未响应或拒绝应答。
 */
BSP_I2C_Status_t BSP_I2C_ReadBytes(uint8_t deviceAddress, uint8_t registerAddress, uint8_t *arr, uint16_t bytes_num)
{
    uint16_t index;
    BSP_I2C_Status_t wait_status;

    if(arr == 0 || bytes_num == 0)
    {
        return BSP_I2C_ERROR_PARAM;
    }

    I2C_GenerateSTART(I2C1, ENABLE);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_MODE_SELECT) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }

    I2C_Send7bitAddress(I2C1, deviceAddress << 1, I2C_Direction_Transmitter);
    wait_status = BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);
    if(wait_status != BSP_I2C_OK)
    {
        printf("[I2C][READS_REG_ADDR_FAIL] dev=0x%02X reg=0x%02X len=%u\r\n",
               deviceAddress,
               registerAddress,
               bytes_num);

        return wait_status;
    }

    I2C_SendData(I2C1, registerAddress);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTING) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }

    I2C_GenerateSTART(I2C1, ENABLE);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_MODE_SELECT) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }

    I2C_Send7bitAddress(I2C1, deviceAddress << 1, I2C_Direction_Receiver);
    wait_status = BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED);
    if(wait_status != BSP_I2C_OK)
    {
        printf("[I2C][READS_DATA_ADDR_FAIL] dev=0x%02X reg=0x%02X len=%u\r\n",
               deviceAddress,
               registerAddress,
               bytes_num);

        return wait_status;
    }

    for(index = 0; index < bytes_num; index ++)
    {
        if(index == bytes_num - 1)
        {
            /* 应答在最后一个字节之前被禁用，以便正确结束通信。 */
            I2C_AcknowledgeConfig(I2C1, DISABLE);
            I2C_GenerateSTOP(I2C1, ENABLE);
        }

        if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_BYTE_RECEIVED) != BSP_I2C_OK)
        {
            I2C_AcknowledgeConfig(I2C1, ENABLE);
            return BSP_I2C_ERROR_REC_BYTE;
        }

        arr[index] = (uint8_t)I2C_ReceiveData(I2C1);
    }

    I2C_AcknowledgeConfig(I2C1, ENABLE);
    return BSP_I2C_OK;
}

/*
 * @brief   检测 I2C 设备是否准备就绪。
 * @param   deviceAddress: 设备的 7 位 I2C 地址。
 * @retval  BSP_I2C_Status_t:
 *          - BSP_I2C_OK: 设备准备就绪。
 *          - BSP_I2C_ERROR_TIMEOUT: 通信超时。
 *          - BSP_I2C_ERROR_NACK: 设备未响应或拒绝应答。
 */
BSP_I2C_Status_t BSP_I2C_IsDeviceReady(uint8_t deviceAddress)
{
    BSP_I2C_Status_t wait_status;

    I2C_GenerateSTART(I2C1, ENABLE);
    if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_MODE_SELECT) != BSP_I2C_OK)
    {
        return BSP_I2C_ERROR_TIMEOUT;
    }

    I2C_Send7bitAddress(I2C1, deviceAddress << 1, I2C_Direction_Transmitter);
    wait_status = BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);
    if(wait_status != BSP_I2C_OK)
    {
        printf("[I2C][READY_ADDR_FAIL] dev=0x%02X\r\n", deviceAddress);
        return wait_status;
    }
    else
    {
        I2C_GenerateSTOP(I2C1, ENABLE);
        return BSP_I2C_OK;
    }
}

/*
 * @brief   扫描 I2C 总线上的设备并打印地址（测试用）
 * @retval  None
 */
void BSP_I2C_ScanBus(void)
{
    printf("Scanning I2C bus for devices...\r\n");
    // 实现 I2C 设备扫描功能，检测总线上连接的设备地址
    // 通过尝试发送 START 条件和地址来判断设备是否存在
    for (uint8_t address = 1; address < 128; address++)
    {
        I2C_GenerateSTART(I2C1, ENABLE);
        BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_MODE_SELECT);

        I2C_Send7bitAddress(I2C1, address << 1, I2C_Direction_Transmitter);

        // 等待地址发送完成，检查是否有设备响应
        if(BSP_I2C_WaitForEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) == BSP_I2C_OK)
        {
            // 设备存在，记录地址
            if(address == OLED_I2C_ADDRESS)
            {
                printf("OLED detected at address: 0x%02X\r\n", address);
            }
            else if(address == AMG8833_I2C_ADDRESS)
            {
                printf("AMG8833 detected at address: 0x%02X\r\n", address);
            }
            else
            {
                printf("Unknown device detected at address: 0x%02X\r\n", address);
            }
        }
        else if(I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) != RESET)
        {
            // 没有设备响应，继续扫描
            printf("No device at address: 0x%02X\r\n", address);
            I2C_ClearFlag(I2C1, I2C_FLAG_AF); // 清除应答失败标志
        }
        I2C_GenerateSTOP(I2C1, ENABLE);
    }
    printf("I2C bus scan complete.\r\n");
}


