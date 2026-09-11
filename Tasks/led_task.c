/**
 * @file led_task.c
 * @brief LED上电自检、采集状态和按键识别反馈任务。
 */
#include "led_task.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "bsp_beep_driver.h"
#include "bsp_led_handler.h"

#define SELF_TEST_BLINK_COUNT      3U
#define SELF_TEST_HALF_PERIOD_MS   250U
#define PREPARE_BLINK_COUNT        3U
#define PREPARE_HALF_PERIOD_MS     250U
#define SHORT_BEEP_TIME_MS         120U
#define LONG_BEEP_TIME_MS          600U
#define DOUBLE_BEEP_TIME_MS        100U
#define DOUBLE_BEEP_INTERVAL_MS    100U
#define LONG_FEEDBACK_TIME_MS      600U
#define DOUBLE_FEEDBACK_TIME_MS    100U
#define REMOTE_BLINK_HALF_PERIOD_MS 250U
#define LED_TASK_PERIOD_MS         1U
#define LED_BRIGHTNESS             1U

#define LED_ACTION_SHORT_PRESS  (1UL << 0U)
#define LED_ACTION_LONG_PRESS   (1UL << 1U)
#define LED_ACTION_DOUBLE_CLICK (1UL << 2U)

static const bsp_led_color_t COLOR_OFF = {0U, 0U, 0U};
static const bsp_led_color_t COLOR_RED = {LED_BRIGHTNESS, 0U, 0U};
static const bsp_led_color_t COLOR_GREEN = {0U, LED_BRIGHTNESS, 0U};
static const bsp_led_color_t COLOR_BLUE = {0U, 0U, LED_BRIGHTNESS};

typedef struct
{
    bool enabled;
    uint8_t led_mode[LED_TASK_LED_COUNT];
} led_task_cmd_t;

static volatile uint32_t s_pending_actions;
static volatile bool s_led_ready;
static volatile uint8_t s_system_state = LED_TASK_SYSTEM_SELF_TEST;
static QueueHandle_t s_led_cmd_queue;

/**
 * @brief 将ROS灯效模式转换为当前闪烁相位对应的颜色。
 * @param mode LED_TASK_MODE_xxx模式。
 * @param blink_on true表示闪烁灯当前处于亮相位。
 * @param[out] color 非空颜色输出指针。
 * @retval TASK_OK 转换成功。
 * @retval TASK_ERROR_PARAMETER mode非法或color为空。
 */
static task_status_t led_task_mode_to_color(uint8_t mode,
                                            bool blink_on,
                                            bsp_led_color_t *color)
{
    if (NULL == color)
    {
        return TASK_ERROR_PARAMETER;
    }

    switch (mode)
    {
        case LED_TASK_MODE_OFF:
            *color = COLOR_OFF;
            break;

        case LED_TASK_MODE_GREEN_SOLID:
            *color = COLOR_GREEN;
            break;

        case LED_TASK_MODE_GREEN_BLINK:
            *color = blink_on ? COLOR_GREEN : COLOR_OFF;
            break;

        case LED_TASK_MODE_RED_SOLID:
            *color = COLOR_RED;
            break;

        case LED_TASK_MODE_BLUE_BLINK:
            *color = blink_on ? COLOR_BLUE : COLOR_OFF;
            break;

        case LED_TASK_MODE_BLUE_SOLID:
            *color = COLOR_BLUE;
            break;

        default:
            return TASK_ERROR_PARAMETER;
    }

    return TASK_OK;
}

/**
 * @brief 判断远程cmd中是否至少包含一个闪烁模式。
 * @param cmd 非空远程cmd指针。
 * @return true表示需要周期切换闪烁相位。
 */
