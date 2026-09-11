/**
 * @file task_manager.c
 * @brief LED、按键和micro-ROS任务的资源初始化及创建实现。
 */
#include "task_manager.h"

#include "FreeRTOS.h"
#include "task.h"

#include "key_task.h"
#include "led_task.h"
#include "micro_ros_task.h"

#define LED_TASK_STACK_WORDS       256U
#define LED_TASK_PRIORITY          (tskIDLE_PRIORITY + 1U)
#define KEY_TASK_STACK_WORDS       128U
#define KEY_TASK_PRIORITY          (tskIDLE_PRIORITY + 2U)
#define MICRO_ROS_TASK_STACK_WORDS 4096U
#define MICRO_ROS_TASK_PRIORITY    (tskIDLE_PRIORITY + 2U)

task_status_t task_manager_init(void)
{
    task_status_t status = led_task_resources_init();
    if (TASK_OK != status)
    {
        return status;
    }

    status = micro_ros_task_resources_init();
    if (TASK_OK != status)
    {
        return status;
    }

    BaseType_t result = xTaskCreate(led_task_entry,
                                     "led",
                                     LED_TASK_STACK_WORDS,
                                     NULL,
                                     LED_TASK_PRIORITY,
                                     NULL);
    if (pdPASS != result)
    {
        return TASK_ERROR_NO_MEMORY;
    }

    result = xTaskCreate(key_task_entry,
                         "key",
                         KEY_TASK_STACK_WORDS,
                         NULL,
                         KEY_TASK_PRIORITY,
                         NULL);
    if (pdPASS != result)
    {
        return TASK_ERROR_NO_MEMORY;
    }

    result = xTaskCreate(micro_ros_task_entry,
                         "micro_ros",
                         MICRO_ROS_TASK_STACK_WORDS,
                         NULL,
                         MICRO_ROS_TASK_PRIORITY,
                         NULL);
    if (pdPASS != result)
    {
        return TASK_ERROR_NO_MEMORY;
    }

    return TASK_OK;
}
