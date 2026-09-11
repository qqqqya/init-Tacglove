/**
 * @file task_manager.h
 * @brief 两个用户任务的统一创建接口。
 */
#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include "task_status.h"

/**
 * @brief 初始化任务间资源并创建LED、按键和micro-ROS任务。
 * @retval TASK_OK 资源初始化和三个任务创建成功。
 * @retval TASK_ERROR_NO_MEMORY 任一任务创建失败。
 * @retval TASK_ERROR_RESOURCE 任一任务间资源初始化失败。
 */
task_status_t task_manager_init(void);

#endif /* TASK_MANAGER_H */
