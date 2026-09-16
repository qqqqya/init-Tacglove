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

TaskHandle_t g_led_task_handle;

task_status_t task_manager_init(void){
    /* 初始化LED key等 存储cmd的队列/邮箱 */
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

    /* 创建LED任务 */
    BaseType_t result = xTaskCreate(led_task_entry,
                                     "led",
                                     LED_TASK_STACK_WORDS,
                                     NULL,
                                     LED_TASK_PRIORITY,
                                     &g_led_task_handle);
    if (pdPASS != result)
    {
        return TASK_ERROR_NO_MEMORY;
    }

    /* 创建按键任务 */
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

    /* 创建micro-ROS任务 */
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
