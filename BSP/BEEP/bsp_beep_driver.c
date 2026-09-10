/**
 * @file bsp_beep_driver.c
 * @brief STM32G474 PB9 低电平有效有源蜂鸣器驱动实现。
 */
#include "bsp_beep_driver.h"

#include "main.h"

/** @brief 标记蜂鸣器 Driver 是否已经完成初始化。 */
static bool s_beep_initialized;

beep_driver_status_t bsp_beep_driver_init(void)
{
    /* 必须先输出无效高电平，避免任务启动前或异常恢复时持续鸣叫。 */
    HAL_GPIO_WritePin(beep_GPIO_Port, beep_Pin, GPIO_PIN_SET);
    s_beep_initialized = true;
    return BEEP_DRIVER_OK;
}

beep_driver_status_t bsp_beep_driver_set(bool active)
{
    if (!s_beep_initialized)
    {
        return BEEP_DRIVER_ERROR_RESOURCE;
    }

    HAL_GPIO_WritePin(beep_GPIO_Port,
                      beep_Pin,
                      active ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return BEEP_DRIVER_OK;
}
