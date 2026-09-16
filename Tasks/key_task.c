/**
 * @file key_task.c
 * @brief 数据采集按键状态机和LED动作分发任务。
 */
#include "key_task.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_key_handler.h"
#include "led_task.h"
#include "micro_ros_task.h"

void key_task_entry(void *argument){
    key_handler_status_t status = bsp_key_handler_init();
    if (KEY_HANDLER_OK != status)
    {
        vTaskSuspend(NULL);
    }

    TickType_t last_wake_time = xTaskGetTickCount();

    for (;;)
    {
        status = bsp_key_handler_process();
        if (KEY_HANDLER_OK != status)
        {
            vTaskSuspend(NULL);
        }

        key_event_t event = KEY_EVENT_NONE;
        status = bsp_key_handler_get_event(&event);
        if (KEY_HANDLER_OK != status)
        {
            vTaskSuspend(NULL);
        }

        switch (event)
        {
            case KEY_EVENT_SHORT_PRESS:
                led_task_on_short_press();
                break;

            case KEY_EVENT_LONG_PRESS:
                led_task_on_long_press();
                break;

            case KEY_EVENT_DOUBLE_CLICK:
                led_task_on_double_click();
                break;

            case KEY_EVENT_NONE:
            default:
                break;
        }

        if (KEY_EVENT_NONE != event)
        {
            /* Agent断线或Queue满时只丢弃ROS上报，本地灯和蜂鸣器动作不受影响。 */
            (void)micro_ros_task_enqueue_key_event((uint8_t)event);

            status = bsp_key_handler_clear_event();
            if (KEY_HANDLER_OK != status)
            {
                vTaskSuspend(NULL);
            }
        }

        vTaskDelayUntil(&last_wake_time,
                        pdMS_TO_TICKS(KEY_TASK_PERIOD_MS));
    }
}
