/**
 * @file bsp_key_handler.c
 * @brief PA11 低电平有效数据采集按键的板级实现。
 */
#include "bsp_key_handler.h"

#include <stddef.h>
#include <stdint.h>

#include "main.h"

#define KEY_DEBOUNCE_TIME_MS 20U

/** @brief 标记按键 Handler 是否已经完成初始化。 */
static bool s_key_initialized;
static bool s_last_raw_pressed;
static bool s_press_reported;
static uint32_t s_last_change_tick;

key_handler_status_t bsp_key_handler_init(void)
{
    s_last_raw_pressed = (GPIO_PIN_RESET ==
                          HAL_GPIO_ReadPin(key_cap_GPIO_Port, key_cap_Pin));
    s_press_reported = s_last_raw_pressed;
    s_last_change_tick = HAL_GetTick();
    s_key_initialized = true;
    return KEY_HANDLER_OK;
}

key_handler_status_t bsp_key_handler_is_pressed(bool *pressed)
{
    if (NULL == pressed)
    {
        return KEY_HANDLER_ERROR_PARAMETER;
    }

    if (!s_key_initialized)
    {
        return KEY_HANDLER_ERROR_RESOURCE;
    }

    *pressed = false;

    const bool raw_pressed = (GPIO_PIN_RESET ==
                              HAL_GPIO_ReadPin(key_cap_GPIO_Port, key_cap_Pin));
    const uint32_t current_tick = HAL_GetTick();

    if (raw_pressed != s_last_raw_pressed)
    {
        s_last_raw_pressed = raw_pressed;
        s_last_change_tick = current_tick;
        return KEY_HANDLER_OK;
    }

    if ((current_tick - s_last_change_tick) < KEY_DEBOUNCE_TIME_MS)
    {
        return KEY_HANDLER_OK;
    }

    if (raw_pressed)
    {
        if (!s_press_reported)
        {
            s_press_reported = true;
            *pressed = true;
        }
    }
    else
    {
        s_press_reported = false;
    }

    return KEY_HANDLER_OK;
}
