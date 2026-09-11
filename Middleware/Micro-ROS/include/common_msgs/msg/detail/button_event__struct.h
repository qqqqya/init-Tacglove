// NOLINT: This file starts with a BOM since it contain non-ASCII characters
// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from common_msgs:msg/ButtonEvent.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "common_msgs/msg/button_event.h"


#ifndef COMMON_MSGS__MSG__DETAIL__BUTTON_EVENT__STRUCT_H_
#define COMMON_MSGS__MSG__DETAIL__BUTTON_EVENT__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants defined in the message

/// Constant 'EVENT_LONG_PRESS'.
/**
  * 按钮状态常量
 */
enum
{
  common_msgs__msg__ButtonEvent__EVENT_LONG_PRESS = 1
};

/// Constant 'EVENT_REC_TOGGLE'.
enum
{
  common_msgs__msg__ButtonEvent__EVENT_REC_TOGGLE = 2
};

/// Constant 'EVENT_DOUBLE_CLICK'.
enum
{
  common_msgs__msg__ButtonEvent__EVENT_DOUBLE_CLICK = 3
};

/// Constant 'EVENT_ERROR_ACK'.
enum
{
  common_msgs__msg__ButtonEvent__EVENT_ERROR_ACK = 4
};

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.h"

/// Struct defined in msg/ButtonEvent in the package common_msgs.
typedef struct common_msgs__msg__ButtonEvent
{
  std_msgs__msg__Header header;
  /// 按键状态
  uint8_t event_type;
} common_msgs__msg__ButtonEvent;

// Struct for a sequence of common_msgs__msg__ButtonEvent.
typedef struct common_msgs__msg__ButtonEvent__Sequence
{
  common_msgs__msg__ButtonEvent * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} common_msgs__msg__ButtonEvent__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // COMMON_MSGS__MSG__DETAIL__BUTTON_EVENT__STRUCT_H_
