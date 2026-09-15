/**
 * @file micro_ros_task.c
 * @brief USART2 micro-ROS Client连接、实体管理和消息收发任务。
 */
#include "micro_ros_task.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "led_task.h"
#include "usart.h"

#include "dma_transport.h"
#include "microros_allocators.h"

#include "common_msgs/msg/button_event.h"
#include "common_msgs/msg/led_cmd.h"
#include "common_msgs/msg/mcu_status.h"
#include "common_msgs/srv/device_synchronization.h"

#include "rcl/rcl.h"
#include "rclc/executor.h"
#include "rclc/rclc.h"
#include "rcutils/allocator.h"
#include "rmw_microros/rmw_microros.h"

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

#define MICRO_ROS_NODE_NAME              "mcu_dev"
#define MICRO_ROS_KEY_TOPIC              "/mcu_dev/key_state"
#define MICRO_ROS_STATUS_TOPIC           "/mcu_dev/mcu_status"
#define MICRO_ROS_LED_CMD_TOPIC          "/mcu_dev/led_cmd"
#define MICRO_ROS_SYNC_SERVICE           "/mcu_dev/sync"
#define MICRO_ROS_FIRMWARE_VERSION       "0.3.0-dev"

typedef enum
{
    MICRO_ROS_STATE_WAIT_AGENT = 0,
    MICRO_ROS_STATE_RUNNING
} micro_ros_state_t;

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

static QueueHandle_t s_key_event_queue;
static volatile bool s_agent_connected;
static uint32_t s_message_tx_count;
static uint32_t s_message_rx_count;
static uint32_t s_key_drop_count;

static rcl_allocator_t s_allocator;
static rclc_support_t s_support;
static rcl_node_t s_node;
static rcl_publisher_t s_key_state_pub;
static rcl_publisher_t s_mcu_status_pub;
static rcl_subscription_t s_led_cmd_sub;
static rcl_client_t s_sync_client;
static rclc_executor_t s_executor;
static micro_ros_entity_flags_t s_entity_flags;

static common_msgs__msg__ButtonEvent s_key_state_msg;
static common_msgs__msg__MCUStatus s_mcu_status_msg;
static common_msgs__msg__LedCmd s_led_cmd_msg;
static common_msgs__srv__DeviceSynchronization_Request s_sync_request;
static common_msgs__srv__DeviceSynchronization_Response s_sync_response;

static char s_key_frame_id[] = "key_state";
static char s_mcu_frame_id[] = "mcu";
static char s_firmware_version[] = MICRO_ROS_FIRMWARE_VERSION;
static char s_led_cmd_frame_buffer[MICRO_ROS_FRAME_BUFFER_SIZE];
static char s_sync_frame_buffer[MICRO_ROS_FRAME_BUFFER_SIZE];

static bool s_time_synced;
static bool s_sync_pending;
static int64_t s_sync_sequence;
static int64_t s_epoch_base_ns;
static TickType_t s_sync_request_tick;

/** @brief 将ROS消息对象绑定到全部静态字符串缓冲区。 */
static void micro_ros_init_messages(void)
{
    memset(&s_key_state_msg, 0, sizeof(s_key_state_msg));
    s_key_state_msg.header.frame_id.data = s_key_frame_id;
    s_key_state_msg.header.frame_id.size = strlen(s_key_frame_id);
    s_key_state_msg.header.frame_id.capacity = sizeof(s_key_frame_id);

    memset(&s_mcu_status_msg, 0, sizeof(s_mcu_status_msg));
    s_mcu_status_msg.header.frame_id.data = s_mcu_frame_id;
    s_mcu_status_msg.header.frame_id.size = strlen(s_mcu_frame_id);
    s_mcu_status_msg.header.frame_id.capacity = sizeof(s_mcu_frame_id);
    s_mcu_status_msg.firmware_version.data = s_firmware_version;
    s_mcu_status_msg.firmware_version.size = strlen(s_firmware_version);
    s_mcu_status_msg.firmware_version.capacity =
        sizeof(s_firmware_version);

    memset(&s_led_cmd_msg, 0, sizeof(s_led_cmd_msg));
    s_led_cmd_msg.header.frame_id.data = s_led_cmd_frame_buffer;
    s_led_cmd_msg.header.frame_id.capacity = sizeof(s_led_cmd_frame_buffer);

    memset(&s_sync_request, 0, sizeof(s_sync_request));
    s_sync_request.sync_request = true;

    memset(&s_sync_response, 0, sizeof(s_sync_response));
    s_sync_response.header.frame_id.data = s_sync_frame_buffer;
    s_sync_response.header.frame_id.capacity = sizeof(s_sync_frame_buffer);
}

