#ifndef __BSP_GPIO_H
#define __BSP_GPIO_H

/*
 * 文件职责：
 * 1. 声明 GPIO 基础初始化和板载 LED、按键相关接口。
 * 2. 管理板载 LED 输出和 KEY1/KEY2 输入事件读取。
 * 3. KEY1/KEY2 的 EXTI 初始化和中断处理位于 bsp_gpio.c。
 *
 * 当前状态：
 * - 已提供板载 LED 初始化/开关/翻转和按键事件读取接口。
 */
#define LED_PORT GPIOB
#define D3  GPIO_Pin_5
#define D4  GPIO_Pin_0
#define D5  GPIO_Pin_1

#define KEY1_PORT GPIOA
#define KEY2_PORT GPIOC
#define KEY1_PIN GPIO_Pin_0
#define KEY2_PIN GPIO_Pin_13

void BSP_GPIO_LED_Init(void);
void BSP_GPIO_LED_On(uint16_t LED_D);
void BSP_GPIO_LED_Off(uint16_t LED_D);
void BSP_GPIO_LED_Toggle(uint16_t LED_D);

void BSP_GPIO_KEY_Init(void);
uint8_t BSP_GPIO_KEY_GetState(uint16_t KEY);

#endif /* __BSP_GPIO_H */
