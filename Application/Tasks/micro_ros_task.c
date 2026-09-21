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

#include "common_msgs/msg/key_state.h"
#include "common_msgs/msg/led_cmd.h"
#include "common_msgs/msg/mcu_status.h"
#include "common_msgs/srv/device_synchronization.h"

#include "rcl/rcl.h"
#include "rclc/executor.h"
#include "rclc/rclc.h"
#include "rcutils/allocator.h"
#include "rmw_microros/rmw_microros.h"

static QueueHandle_t s_key_event_queue;
static volatile bool s_agent_connected;     // 是否已连接到agent
static uint32_t s_message_tx_count;         // 发送的消息计数
static uint32_t s_message_rx_count;         // 接收的消息计数
static uint32_t s_key_drop_count;           // 按键事件丢弃计数

static rcl_allocator_t s_allocator;         // ROS allocator
static rclc_support_t s_support;
static rcl_node_t s_node;
static rcl_publisher_t s_key_state_pub;
static rcl_publisher_t s_mcu_status_pub;    
static rcl_subscription_t s_led_cmd_sub;    
static rcl_client_t s_sync_client;          
static rclc_executor_t s_executor;          // ROS executor
static micro_ros_entity_flags_t s_entity_flags;

static common_msgs__msg__KeyState s_key_state_msg;
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
static void micro_ros_init_messages(void){
    
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
    s_sync_request.sync_request = true;// 请求同步

    memset(&s_sync_response, 0, sizeof(s_sync_response));
    s_sync_response.header.frame_id.data = s_sync_frame_buffer;
    s_sync_response.header.frame_id.capacity = sizeof(s_sync_frame_buffer);
}

/**
 * @brief 配置micro-ROS专用分配器。
 * @return true表示rcutils已接受分配器。
 */
static bool micro_ros_init_allocator(void){
    rcutils_allocator_t allocator = rcutils_get_zero_initialized_allocator();
    allocator.allocate = microros_allocate;             // ← 自定义的 malloc
    allocator.deallocate = microros_deallocate;         // ← 自定义的 free
    allocator.reallocate = microros_reallocate;         // ← 自定义的 realloc    
    allocator.zero_allocate = microros_zero_allocate;   // ← 自定义的 calloc    
    allocator.state = NULL;

    return rcutils_set_default_allocator(&allocator);
}


