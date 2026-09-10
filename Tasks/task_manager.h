/**
 * @file task_manager.h
 * @brief 两个用户任务的统一创建接口。
 */
#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

/** @brief 任务创建返回状态。 */
typedef enum
{
    TASK_OK = 0,           /**< 两个任务均创建成功。 */
    TASK_ERROR_NO_MEMORY   /**< FreeRTOS无法分配任务控制块或任务栈。 */
} task_status_t;

/**
 * @brief 直接创建LED任务和按键任务。
 * @retval TASK_OK 两个任务均创建成功。
 * @retval TASK_ERROR_NO_MEMORY 任一任务创建失败。
 */
task_status_t task_manager_init(void);

#endif /* TASK_MANAGER_H */
