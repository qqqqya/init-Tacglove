// Generated-equivalent C structure for common_msgs/msg/LedCmd.

#ifndef COMMON_MSGS__MSG__DETAIL__LED_CMD__STRUCT_H_
#define COMMON_MSGS__MSG__DETAIL__LED_CMD__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>

#include "std_msgs/msg/detail/header__struct.h"

enum
{
    common_msgs__msg__LedCmd__MODE_OFF = 0,
    common_msgs__msg__LedCmd__MODE_GREEN_SOLID = 1,
    common_msgs__msg__LedCmd__MODE_GREEN_BLINK = 2,
    common_msgs__msg__LedCmd__MODE_RED_SOLID = 3,
    common_msgs__msg__LedCmd__MODE_BLUE_BLINK = 4,
    common_msgs__msg__LedCmd__MODE_BLUE_SOLID = 5,
    common_msgs__msg__LedCmd__MODE_BEEP_OFF = 0,
    common_msgs__msg__LedCmd__MODE_SHORT_BEEP = 1,
    common_msgs__msg__LedCmd__MODE_LONG_BEEP = 2,
    common_msgs__msg__LedCmd__MODE_BEEPING = 3
};

/** @brief Six-LED command in logical LED2 through LED7 order. */
typedef struct common_msgs__msg__LedCmd
{
    std_msgs__msg__Header header;
    uint8_t led_mode[6];
    uint8_t beep_mode;
} common_msgs__msg__LedCmd;

typedef struct common_msgs__msg__LedCmd__Sequence
{
    common_msgs__msg__LedCmd *data;
    size_t size;
    size_t capacity;
} common_msgs__msg__LedCmd__Sequence;

#ifdef __cplusplus
}
#endif

#endif /* COMMON_MSGS__MSG__DETAIL__LED_CMD__STRUCT_H_ */