/**
 * @brief 配置micro-ROS专用分配器。
 * @return true表示rcutils已接受分配器。
 */
static bool micro_ros_init_allocator(void)
{
    rcutils_allocator_t allocator = rcutils_get_zero_initialized_allocator();
    allocator.allocate = microros_allocate;
    allocator.deallocate = microros_deallocate;
    allocator.reallocate = microros_reallocate;
    allocator.zero_allocate = microros_zero_allocate;
    allocator.state = NULL;

    return rcutils_set_default_allocator(&allocator);
}

/** @brief 将全部rcl句柄恢复为可重新初始化的零状态。 */
static void micro_ros_zero_entities(void)
{
    memset(&s_support, 0, sizeof(s_support));
    memset(&s_entity_flags, 0, sizeof(s_entity_flags));
    s_node = rcl_get_zero_initialized_node();
    s_key_state_pub = rcl_get_zero_initialized_publisher();
    s_mcu_status_pub = rcl_get_zero_initialized_publisher();
    s_led_cmd_sub = rcl_get_zero_initialized_subscription();
    s_sync_client = rcl_get_zero_initialized_client();
    s_executor = rclc_executor_get_zero_initialized_executor();
}

/**
 * @brief 把LED业务状态映射到参考工程MCUStatus的状态枚举。
 * @return common_msgs/msg/MCUStatus中的STATE_xxx值。
 */
static uint8_t micro_ros_get_mcu_state(void)
{
    switch ((led_task_system_state_t)led_task_get_system_state())
    {
        case LED_TASK_SYSTEM_IDLE:
            return common_msgs__msg__MCUStatus__STATE_IDLE;

        case LED_TASK_SYSTEM_COLLECTING:
            return common_msgs__msg__MCUStatus__STATE_READY;

        case LED_TASK_SYSTEM_ERROR:
            return common_msgs__msg__MCUStatus__STATE_ERROR;

        case LED_TASK_SYSTEM_UPDATING:
            return common_msgs__msg__MCUStatus__STATE_UPDATING;

        case LED_TASK_SYSTEM_SELF_TEST:
        case LED_TASK_SYSTEM_PREPARING:
        default:
            return common_msgs__msg__MCUStatus__STATE_CALIBRATING;
    }
}

/**
 * @brief 用同步基准填写ROS时间戳。
 * @param[out] stamp 非空时间戳指针；未同步时写0。
 */
