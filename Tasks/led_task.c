/**
 * @file led_task.c
 * @brief LED上电自检及按键蓝灯、蜂鸣器反馈任务。
 */
#include "led_task.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_beep_driver.h"
#include "bsp_led_handler.h"

#define SELF_TEST_BLINK_COUNT    3U     // 自检闪烁次数
#define SELF_TEST_HALF_PERIOD_MS 250U   // 自检闪烁周期的一半时间
#define RGB_TEST_HOLD_MS         400U   // RGB测试保持时间
#define RGB_TEST_OFF_HOLD_MS     200U   // RGB测试关闭保持时间
#define SYSTEM_BLINK_COUNT       3U     // 系统状态灯闪烁次数
#define SYSTEM_HALF_PERIOD_MS    250U   // 系统状态灯闪烁周期的一半时间
#define BEEP_TIME_MS             120U   // 蜂鸣器反馈时间
#define LED_BRIGHTNESS           20U    // LED亮度值

static const bsp_led_color_t COLOR_OFF = {0U, 0U, 0U};
static const bsp_led_color_t COLOR_RED = {LED_BRIGHTNESS, 0U, 0U};
static const bsp_led_color_t COLOR_GREEN = {0U, LED_BRIGHTNESS, 0U};
static const bsp_led_color_t COLOR_BLUE = {0U, 0U, LED_BRIGHTNESS};
static volatile bool s_led_ready;

/**
 * @brief 同时设置五颗相机灯和LED7系统状态灯。
 * @param camera_color LED2~LED6的目标颜色。
 * @param system_color LED7的目标颜色。
 * @return LED Handler层状态。
 */
static led_handler_status_t led_task_show_state(
    bsp_led_color_t camera_color,
    bsp_led_color_t system_color)
{
    bsp_led_handler_clear();

    led_handler_status_t status =
        bsp_led_handler_set_all_cameras(camera_color);
    if (HANDLER_OK != status)
    {
        return status;
    }

    status = bsp_led_handler_set(BSP_LED_SYSTEM, system_color);
    if (HANDLER_OK != status)
    {
        return status;
    }

    return bsp_led_handler_commit();
}

/**
 * @brief 执行六灯RGB检查及五颗相机灯绿色闪烁自检。
 * @return LED Handler层状态。
 */
static led_handler_status_t led_task_run_self_test(void)
{
    const bsp_led_color_t rgb_colors[] = {
        {LED_BRIGHTNESS, 0U, 0U},
        {0U, LED_BRIGHTNESS, 0U},
        {0U, 0U, LED_BRIGHTNESS},
    };

    for (uint8_t color = 0U;
         color < (uint8_t)(sizeof(rgb_colors) / sizeof(rgb_colors[0]));
         ++color)
    {
        const led_handler_status_t status =
            led_task_show_state(rgb_colors[color], rgb_colors[color]);
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(RGB_TEST_HOLD_MS));
    }

    led_handler_status_t status =
        led_task_show_state(COLOR_OFF, COLOR_OFF);
    if (HANDLER_OK != status)
    {
        return status;
    }
    vTaskDelay(pdMS_TO_TICKS(RGB_TEST_OFF_HOLD_MS));

    for (uint8_t blink = 0U; blink < SELF_TEST_BLINK_COUNT; ++blink)
    {
        status = led_task_show_state(COLOR_GREEN, COLOR_OFF);
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(SELF_TEST_HALF_PERIOD_MS));

        status = led_task_show_state(COLOR_OFF, COLOR_OFF);
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(SELF_TEST_HALF_PERIOD_MS));
    }

    return HANDLER_OK;
}

/**
 * @brief 让LED7蓝色闪烁，LED2~LED6保持绿色。
 * @return LED Handler层状态。
 */
static led_handler_status_t led_task_blink_system_blue(void)
{
    for (uint8_t blink = 0U; blink < SYSTEM_BLINK_COUNT; ++blink)
    {
        led_handler_status_t status =
            led_task_show_state(COLOR_GREEN, COLOR_BLUE);
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_HALF_PERIOD_MS));

        status = led_task_show_state(COLOR_GREEN, COLOR_OFF);
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_HALF_PERIOD_MS));
    }

    return HANDLER_OK;
}

/**
 * @brief 显示红色故障状态并停止当前任务的正常流程。
 */
static void led_task_show_fault(void)
{
    (void)bsp_beep_driver_set(false);
    (void)led_task_show_state(COLOR_RED, COLOR_RED);

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void led_task_start_collection(void)
{
    if (!s_led_ready)
    {
        return;
    }

    led_handler_status_t led_status = led_task_blink_system_blue();//
    if (HANDLER_OK != led_status)
    {
        led_task_show_fault();
    }

    beep_driver_status_t beep_status = bsp_beep_driver_set(true);
    if (BEEP_DRIVER_OK != beep_status)
    {
        led_task_show_fault();
    }
    vTaskDelay(pdMS_TO_TICKS(BEEP_TIME_MS));

    beep_status = bsp_beep_driver_set(false);
    if (BEEP_DRIVER_OK != beep_status)
    {
        led_task_show_fault();
    }

    led_status = led_task_show_state(COLOR_GREEN, COLOR_OFF);
    if (HANDLER_OK != led_status)
    {
        led_task_show_fault();
    }
}

void led_task_entry(void *argument)
{
    led_handler_status_t led_status = bsp_led_handler_init();
    if (HANDLER_OK != led_status)
    {
        led_task_show_fault();
    }

    beep_driver_status_t beep_status = bsp_beep_driver_init();
    if (BEEP_DRIVER_OK != beep_status)
    {
        led_task_show_fault();
    }

    led_status = led_task_run_self_test();
    if (HANDLER_OK != led_status)
    {
        led_task_show_fault();
    }

    led_status = led_task_show_state(COLOR_GREEN, COLOR_OFF);//
    if (HANDLER_OK != led_status)
    {
        led_task_show_fault();
    }

    s_led_ready = true;

    for (;;)
    {
        vTaskSuspend(NULL);
    }
}
