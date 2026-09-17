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
        status = bsp_key_handler_process(); //读取PA0并向前推进一次按键状态机。状态改为单击 双击 长按
                                            //包含事件KEY_EVENT更新；KEY_STATE
        if (KEY_HANDLER_OK != status)
        {
            vTaskSuspend(NULL);
        }

        key_event_t event = KEY_EVENT_NONE;
        status = bsp_key_handler_get_event(&event); //获取当前按键事件 key_handler_process中更新的s_key.event
        if (KEY_HANDLER_OK != status)
        {
            vTaskSuspend(NULL);
        }

        switch (event)
        {   //根据按键事件分发LED动作
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
        {//按键事件有更新--进行上报（ROS连接情况下）
            /* Agent断线或Queue满时只丢弃ROS上报，本地灯和蜂鸣器动作不受影响 ---from ros task */
            (void)micro_ros_task_enqueue_key_event((uint8_t)event);//将已识别的按键事件送入micro-ROS发布Queue。

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
