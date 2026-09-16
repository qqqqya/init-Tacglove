// Generated-equivalent C structure for common_msgs/msg/KeyState.

#ifndef COMMON_MSGS__MSG__DETAIL__KEY_STATE__STRUCT_H_
#define COMMON_MSGS__MSG__DETAIL__KEY_STATE__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>

#include "std_msgs/msg/detail/header__struct.h"

enum
{
    common_msgs__msg__KeyState__EVENT_LONG_PRESS = 1,
    common_msgs__msg__KeyState__EVENT_REC_TOGGLE = 2,
    common_msgs__msg__KeyState__EVENT_DOUBLE_CLICK = 3,
    common_msgs__msg__KeyState__EVENT_ERROR_ACK = 4
};

/** @brief 已经完成消抖和动作识别的按键状态消息。 */
typedef struct common_msgs__msg__KeyState
{
    std_msgs__msg__Header header;
    uint8_t event_type;
} common_msgs__msg__KeyState;

typedef struct common_msgs__msg__KeyState__Sequence
{
    common_msgs__msg__KeyState *data;
    size_t size;
    size_t capacity;
} common_msgs__msg__KeyState__Sequence;

#ifdef __cplusplus
}
#endif

#endif /* COMMON_MSGS__MSG__DETAIL__KEY_STATE__STRUCT_H_ */
