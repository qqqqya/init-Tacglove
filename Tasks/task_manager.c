/**
 * @file task_manager.c
 * @brief LED任务和按键任务的创建实现。
 */
#include "task_manager.h"

#include "FreeRTOS.h"
#include "task.h"

#include "key_task.h"
#include "led_task.h"

#define LED_TASK_STACK_WORDS 256U
#define LED_TASK_PRIORITY    (tskIDLE_PRIORITY + 1U)
#define KEY_TASK_STACK_WORDS 128U
#define KEY_TASK_PRIORITY    (tskIDLE_PRIORITY + 2U)

task_status_t task_manager_init(void)
{
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

    return TASK_OK;
}
