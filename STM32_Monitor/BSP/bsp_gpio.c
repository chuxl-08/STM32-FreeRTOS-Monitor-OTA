#include "stm32f10x.h"                  // Device header
#include "bsp_gpio.h"

/*
 * 文件职责：
 * 1. 实现 GPIO 底层配置。
 * 2. 封装板载 LED、普通 GPIO 输入输出和 EXTI 引脚初始化。
 * 3. 为 DHT11、HC-SR04 Trig、WiFi RST、编码器按键等模块提供 GPIO 支撑。
 *
 * 当前状态：
 * - 已完成板载 LED 的初始化和控制函数实现。
 * - 已完成 KEY1 和 KEY2 的输入配置和状态获取函数实现。
 * - 已配置 KEY1 和 KEY2 的 EXTI 中断，并在中断服务程序中更新按键状态。
 */

 void BSP_GPIO_LED_Init(void)
 {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = D3 | D4 | D5;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(LED_PORT, &GPIO_InitStructure);
 }

void BSP_GPIO_LED_On(uint16_t LED_D)
{
    GPIO_ResetBits(LED_PORT, LED_D);
}

void BSP_GPIO_LED_Off(uint16_t LED_D)
{
    GPIO_SetBits(LED_PORT, LED_D);
}

void BSP_GPIO_LED_Toggle(uint16_t LED_D)
{
    if (GPIO_ReadOutputDataBit(LED_PORT, LED_D) == Bit_SET)
    {
        GPIO_ResetBits(LED_PORT, LED_D);
    }
    else
    {
        GPIO_SetBits(LED_PORT, LED_D);
    }
}

void BSP_GPIO_KEY_Init(void)
{
    // 使能 AFIO 时钟  
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Pin = KEY1_PIN;
    GPIO_Init(KEY1_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = KEY2_PIN;
    GPIO_Init(KEY2_PORT, &GPIO_InitStructure);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource13);

    EXTI_InitTypeDef EXTI_InitStructure;
    EXTI_InitStructure.EXTI_Line = EXTI_Line0;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_Line = EXTI_Line13;
    EXTI_Init(&EXTI_InitStructure);

    EXTI_ClearITPendingBit(EXTI_Line0);
    EXTI_ClearITPendingBit(EXTI_Line13);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 12;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 13;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&NVIC_InitStructure);
}

volatile uint8_t KEY1_State;
volatile uint8_t KEY2_State;

uint8_t BSP_GPIO_KEY_GetState(uint16_t KEY)
{
    if (KEY == KEY1_PIN)
    {
        if(KEY1_State)
        {
            KEY1_State = 0;
            return 1;
        }
        return 0;
    }
    if (KEY == KEY2_PIN)
    {
        if(KEY2_State)
        {
            KEY2_State = 0;
            return 1;
        }
        return 0;
    }
    return 0;
}

void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        // 处理 KEY1 按键事件
        KEY1_State = 1;
        EXTI_ClearITPendingBit(EXTI_Line0);
    }
}

void EXTI15_10_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line13) != RESET)
    {
        // 处理 KEY2 按键事件
        KEY2_State = 1;
        EXTI_ClearITPendingBit(EXTI_Line13);
    }
}

 
 
 
