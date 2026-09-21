// NOLINT: This file starts with a BOM since it contain non-ASCII characters
// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from common_msgs:msg/LedCmd.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "common_msgs/msg/led_cmd.h"


#ifndef COMMON_MSGS__MSG__DETAIL__LED_CMD__STRUCT_H_
#define COMMON_MSGS__MSG__DETAIL__LED_CMD__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants defined in the message

/// Constant 'MODE_OFF'.
enum
{
  common_msgs__msg__LedCmd__MODE_OFF = 0
};

/// Constant 'MODE_GREEN_SOLID'.
/**
  * 绿色常亮：自检通过进入待机
 */
enum
{
  common_msgs__msg__LedCmd__MODE_GREEN_SOLID = 1
};

/// Constant 'MODE_GREEN_BLINK'.
/**
  * 绿色闪烁：上电自检中
 */
enum
{
  common_msgs__msg__LedCmd__MODE_GREEN_BLINK = 2
};

/// Constant 'MODE_RED_SOLID'.
/**
  * 红色常亮：故障告警
 */
enum
{
  common_msgs__msg__LedCmd__MODE_RED_SOLID = 3
};

/// Constant 'MODE_BLUE_BLINK'.
/**
  * 蓝色闪烁：数采准备/收尾状态
 */
enum
{
  common_msgs__msg__LedCmd__MODE_BLUE_BLINK = 4
};

/// Constant 'MODE_BLUE_SOLID'.
/**
  * 蓝色常亮：数采工作状态
 */
enum
{
  common_msgs__msg__LedCmd__MODE_BLUE_SOLID = 5
};

/// Constant 'MODE_BEEP_OFF'.
/**
  * 蜂鸣器命令常量
 */
enum
{
  common_msgs__msg__LedCmd__MODE_BEEP_OFF = 0
};

/// Constant 'MODE_SHORT_BEEP'.
enum
{
  common_msgs__msg__LedCmd__MODE_SHORT_BEEP = 1
};

/// Constant 'MODE_LONG_BEEP'.
enum
{
  common_msgs__msg__LedCmd__MODE_LONG_BEEP = 2
};

/// Constant 'MODE_BEEPING'.
enum
{
  common_msgs__msg__LedCmd__MODE_BEEPING = 3
};

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.h"

/// Struct defined in msg/LedCmd in the package common_msgs.
typedef struct common_msgs__msg__LedCmd
{
  std_msgs__msg__Header header;
  /// 六个led模式
  uint8_t led_mode[6];
  /// 蜂鸣器模式
  uint8_t beep_mode;
} common_msgs__msg__LedCmd;

// Struct for a sequence of common_msgs__msg__LedCmd.
typedef struct common_msgs__msg__LedCmd__Sequence
{
  common_msgs__msg__LedCmd * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} common_msgs__msg__LedCmd__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // COMMON_MSGS__MSG__DETAIL__LED_CMD__STRUCT_H_