/** @brief 将全部rcl句柄恢复为可重新初始化的零状态。 */
static void micro_ros_zero_entities(void){
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
static uint8_t micro_ros_get_mcu_state(void){
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
static void micro_ros_fill_stamp(builtin_interfaces__msg__Time *stamp){
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

/** @brief 接收PC端LedCmd并把合法灯光和蜂鸣器cmd写入任务邮箱。 */
static void micro_ros_led_cmd_callback(const void *message){
    const common_msgs__msg__LedCmd *led_cmd =
        (const common_msgs__msg__LedCmd *)message;
    if (NULL == led_cmd)
    {
        return;
    }

    ++s_message_rx_count;   //回调的时候执行ledtask中的submit--写入任务邮箱
    (void)led_task_submit_cmd(led_cmd->led_mode, led_cmd->beep_mode);
}

/** @brief 接收PC同步服务响应并建立ROS时间和FreeRTOS tick的换算基准。 */
static void micro_ros_sync_callback(const void *message){
    const common_msgs__srv__DeviceSynchronization_Response *response =
        (const common_msgs__srv__DeviceSynchronization_Response *)message;

    s_sync_pending = false;                             // 无论成功与否，取消 pending 状态
    if ((NULL == response) || !response->sync_state)    // ← 检查 response 是否有效
    {
        return;                                 // response 为空或 sync_state=false
    }
    // 到这里说明成功，进行时间基准换算
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
static task_status_t micro_ros_create_entities(void){
    micro_ros_zero_entities();

    // Allocator + Domain ID
    s_allocator = rcl_get_default_allocator();              //获取已注册的分配器
    rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();//用该分配器初始化配置
    rcl_ret_t result = rcl_init_options_init(&init_options, s_allocator);
    if (RCL_RET_OK != result){
        return TASK_ERROR;
    }
    result = rcl_init_options_set_domain_id(&init_options,MICRO_ROS_DOMAIN_ID);//设置域ID
    if (RCL_RET_OK != result){
        const rcl_ret_t fini_result = rcl_init_options_fini(&init_options);
        (void)fini_result; //ignore ret
        return TASK_ERROR;
    }
    result = rclc_support_init_with_options(&s_support,         //初始化支持support
                                            0,
                                            NULL,
                                            &init_options,
                                            &s_allocator);
    const rcl_ret_t init_options_fini_result =
        rcl_init_options_fini(&init_options);
    (void)init_options_fini_result;
    if (RCL_RET_OK != result){
        return TASK_ERROR;
    }
    s_entity_flags.support = true;

    
    // 创建节点
    result = rclc_node_init_default(&s_node,
                                    MICRO_ROS_NODE_NAME,
                                    "",
                                    &s_support);
    if (RCL_RET_OK != result){
        return TASK_ERROR;
    }
    s_entity_flags.node = true;


    // 创建按键state publisher
    result = rclc_publisher_init_default(&s_key_state_pub,
                                         &s_node,
                                         ROSIDL_GET_MSG_TYPE_SUPPORT(
                                             common_msgs,
                                             msg,
                                             KeyState),
                                         MICRO_ROS_KEY_TOPIC);
    if (RCL_RET_OK != result){
        return TASK_ERROR;
    }
    s_entity_flags.key_pub = true;

    // 创建MCU status publisher
    result = rclc_publisher_init_default(
        &s_mcu_status_pub,
        &s_node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(common_msgs, msg, MCUStatus),
        MICRO_ROS_STATUS_TOPIC);
    if (RCL_RET_OK != result){
        return TASK_ERROR;
    }
    s_entity_flags.status_pub = true;

    // 创建LED cmd sub
    result = rclc_subscription_init_default(&s_led_cmd_sub,
                                            &s_node,
                                            ROSIDL_GET_MSG_TYPE_SUPPORT(
                                                common_msgs,
                                                msg,
                                                LedCmd),
                                            MICRO_ROS_LED_CMD_TOPIC);
    if (RCL_RET_OK != result)
    {
        return TASK_ERROR;
    }
    s_entity_flags.led_cmd_sub = true;

    // 创建同步服务客户端---mcu client
    result = rclc_client_init_default(
        &s_sync_client,
        &s_node,
        ROSIDL_GET_SRV_TYPE_SUPPORT(common_msgs, srv,DeviceSynchronization),
        MICRO_ROS_SYNC_SERVICE);
    if (RCL_RET_OK != result){
        return TASK_ERROR;
    }
    s_entity_flags.sync_client = true;

    // 创建ROS executor
    result = rclc_executor_init(&s_executor,        //初始化ROS executor
                                &s_support.context,
                                MICRO_ROS_EXECUTOR_HANDLES,
                                &s_allocator);
    if (RCL_RET_OK != result){
        return TASK_ERROR;
    }
    s_entity_flags.executor = true;

    result = rclc_executor_add_subscription(&s_executor,
                                            &s_led_cmd_sub,
                                            &s_led_cmd_msg,
                                            micro_ros_led_cmd_callback,
                                            ON_NEW_DATA);
    if (RCL_RET_OK != result){
        return TASK_ERROR;
    }

    result = rclc_executor_add_client(&s_executor,              //这里的 client是相对于server的 是同步服务客户端
                                      &s_sync_client,
                                      &s_sync_response,
                                      micro_ros_sync_callback);
    if (RCL_RET_OK != result){
        return TASK_ERROR;
    }

    return TASK_OK;
}

/** @brief 按逆序释放已成功创建的ROS实体，允许后续重新连接。 */
static void micro_ros_fini_entities(void){
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
static void micro_ros_publish_key_events(void){
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
static void micro_ros_publish_mcu_status(void){
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
static void micro_ros_process_time_sync(TickType_t current_tick){
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

task_status_t micro_ros_task_resources_init(void){
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

task_status_t micro_ros_task_enqueue_key_event(uint8_t event_type){//将已识别的按键事件送入micro-ROS发布Queue。
    if ((common_msgs__msg__KeyState__EVENT_LONG_PRESS > event_type) ||
        (common_msgs__msg__KeyState__EVENT_ERROR_ACK < event_type))
    {
        return TASK_ERROR_PARAMETER;
    }

    if ((NULL == s_key_event_queue) || !s_agent_connected)
    {
        return TASK_ERROR_RESOURCE;
    }

    if (pdPASS != xQueueSendToBack(s_key_event_queue,   //添加按键事件到队列尾部
                                   &event_type,
                                   0U))
    {
        ++s_key_drop_count;
        return TASK_ERROR;
    }

    return TASK_OK;
}

void micro_ros_task_entry(void *argument){
    if (!micro_ros_init_allocator())
    {
        vTaskSuspend(NULL);
    }

    micro_ros_init_messages();//ROS消息对象绑定到全部静态字符串缓冲区
    micro_ros_zero_entities();//初始化ROS实体

    if (RMW_RET_OK != rmw_uros_set_custom_transport(//handheld代码里面这个是在 micro_ros_init 中包含着的
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
            s_agent_connected = false;  //若没有连接到Agent，重置连接状态
            if (RMW_RET_OK == rmw_uros_ping_agent(MICRO_ROS_AGENT_PING_TIMEOUT_MS,
                                  MICRO_ROS_AGENT_PING_ATTEMPTS))//ping agent 100ms
            {   // Ping 通 → 创建实体，进入 RUNNING 状态
                if (TASK_OK == micro_ros_create_entities()) //创建executor
                                                            //ROS实体Publisher、Subscription和Client
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
                    micro_ros_fini_entities();//创建失败，释放ROS实体 类似deinit
                }
            }
            vTaskDelay(pdMS_TO_TICKS(MICRO_ROS_AGENT_WAIT_MS));
            continue;
        }

        // ros running 状态下，等待ROS事件发生 --最多5ms等待
        (void)rclc_executor_spin_some(      //等待ROS事件发生 --最多5ms等待
            &s_executor,
            RCL_MS_TO_NS(MICRO_ROS_EXECUTOR_TIMEOUT_MS));
            
        //发布Queue中当前积压的按键动作。    
        micro_ros_publish_key_events();     

        //时间同步  发送request 
        const TickType_t current_tick = xTaskGetTickCount();
        micro_ros_process_time_sync(current_tick);//内部send request 相应的  client executor的callback会发送response

        if (pdMS_TO_TICKS(MICRO_ROS_STATUS_PERIOD_MS) <=
            (current_tick - last_status_tick))      //每1 s发布一次MCU状态
        {
            last_status_tick = current_tick;
            micro_ros_publish_mcu_status();
        }

        // ros running 状态下，每10 s检查一次Agent是否连接
        // 运行中检查健康状态
        if (pdMS_TO_TICKS(MICRO_ROS_HEALTH_PERIOD_MS) <=
            (current_tick - last_health_tick))
        {
            last_health_tick = current_tick;
            if (RMW_RET_OK != rmw_uros_ping_agent(/////agent  启动  打了断点  这里会停
                                  MICRO_ROS_AGENT_PING_TIMEOUT_MS,
                                  MICRO_ROS_AGENT_PING_ATTEMPTS))
            {
                ++ping_failure_count;
            }
            else
            {
                ping_failure_count = 0U;
            }

        // 连续失败达到阈值 → 断开，回到 WAIT_AGENT
            if (MICRO_ROS_AGENT_FAILURE_LIMIT <= ping_failure_count)
            {
                s_agent_connected = false;
                s_time_synced = false;
                s_sync_pending = false;
                xQueueReset(s_key_event_queue);
                (void)led_task_release_remote_control();//释放PC远程灯光控制并恢复本地按键状态显示。
                micro_ros_fini_entities();          //清理实体，回到等待状态  释放ROS实体 类似deinit
                state = MICRO_ROS_STATE_WAIT_AGENT;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MICRO_ROS_LOOP_DELAY_MS));
    }
}