static void micro_ros_fill_stamp(builtin_interfaces__msg__Time *stamp)
{
    if (!s_time_synced)
    {
        stamp->sec = 0;
        stamp->nanosec = 0U;
        return;
    }

    const uint64_t uptime_ns =
        (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS * 1000000ULL;
    const int64_t current_ns = s_epoch_base_ns + (int64_t)uptime_ns;
    stamp->sec = (int32_t)(current_ns / 1000000000LL);
    stamp->nanosec = (uint32_t)(current_ns % 1000000000LL);
}

/** @brief 接收PC端LedCmd并把合法cmd覆盖写入LED任务邮箱。 */
static void micro_ros_led_cmd_callback(const void *message)
{
    const common_msgs__msg__LedCmd *led_cmd =
        (const common_msgs__msg__LedCmd *)message;
    if (NULL == led_cmd)
    {
        return;
    }

    ++s_message_rx_count;
    (void)led_task_submit_cmd(led_cmd->led_mode);
}

/** @brief 接收PC同步服务响应并建立ROS时间和FreeRTOS tick的换算基准。 */
static void micro_ros_sync_callback(const void *message)
{
    const common_msgs__srv__DeviceSynchronization_Response *response =
        (const common_msgs__srv__DeviceSynchronization_Response *)message;

    s_sync_pending = false;
    if ((NULL == response) || !response->sync_state)
    {
        return;
    }

    const int64_t response_ns =
        (int64_t)response->header.stamp.sec * 1000000000LL +
        response->header.stamp.nanosec;
    const uint64_t uptime_ns =
        (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS * 1000000ULL;
    s_epoch_base_ns = response_ns - (int64_t)uptime_ns;
    s_time_synced = true;
}

/**
 * @brief 创建本阶段所需的2个Publisher、1个Subscription和1个Client。
 * @retval TASK_OK 全部实体创建完成。
 * @retval TASK_ERROR 任一步骤失败。
 */
static task_status_t micro_ros_create_entities(void)
{
    micro_ros_zero_entities();
    s_allocator = rcl_get_default_allocator();

    rcl_init_options_t init_options =
        rcl_get_zero_initialized_init_options();
    rcl_ret_t result = rcl_init_options_init(&init_options, s_allocator);
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }

    result = rcl_init_options_set_domain_id(&init_options,
                                            MICRO_ROS_DOMAIN_ID);
    if (RCL_RET_OK != result)
    {
        const rcl_ret_t fini_result =
            rcl_init_options_fini(&init_options);
        (void)fini_result;
        return TASK_ERROR;
    }

    result = rclc_support_init_with_options(&s_support,
                                            0,
                                            NULL,
                                            &init_options,
                                            &s_allocator);
    const rcl_ret_t init_options_fini_result =
        rcl_init_options_fini(&init_options);
    (void)init_options_fini_result;
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }
    s_entity_flags.support = true;

    result = rclc_node_init_default(&s_node,
                                    MICRO_ROS_NODE_NAME,
                                    "",
                                    &s_support);
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }
    s_entity_flags.node = true;

    result = rclc_publisher_init_default(
        &s_key_state_pub,
        &s_node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(common_msgs, msg, ButtonEvent),
        MICRO_ROS_KEY_TOPIC);
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }
    s_entity_flags.key_pub = true;

    result = rclc_publisher_init_default(
        &s_mcu_status_pub,
        &s_node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(common_msgs, msg, MCUStatus),
        MICRO_ROS_STATUS_TOPIC);
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }
    s_entity_flags.status_pub = true;

    const rosidl_message_type_support_t *led_cmd_type_support =
        ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(
            rosidl_typesupport_microxrcedds_c,
            common_msgs,
            msg,
            LedCmd)();
    result = rclc_subscription_init_default(&s_led_cmd_sub,
                                            &s_node,
                                            led_cmd_type_support,
                                            MICRO_ROS_LED_CMD_TOPIC);
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }
    s_entity_flags.led_cmd_sub = true;

    result = rclc_client_init_default(
        &s_sync_client,
        &s_node,
        ROSIDL_GET_SRV_TYPE_SUPPORT(common_msgs,
                                    srv,
                                    DeviceSynchronization),
        MICRO_ROS_SYNC_SERVICE);
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }
    s_entity_flags.sync_client = true;

    result = rclc_executor_init(&s_executor,
                                &s_support.context,
                                MICRO_ROS_EXECUTOR_HANDLES,
                                &s_allocator);
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }
    s_entity_flags.executor = true;

    result = rclc_executor_add_subscription(&s_executor,
                                            &s_led_cmd_sub,
                                            &s_led_cmd_msg,
                                            micro_ros_led_cmd_callback,
                                            ON_NEW_DATA);
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }

    result = rclc_executor_add_client(&s_executor,
                                      &s_sync_client,
                                      &s_sync_response,
                                      micro_ros_sync_callback);
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }

    return TASK_OK;
}

