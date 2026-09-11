/**
 * @file task_status.h
 * @brief 用户任务层公共返回状态。
 */
#ifndef TASK_STATUS_H
#define TASK_STATUS_H

/** @brief Task层函数返回状态。 */
typedef enum
{
    TASK_OK              = 0,    /**< Operation completed successfully. */
    TASK_ERROR           = 1,    /**< General runtime error. */
    TASK_ERROR_RESOURCE  = 2,    /**< Required resource is unavailable. */
    TASK_ERROR_PARAMETER = 3,    /**< Invalid parameter. */
    TASK_ERROR_NO_MEMORY = 4     /**< Memory allocation failed. */
} task_status_t;

#endif /* TASK_STATUS_H */
