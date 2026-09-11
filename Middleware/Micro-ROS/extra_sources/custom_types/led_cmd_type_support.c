/**
 * @file led_cmd_type_support.c
 * @brief common_msgs/msg/LedCmd的micro XRCE-DDS CDR类型支持。
 * @details 本文件等价于rosidl_typesupport_microxrcedds_c针对
 *          `std_msgs/Header header + uint8[6] led_mode`生成的核心代码。
 */
#include "common_msgs/msg/led_cmd.h"

#include <stdint.h>

#include "rosidl_typesupport_microxrcedds_c/identifier.h"
#include "rosidl_typesupport_microxrcedds_c/message_type_support.h"
#include "std_msgs/msg/detail/header__rosidl_typesupport_microxrcedds_c.h"
#include "ucdr/microcdr.h"

#define LED_CMD_MODE_COUNT 6U

/** @brief 获取std_msgs/Header的micro XRCE-DDS回调。 */
static const message_type_support_callbacks_t *led_cmd_header_callbacks(void)
{
    const rosidl_message_type_support_t *header_type_support =
        ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(
            rosidl_typesupport_microxrcedds_c,
            std_msgs,
            msg,
            Header)();

    return (const message_type_support_callbacks_t *)
        header_type_support->data;
}

/** @brief 将LedCmd序列化为CDR。 */
static bool led_cmd_cdr_serialize(const void *untyped_ros_message,
                                  ucdrBuffer *cdr)
{
    const common_msgs__msg__LedCmd *message =
        (const common_msgs__msg__LedCmd *)untyped_ros_message;
    const message_type_support_callbacks_t *header_callbacks =
        led_cmd_header_callbacks();

    if ((NULL == message) || (NULL == header_callbacks))
    {
        return false;
    }

    if (!header_callbacks->cdr_serialize(&message->header, cdr))
    {
        return false;
    }

    return ucdr_serialize_array_uint8_t(cdr,
                                        message->led_mode,
                                        LED_CMD_MODE_COUNT);
}

/** @brief 从CDR反序列化LedCmd。 */
static bool led_cmd_cdr_deserialize(ucdrBuffer *cdr,
                                    void *untyped_ros_message)
{
    common_msgs__msg__LedCmd *message =
        (common_msgs__msg__LedCmd *)untyped_ros_message;
    const message_type_support_callbacks_t *header_callbacks =
        led_cmd_header_callbacks();

    if ((NULL == message) || (NULL == header_callbacks))
    {
        return false;
    }

    if (!header_callbacks->cdr_deserialize(cdr, &message->header))
    {
        return false;
    }

    return ucdr_deserialize_array_uint8_t(cdr,
                                          message->led_mode,
                                          LED_CMD_MODE_COUNT);
}

size_t get_serialized_size_common_msgs__msg__LedCmd(
    const void *untyped_ros_message,
    size_t current_alignment)
{
    const common_msgs__msg__LedCmd *message =
        (const common_msgs__msg__LedCmd *)untyped_ros_message;
    const size_t initial_alignment = current_alignment;

    current_alignment += get_serialized_size_std_msgs__msg__Header(
        &message->header, current_alignment);
    current_alignment += LED_CMD_MODE_COUNT;

    return current_alignment - initial_alignment;
}

size_t max_serialized_size_common_msgs__msg__LedCmd(
    bool *full_bounded,
    size_t current_alignment)
{
    const size_t initial_alignment = current_alignment;

    current_alignment += max_serialized_size_std_msgs__msg__Header(
        full_bounded, current_alignment);
    current_alignment += LED_CMD_MODE_COUNT;

    return current_alignment - initial_alignment;
}

/** @brief 返回从零对齐开始的LedCmd序列化长度。 */
static uint32_t led_cmd_get_serialized_size(const void *untyped_ros_message)
{
    return (uint32_t)get_serialized_size_common_msgs__msg__LedCmd(
        untyped_ros_message, 0U);
}

/** @brief 返回LedCmd的最大序列化长度。 */
static size_t led_cmd_max_serialized_size(void)
{
    bool full_bounded = true;
    return max_serialized_size_common_msgs__msg__LedCmd(&full_bounded, 0U);
}

static message_type_support_callbacks_t s_led_cmd_callbacks = {
    "common_msgs::msg",
    "LedCmd",
    led_cmd_cdr_serialize,
    led_cmd_cdr_deserialize,
    led_cmd_get_serialized_size,
    get_serialized_size_common_msgs__msg__LedCmd,
    led_cmd_max_serialized_size,
};

static rosidl_message_type_support_t s_led_cmd_type_support = {
    ROSIDL_TYPESUPPORT_MICROXRCEDDS_C__IDENTIFIER_VALUE,
    &s_led_cmd_callbacks,
    get_message_typesupport_handle_function,
    NULL,
    NULL,
    NULL,
};

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(
    rosidl_typesupport_microxrcedds_c,
    common_msgs,
    msg,
    LedCmd)(void)
{
    return &s_led_cmd_type_support;
}
