#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"

#define DELAY_CPU_CLOCK_MHZ        72U
#define DELAY_DWT_CTRL             (*(volatile uint32_t *)0xE0001000UL)
#define DELAY_DWT_CYCCNT           (*(volatile uint32_t *)0xE0001004UL)
#define DELAY_DWT_CYCCNTENA_Msk    0x00000001UL
#define DELAY_DEMCR                (*(volatile uint32_t *)0xE000EDFCUL)
#define DELAY_DEMCR_TRCENA_Msk     0x01000000UL

static void Delay_DWTInit(void)
{
    if((DELAY_DWT_CTRL & DELAY_DWT_CYCCNTENA_Msk) == 0)
    {
        DELAY_DEMCR |= DELAY_DEMCR_TRCENA_Msk;
        DELAY_DWT_CYCCNT = 0;
        DELAY_DWT_CTRL |= DELAY_DWT_CYCCNTENA_Msk;
    }
}

/**
  * @brief  微秒级延时
  * @param  xus 延时时长，范围：0~233015
  * @retval 无
  */
void Delay_us(uint32_t xus)
{
    uint32_t start_count;
    uint32_t delay_ticks;

    Delay_DWTInit();

    start_count = DELAY_DWT_CYCCNT;
    delay_ticks = xus * DELAY_CPU_CLOCK_MHZ;

    while((uint32_t)(DELAY_DWT_CYCCNT - start_count) < delay_ticks)
    {
    }
}

/**
  * @brief  毫秒级延时
  * @param  xms 延时时长，范围：0~4294967295
  * @retval 无
  */
void Delay_ms(uint32_t xms)
{
    if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(xms));
        return;
    }

    while(xms--)
    {
        Delay_us(1000);
    }
}
 
/**
  * @brief  秒级延时
  * @param  xs 延时时长，范围：0~4294967295
  * @retval 无
  */
void Delay_s(uint32_t xs)
{
    while(xs--)
    {
        Delay_ms(1000);
    }
} 
