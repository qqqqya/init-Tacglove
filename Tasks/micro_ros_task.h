/**
 * @file micro_ros_task.h
 * @brief USART2 micro-ROS通信任务接口。
 */
#ifndef MICRO_ROS_TASK_H
#define MICRO_ROS_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "task_manager.h"

/** @brief micro-ROS连接、执行器、状态发布和同步参数。 */
#define MICRO_ROS_DOMAIN_ID              9U
#define MICRO_ROS_EXECUTOR_HANDLES       2U
#define MICRO_ROS_KEY_QUEUE_LENGTH       8U
#define MICRO_ROS_AGENT_WAIT_MS          500U
#define MICRO_ROS_AGENT_PING_TIMEOUT_MS  100U
#define MICRO_ROS_AGENT_PING_ATTEMPTS    1U
#define MICRO_ROS_AGENT_FAILURE_LIMIT    3U
#define MICRO_ROS_EXECUTOR_TIMEOUT_MS    5U
#define MICRO_ROS_LOOP_DELAY_MS          2U
#define MICRO_ROS_STATUS_PERIOD_MS       1000U
#define MICRO_ROS_HEALTH_PERIOD_MS       1000U
#define MICRO_ROS_SYNC_RETRY_MS          2000U
#define MICRO_ROS_SYNC_RESPONSE_MS       1500U
#define MICRO_ROS_FRAME_BUFFER_SIZE      64U

/** @brief ROS节点、Topic、Service和版本标识。 */
#define MICRO_ROS_NODE_NAME              "mcu_dev"
#define MICRO_ROS_KEY_TOPIC              "/mcu_dev/key_state"
#define MICRO_ROS_STATUS_TOPIC           "/mcu_dev/mcu_status"
#define MICRO_ROS_LED_CMD_TOPIC          "/mcu_dev/led_cmd"
#define MICRO_ROS_SYNC_SERVICE           "/mcu_dev/sync"
#define MICRO_ROS_FIRMWARE_VERSION       "0.3.0-dev"

/** @brief micro-ROS任务连接状态。 */
typedef enum
{
    MICRO_ROS_STATE_WAIT_AGENT = 0,
    MICRO_ROS_STATE_RUNNING
} micro_ros_state_t;

/** @brief 已成功创建的ROS实体，用于失败回滚和断线释放。 */
typedef struct
{
    bool support;
    bool node;
    bool key_pub;
    bool status_pub;
    bool led_cmd_sub;
    bool sync_client;
    bool executor;
} micro_ros_entity_flags_t;

/**
 * @brief 创建按键事件上报Queue。
 * @retval TASK_OK Queue创建成功或已经存在。
 * @retval TASK_ERROR_NO_MEMORY FreeRTOS无法分配Queue。
 * @note 必须在调度器启动前由task_manager_init()调用。
 */
task_status_t micro_ros_task_resources_init(void);

/**
 * @brief 将已识别的按键事件送入micro-ROS发布Queue。
 * @param event_type 参考KeyState定义的事件值，范围为1~4。
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