/** @brief 按逆序释放已成功创建的ROS实体，允许后续重新连接。 */
static void micro_ros_fini_entities(void)
{
    if (s_entity_flags.support)
    {
        rmw_context_t *rmw_context =
            rcl_context_get_rmw_context(&s_support.context);
        if (NULL != rmw_context)
        {
            (void)rmw_uros_set_context_entity_destroy_session_timeout(
                rmw_context, 0);
        }
    }

    rcl_ret_t fini_result = RCL_RET_OK;
    if (s_entity_flags.executor)
    {
        fini_result = rclc_executor_fini(&s_executor);
    }
    if (s_entity_flags.led_cmd_sub)
    {
        fini_result = rcl_subscription_fini(&s_led_cmd_sub, &s_node);
    }
    if (s_entity_flags.sync_client)
    {
        fini_result = rcl_client_fini(&s_sync_client, &s_node);
    }
    if (s_entity_flags.status_pub)
    {
        fini_result = rcl_publisher_fini(&s_mcu_status_pub, &s_node);
    }
    if (s_entity_flags.key_pub)
    {
        fini_result = rcl_publisher_fini(&s_key_state_pub, &s_node);
    }
    if (s_entity_flags.node)
    {
        fini_result = rcl_node_fini(&s_node);
    }
    if (s_entity_flags.support)
    {
        fini_result = rclc_support_fini(&s_support);
    }
    (void)fini_result;

    micro_ros_zero_entities();
}

/** @brief 发布Queue中当前积压的按键动作。 */
static void micro_ros_publish_key_events(void)
{
    uint8_t event_type = 0U;
    while (pdPASS == xQueueReceive(s_key_event_queue, &event_type, 0U))
    {//包含的八个队列容量
        micro_ros_fill_stamp(&s_key_state_msg.header.stamp);
        s_key_state_msg.event_type = event_type;

        if (RCL_RET_OK == rcl_publish(&s_key_state_pub,
                                      &s_key_state_msg,
                                      NULL))// 发布按键状态消息
        {
            ++s_message_tx_count;
        }
    }
}

/** @brief 发布一帧1 Hz MCU状态。 */
static void micro_ros_publish_mcu_status(void)
{
    micro_ros_fill_stamp(&s_mcu_status_msg.header.stamp);
    s_mcu_status_msg.uptime_seconds =
        (uint32_t)(((uint64_t)xTaskGetTickCount() *
                    portTICK_PERIOD_MS) /
                   1000ULL);
    s_mcu_status_msg.system_state = micro_ros_get_mcu_state();//LED业务状态映射到参考工程MCUStatus的状态枚举
    s_mcu_status_msg.agent_connected = s_agent_connected;
    s_mcu_status_msg.message_tx_count = s_message_tx_count;
    s_mcu_status_msg.message_rx_count = s_message_rx_count;

    if (RCL_RET_OK == rcl_publish(&s_mcu_status_pub,
                                  &s_mcu_status_msg,
                                  NULL))
    {
        ++s_message_tx_count;
    }
}

/** @brief 未同步时按固定周期发送一次时间同步请求。 */
static void micro_ros_process_time_sync(TickType_t current_tick)
{
    if (s_time_synced)
    {
        return;
    }

    if (s_sync_pending &&
        (pdMS_TO_TICKS(MICRO_ROS_SYNC_RESPONSE_MS) <=
         (current_tick - s_sync_request_tick)))
    {
        s_sync_pending = false;
    }

    if (!s_sync_pending &&
        (pdMS_TO_TICKS(MICRO_ROS_SYNC_RETRY_MS) <=
         (current_tick - s_sync_request_tick)))
    {
        if (RCL_RET_OK == rcl_send_request(&s_sync_client,
                                           &s_sync_request,
                                           &s_sync_sequence))
        {
            s_sync_pending = true;
            s_sync_request_tick = current_tick;
        }
    }
}

task_status_t micro_ros_task_resources_init(void)
{
    if (NULL != s_key_event_queue)
    {
        return TASK_OK;
    }

    //创建队列 大小为8  用于存储按键事件
    s_key_event_queue = xQueueCreate(MICRO_ROS_KEY_QUEUE_LENGTH,
                                     sizeof(uint8_t));
    if (NULL == s_key_event_queue)
    {
        return TASK_ERROR_NO_MEMORY;
    }

    return TASK_OK;
}

