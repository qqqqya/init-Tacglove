/**
 * @file task_manager.h
 * @brief 用户任务公共定义和统一创建接口。
 */
#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include "FreeRTOS.h"
#include "task.h"

/** @brief Task层函数返回状态。 */
typedef enum
{
    TASK_OK              = 0,    /**< Operation completed successfully. */
    TASK_ERROR           = 1,    /**< General runtime error. */
    TASK_ERROR_RESOURCE  = 2,    /**< Required resource is unavailable. */
    TASK_ERROR_PARAMETER = 3,    /**< Invalid parameter. */
    TASK_ERROR_NO_MEMORY = 4     /**< Memory allocation failed. */
} task_status_t;

/** @brief 各任务的栈深度和优先级。 */
#define LED_TASK_STACK_WORDS       256U
#define LED_TASK_PRIORITY          (tskIDLE_PRIORITY + 1U)
#define KEY_TASK_STACK_WORDS       128U
#define KEY_TASK_PRIORITY          (tskIDLE_PRIORITY + 2U)
#define MICRO_ROS_TASK_STACK_WORDS 4096U
#define MICRO_ROS_TASK_PRIORITY    (tskIDLE_PRIORITY + 2U)

/** @brief LED任务句柄，供按键任务通过FreeRTOS Task Notification投递动作。 */
extern TaskHandle_t g_led_task_handle;

/**
 * @brief 初始化任务间资源并创建LED、按键和micro-ROS任务。
 * @retval TASK_OK 资源初始化和三个任务创建成功。
 * @retval TASK_ERROR_NO_MEMORY 任一任务创建失败。
 * @retval TASK_ERROR_RESOURCE 任一任务间资源初始化失败。
 */
task_status_t task_manager_init(void);

#endif /* TASK_MANAGER_H */
