#include "stm32f10x.h"                  // Device header
#include "bsp_adc.h"
#include "Delay.h"
#include <stdio.h>

/*
 * 文件职责：
 * 1. 实现 ADC1 单通道采样的底层配置。
 * 2. 当前固定采集 PC0 / ADC1_IN10，用于读取光敏模块 AO 模拟输出。
 * 3. 提供 ADC 初始化、校准和软件触发单次转换接口。
 * 4. 本层返回 ADC 原始值和硬件级状态码
 *
 * 当前状态：
 * - ADC1 独立模式、单通道、单次转换、软件触发、右对齐。
 * - ADC 时钟配置为 PCLK2 / 6，即 12 MHz，满足 ADC 时钟 < 14 MHz 的要求。
 * - 通道采样时间为 55.5 cycles，单次转换总周期约为 55.5 + 12.5 = 68 cycles。
 * - 初始化阶段执行 ADC 复位校准和自校准，并带有超时保护。
 * - 读取阶段等待 EOC 标志置位，并通过输出参数返回 0~4095 的 ADC 原始值。
 */

 #define ADC1_GPIO_RCC              RCC_APB2Periph_GPIOC
 #define ADC1_PORT                  GPIOC
 #define ADC1_PIN                   GPIO_Pin_0
 #define ADC1_RCC                   RCC_APB2Periph_ADC1

 #define ADC_CALIB_TIMEOUT_US       5000  // 校准时间 us
 #define ADC_CONV_TIMEOUT_US        1000   // 转换时间 us


/**
  * @brief  执行 ADC 复位校准和自校准。
  * @param  ADCx: 需要校准的 ADC 外设指针，当前实际使用 ADC1。
  * @retval BSP_ADC_Status_t:
  *         - BSP_ADC_OK：校准完成。
  *         - BSP_ADC_ERROR_TIMEOUT：等待复位校准或自校准完成超时。
  * @note   
  */
 static BSP_ADC_Status_t BSP_ADC_Calibrate(ADC_TypeDef* ADCx)
 {
    uint16_t timeout = ADC_CALIB_TIMEOUT_US;
    ADC_ResetCalibration(ADCx);
    while(ADC_GetResetCalibrationStatus(ADCx) == SET)
    {
        Delay_us(1);
        if(timeout-- == 0)
        {
            return BSP_ADC_ERROR_TIMEOUT;
        }
    }

    timeout = ADC_CALIB_TIMEOUT_US;
    ADC_StartCalibration(ADCx);
    while(ADC_GetCalibrationStatus(ADCx) == SET)
    {
        Delay_us(1);
        if(timeout-- == 0)
        {
            return BSP_ADC_ERROR_TIMEOUT;
        }
    }
    return BSP_ADC_OK;
 }

/**
  * @brief  初始化 ADC1 及其对应 GPIO。
  * @param  None
  * @retval BSP_ADC_Status_t:
  *         - BSP_ADC_OK：ADC1 初始化和校准成功。
  *         - BSP_ADC_ERROR_CALIBRATION：ADC 校准失败。
  * @note   当前配置用于 PC0 / ADC1_IN10：
  *         - GPIOC Pin0 配置为模拟输入。
  *         - ADC1 使用独立模式、单通道、单次转换。
  *         - 使用软件触发转换，不开启连续转换和扫描模式。
  */
 BSP_ADC_Status_t BSP_ADC_Init(void)
 {
    RCC_APB2PeriphClockCmd(ADC1_GPIO_RCC | ADC1_RCC, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);   // PCLK2 / 6 = 12MHz < 14MHz; 1cycle = 1/12M = 0.0833us

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_InitStructure.GPIO_Pin = ADC1_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ADC1_PORT, &GPIO_InitStructure);

    ADC_InitTypeDef ADC_InitStructure;
    ADC_StructInit(&ADC_InitStructure);
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_RegularChannelConfig(ADC1, ADC_Channel_10, 1, ADC_SampleTime_55Cycles5);    // 55.5+12.5=68cycles 

    ADC_Cmd(ADC1, ENABLE);

    // 校准
    if(BSP_ADC_Calibrate(ADC1) != BSP_ADC_OK)
    {
        return BSP_ADC_ERROR_CALIBRATION;
    }
    return BSP_ADC_OK;
}

/**
  * @brief  触发 ADC1 单次转换并读取原始采样值。
  * @param  adc_value: 输出参数，用于保存 ADC 原始值，范围 0~4095。
  * @retval BSP_ADC_Status_t:
  *         - BSP_ADC_OK：转换成功，adc_value 数据有效。
  *         - BSP_ADC_ERROR_PARAM：adc_value 为空指针。
  *         - BSP_ADC_ERROR_CONVERSION：等待转换完成超时。
  * @note   返回 ADC 原始值。
  */
BSP_ADC_Status_t BSP_ADC_Read(uint16_t* adc_value)
{
    uint16_t timeout = ADC_CONV_TIMEOUT_US;
    uint16_t temp;
    if(adc_value == 0)
    {
        return BSP_ADC_ERROR_PARAM;
    }

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
    {
        Delay_us(1);
        if(timeout-- == 0)
        {
            return BSP_ADC_ERROR_CONVERSION; // 返回错误值
        }
    }

    *adc_value = ADC_GetConversionValue(ADC1);
    return BSP_ADC_OK; // 返回成功值
}