task_status_t micro_ros_task_enqueue_key_event(uint8_t event_type)
{
    if ((common_msgs__msg__ButtonEvent__EVENT_LONG_PRESS > event_type) ||
        (common_msgs__msg__ButtonEvent__EVENT_ERROR_ACK < event_type))
    {
        return TASK_ERROR_PARAMETER;
    }

    if ((NULL == s_key_event_queue) || !s_agent_connected)
    {
        return TASK_ERROR_RESOURCE;
    }

    if (pdPASS != xQueueSendToBack(s_key_event_queue,
                                   &event_type,
                                   0U))
    {
        ++s_key_drop_count;
        return TASK_ERROR;
    }

    return TASK_OK;
}

void micro_ros_task_entry(void *argument)
{
    if (!micro_ros_init_allocator())
    {
        vTaskSuspend(NULL);
    }

    micro_ros_init_messages();//ROS消息对象绑定到全部静态字符串缓冲区
    micro_ros_zero_entities();//初始化ROS实体

    if (RMW_RET_OK != rmw_uros_set_custom_transport(
                          true,
                          &huart2,
                          cubemx_transport_open,
                          cubemx_transport_close,
                          cubemx_transport_write,
                          cubemx_transport_read))
    {
        vTaskSuspend(NULL);
    }

    micro_ros_state_t state = MICRO_ROS_STATE_WAIT_AGENT;
    uint8_t ping_failure_count = 0U;
    TickType_t last_status_tick = xTaskGetTickCount();
    TickType_t last_health_tick = xTaskGetTickCount();

    for (;;)
    {
        if (MICRO_ROS_STATE_WAIT_AGENT == state)
        {
            s_agent_connected = false;
            if (RMW_RET_OK == rmw_uros_ping_agent(
                                  MICRO_ROS_AGENT_PING_TIMEOUT_MS,
                                  MICRO_ROS_AGENT_PING_ATTEMPTS))
            {
                if (TASK_OK == micro_ros_create_entities())
                {
                    s_agent_connected = true;
                    s_time_synced = false;
                    s_sync_pending = false;
                    s_sync_request_tick =
                        xTaskGetTickCount() -
                        pdMS_TO_TICKS(MICRO_ROS_SYNC_RETRY_MS);
                    ping_failure_count = 0U;
                    last_status_tick = xTaskGetTickCount();
                    last_health_tick = xTaskGetTickCount();
                    state = MICRO_ROS_STATE_RUNNING;
                }
                else
                {
                    micro_ros_fini_entities();
                }
            }

            vTaskDelay(pdMS_TO_TICKS(MICRO_ROS_AGENT_WAIT_MS));
            continue;
        }

        (void)rclc_executor_spin_some(
            &s_executor,
            RCL_MS_TO_NS(MICRO_ROS_EXECUTOR_TIMEOUT_MS));
        micro_ros_publish_key_events();

        const TickType_t current_tick = xTaskGetTickCount();
        micro_ros_process_time_sync(current_tick);

        if (pdMS_TO_TICKS(MICRO_ROS_STATUS_PERIOD_MS) <=
            (current_tick - last_status_tick))
        {
            last_status_tick = current_tick;
            micro_ros_publish_mcu_status();
        }

        if (pdMS_TO_TICKS(MICRO_ROS_HEALTH_PERIOD_MS) <=
            (current_tick - last_health_tick))
        {
            last_health_tick = current_tick;
            if (RMW_RET_OK != rmw_uros_ping_agent(
                                  MICRO_ROS_AGENT_PING_TIMEOUT_MS,
                                  MICRO_ROS_AGENT_PING_ATTEMPTS))
            {
                ++ping_failure_count;
            }
            else
            {
                ping_failure_count = 0U;
            }

            if (MICRO_ROS_AGENT_FAILURE_LIMIT <= ping_failure_count)
            {
                s_agent_connected = false;
                s_time_synced = false;
                s_sync_pending = false;
                xQueueReset(s_key_event_queue);
                (void)led_task_release_remote_control();
                micro_ros_fini_entities();
                state = MICRO_ROS_STATE_WAIT_AGENT;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MICRO_ROS_LOOP_DELAY_MS));
    }
}
