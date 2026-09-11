/**
 * @file micro_ros_task.h
 * @brief USART2 micro-ROS通信任务接口。
 */
#ifndef MICRO_ROS_TASK_H
#define MICRO_ROS_TASK_H

#include <stdint.h>

#include "task_status.h"

/**
 * @brief 创建按键事件上报Queue。
 * @retval TASK_OK Queue创建成功或已经存在。
 * @retval TASK_ERROR_NO_MEMORY FreeRTOS无法分配Queue。
 * @note 必须在调度器启动前由task_manager_init()调用。
 */
task_status_t micro_ros_task_resources_init(void);

/**
 * @brief 将已识别的按键事件送入micro-ROS发布Queue。
 * @param event_type 参考ButtonEvent定义的事件值，范围为1~4。
 * @retval TASK_OK 事件已经入队。
 * @retval TASK_ERROR_RESOURCE Agent未连接或Queue尚未创建。
 * @retval TASK_ERROR_PARAMETER event_type不在有效范围内。
 * @retval TASK_ERROR Queue已满，事件被丢弃且不会延迟回放。
 */
task_status_t micro_ros_task_enqueue_key_event(uint8_t event_type);

/**
 * @brief 管理Agent连接、ROS实体、消息收发和断线重连。
 * @param argument FreeRTOS预留任务参数，当前固定传入NULL。
 */
void micro_ros_task_entry(void *argument);

#endif /* MICRO_ROS_TASK_H */