static bool led_task_cmd_has_blink(const led_task_cmd_t *cmd)
{
    for (uint8_t index = 0U; index < LED_TASK_LED_COUNT; ++index)
    {
        if ((LED_TASK_MODE_GREEN_BLINK == cmd->led_mode[index]) ||
            (LED_TASK_MODE_BLUE_BLINK == cmd->led_mode[index]))
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief 显示远程cmd在指定闪烁相位下的完整六灯状态。
 * @param cmd 非空远程cmd指针。
 * @param blink_on true表示闪烁灯当前处于亮相位。
 * @return LED Handler层状态。
 */
static led_handler_status_t led_task_show_remote_cmd(
    const led_task_cmd_t *cmd,
    bool blink_on)
{
    bsp_led_handler_clear();

    for (uint8_t index = 0U; index < LED_TASK_LED_COUNT; ++index)
    {
        bsp_led_color_t color = COLOR_OFF;
        const task_status_t convert_status = led_task_mode_to_color(
            cmd->led_mode[index], blink_on, &color);
        if (TASK_OK != convert_status)
        {
            return HANDLER_ERRORPARAMETER;
        }

        const led_handler_status_t status = bsp_led_handler_set(
            (bsp_led_id_t)index, color);
        if (HANDLER_OK != status)
        {
            return status;
        }
    }

    return bsp_led_handler_commit();
}

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
 * @brief 执行LED2~LED6绿色闪烁的上电自检灯效。
 * @return LED Handler层状态。
 * @note LED7在整个上电自检过程中保持熄灭。
 */
static led_handler_status_t led_task_run_self_test(void)
{
    for (uint8_t blink = 0U; blink < SELF_TEST_BLINK_COUNT; ++blink)
    {
        led_handler_status_t status =
            led_task_show_state(COLOR_GREEN, COLOR_OFF);//这个green不对 亮度在哪里调？？--好像里面也包含 但是时序好像确实不对
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
 * @brief 让LED7蓝色闪烁表示进入数据采集准备状态。
 * @return LED Handler层状态。
 */
static led_handler_status_t led_task_run_prepare(void)
{
    for (uint8_t blink = 0U; blink < PREPARE_BLINK_COUNT; ++blink)
    {
        led_handler_status_t status =
            led_task_show_state(COLOR_GREEN, COLOR_BLUE);
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(PREPARE_HALF_PERIOD_MS));

        status = led_task_show_state(COLOR_GREEN, COLOR_OFF);
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(PREPARE_HALF_PERIOD_MS));
    }

    return HANDLER_OK;
}

/**
 * @brief 阻塞LED任务完成一次蜂鸣，按键任务仍可继续扫描。
 * @param duration_ms 蜂鸣持续时间，单位ms，必须大于0。
 * @return 蜂鸣器Driver层状态。
 */
static beep_driver_status_t led_task_beep(uint32_t duration_ms)
{
    beep_driver_status_t status = bsp_beep_driver_set(true);
    if (BEEP_DRIVER_OK != status)
    {
        return status;
    }

    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return bsp_beep_driver_set(false);
}

/**
 * @brief 临时反转LED7，用于观察长按或双击是否被正确识别。
 * @param collecting true表示当前处于采集中状态。
 * @param hold_ms 反转状态保持时间，单位ms。
 * @return LED Handler层状态。
 */
static led_handler_status_t led_task_show_gesture_feedback(
    bool collecting,
    uint32_t hold_ms)
{
    const bsp_led_color_t feedback_color = collecting ? COLOR_OFF : COLOR_BLUE;
    led_handler_status_t status =
        led_task_show_state(COLOR_GREEN, feedback_color);
    if (HANDLER_OK != status)
    {
        return status;
    }

    vTaskDelay(pdMS_TO_TICKS(hold_ms));
    return led_task_show_state(COLOR_GREEN,
                               collecting ? COLOR_BLUE : COLOR_OFF);
}

/**
 * @brief 原子取出并清空当前待处理的按键动作位。
 * @return 待处理动作位掩码。
 */
static uint32_t led_task_take_pending_actions(void)
{
    taskENTER_CRITICAL();
    const uint32_t actions = s_pending_actions;
    s_pending_actions = 0U;
    taskEXIT_CRITICAL();
    return actions;
}

/**
 * @brief 原子记录一个由按键任务直接提交的LED动作。
 * @param action LED_ACTION_xxx动作位。
 */
static void led_task_request_action(uint32_t action)
{
    if (!s_led_ready)
    {
        return;
    }

    taskENTER_CRITICAL();// 确保原子性  进入临界区
    s_pending_actions |= action;
    taskEXIT_CRITICAL();
}

/**
 * @brief 显示六灯红色常亮故障状态并停止正常流程。
 */
static void led_task_show_fault(void)
{
    s_system_state = LED_TASK_SYSTEM_ERROR;
    (void)bsp_beep_driver_set(false);
    (void)led_task_show_state(COLOR_RED, COLOR_RED);

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

task_status_t led_task_resources_init(void)
{
    if (NULL != s_led_cmd_queue)
    {
        return TASK_OK;
    }

    s_led_cmd_queue = xQueueCreate(1U, sizeof(led_task_cmd_t));
    if (NULL == s_led_cmd_queue)
    {
        return TASK_ERROR_NO_MEMORY;
    }

    return TASK_OK;
}

task_status_t led_task_submit_cmd(
    const uint8_t led_mode[LED_TASK_LED_COUNT])
{
    if (NULL == led_mode)
    {
        return TASK_ERROR_PARAMETER;
    }

    if (NULL == s_led_cmd_queue)
    {
        return TASK_ERROR_RESOURCE;
    }

    led_task_cmd_t cmd = {.enabled = true};
    for (uint8_t index = 0U; index < LED_TASK_LED_COUNT; ++index)
    {
        if (LED_TASK_MODE_BLUE_SOLID < led_mode[index])
        {
            return TASK_ERROR_PARAMETER;
        }

        cmd.led_mode[index] = led_mode[index];
    }

    if (pdPASS != xQueueOverwrite(s_led_cmd_queue, &cmd))
    {
        return TASK_ERROR;
    }

    return TASK_OK;
}

task_status_t led_task_release_remote_control(void)
{
    if (NULL == s_led_cmd_queue)
    {
        return TASK_ERROR_RESOURCE;
    }

    const led_task_cmd_t cmd = {.enabled = false};
    if (pdPASS != xQueueOverwrite(s_led_cmd_queue, &cmd))
    {
        return TASK_ERROR;
    }

    return TASK_OK;
}

uint8_t led_task_get_system_state(void)
{
    return s_system_state;
}

void led_task_on_short_press(void)
{
    led_task_request_action(LED_ACTION_SHORT_PRESS);
}

void led_task_on_long_press(void)
{
    led_task_request_action(LED_ACTION_LONG_PRESS);
}

void led_task_on_double_click(void)
{
    led_task_request_action(LED_ACTION_DOUBLE_CLICK);
}

void led_task_entry(void *argument)
{
    s_system_state = LED_TASK_SYSTEM_SELF_TEST;

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

    led_status = led_task_run_self_test();//上电自检LED 五个绿灯闪烁三次
    if (HANDLER_OK != led_status)
    {
        led_task_show_fault();
    }

    led_status = led_task_show_state(COLOR_GREEN, COLOR_OFF);//自检之后常亮绿色
    if (HANDLER_OK != led_status)
    {
        led_task_show_fault();
    }

    bool collecting = false;
    bool remote_active = false;
    bool remote_blink_on = true;
    led_task_cmd_t remote_cmd = {0};
    TickType_t next_remote_blink = xTaskGetTickCount();

    s_system_state = LED_TASK_SYSTEM_IDLE;
    s_led_ready = true;

    for (;;)
    {
        led_task_cmd_t received_cmd = {0};
        if (pdPASS == xQueueReceive(s_led_cmd_queue, &received_cmd, 0U))
        {
            remote_active = received_cmd.enabled;
            if (remote_active)
            {
                remote_cmd = received_cmd;
                remote_blink_on = true;
                next_remote_blink = xTaskGetTickCount() +
                                    pdMS_TO_TICKS(
                                        REMOTE_BLINK_HALF_PERIOD_MS);
                led_status = led_task_show_remote_cmd(&remote_cmd,
                                                      remote_blink_on);
            }
            else
            {
                led_status = led_task_show_state(
                    COLOR_GREEN,
                    collecting ? COLOR_BLUE : COLOR_OFF);
            }

            if (HANDLER_OK != led_status)
            {
                led_task_show_fault();
            }
        }

        const uint32_t actions = led_task_take_pending_actions();

        if (0U != (actions & LED_ACTION_SHORT_PRESS))
        {
            if (!collecting)
            {
                s_system_state = LED_TASK_SYSTEM_PREPARING;

                if (!remote_active)
                {
                    led_status = led_task_run_prepare();
                    if (HANDLER_OK != led_status)
                    {
                        led_task_show_fault();
                    }
                }

                beep_status = led_task_beep(SHORT_BEEP_TIME_MS);
                if (BEEP_DRIVER_OK != beep_status)
                {
                    led_task_show_fault();
                }

                if (!remote_active)
                {
                    led_status = led_task_show_state(COLOR_GREEN,
                                                     COLOR_BLUE);
                }
                collecting = true;
                s_system_state = LED_TASK_SYSTEM_COLLECTING;
            }
            else
            {
                beep_status = led_task_beep(SHORT_BEEP_TIME_MS);
                if (BEEP_DRIVER_OK != beep_status)
                {
                    led_task_show_fault();
                }

                if (!remote_active)
                {
                    led_status = led_task_show_state(COLOR_GREEN, COLOR_OFF);
                }
                collecting = false;
                s_system_state = LED_TASK_SYSTEM_IDLE;
            }

            if ((!remote_active) && (HANDLER_OK != led_status))
            {
                led_task_show_fault();
            }
        }

        if (0U != (actions & LED_ACTION_LONG_PRESS))
        {
            if (!remote_active)
            {
                led_status = led_task_show_gesture_feedback(
                    collecting,
                    LONG_FEEDBACK_TIME_MS);
                if (HANDLER_OK != led_status)
                {
                    led_task_show_fault();
                }
            }

            beep_status = led_task_beep(LONG_BEEP_TIME_MS);
            if (BEEP_DRIVER_OK != beep_status)
            {
                led_task_show_fault();
            }
        }

        if (0U != (actions & LED_ACTION_DOUBLE_CLICK))
        {
            for (uint8_t count = 0U; count < 2U; ++count)
            {
                if (!remote_active)
                {
                    led_status = led_task_show_gesture_feedback(
                        collecting,
                        DOUBLE_FEEDBACK_TIME_MS);
                    if (HANDLER_OK != led_status)
                    {
                        led_task_show_fault();
                    }
                }

                beep_status = led_task_beep(DOUBLE_BEEP_TIME_MS);
                if (BEEP_DRIVER_OK != beep_status)
                {
                    led_task_show_fault();
                }

                if (0U == count)
                {
                    vTaskDelay(pdMS_TO_TICKS(DOUBLE_BEEP_INTERVAL_MS));
                }
            }
        }

        const TickType_t current_tick = xTaskGetTickCount();
        if (remote_active &&
            led_task_cmd_has_blink(&remote_cmd) &&
            ((int32_t)(current_tick - next_remote_blink) >= 0))
        {
            remote_blink_on = !remote_blink_on;
            next_remote_blink = current_tick +
                                pdMS_TO_TICKS(
                                    REMOTE_BLINK_HALF_PERIOD_MS);
            led_status = led_task_show_remote_cmd(&remote_cmd,
                                                  remote_blink_on);
            if (HANDLER_OK != led_status)
            {
                led_task_show_fault();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LED_TASK_PERIOD_MS));
    }
}
