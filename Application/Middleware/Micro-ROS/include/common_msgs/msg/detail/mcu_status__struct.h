// NOLINT: This file starts with a BOM since it contain non-ASCII characters
// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from common_msgs:msg/MCUStatus.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "common_msgs/msg/mcu_status.h"


#ifndef COMMON_MSGS__MSG__DETAIL__MCU_STATUS__STRUCT_H_
#define COMMON_MSGS__MSG__DETAIL__MCU_STATUS__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants defined in the message

/// Constant 'STATE_IDLE'.
/**
  * 空闲
 */
enum
{
  common_msgs__msg__MCUStatus__STATE_IDLE = 0
};

/// Constant 'STATE_READY'.
/**
  * 就绪
 */
enum
{
  common_msgs__msg__MCUStatus__STATE_READY = 1
};

/// Constant 'STATE_ERROR'.
/**
  * 错误
 */
enum
{
  common_msgs__msg__MCUStatus__STATE_ERROR = 2
};

/// Constant 'STATE_CALIBRATING'.
/**
  * 校准中
 */
enum
{
  common_msgs__msg__MCUStatus__STATE_CALIBRATING = 3
};

/// Constant 'STATE_UPDATING'.
/**
  * 固件更新中
 */
enum
{
  common_msgs__msg__MCUStatus__STATE_UPDATING = 4
};

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.h"
// Member 'firmware_version'
#include "rosidl_runtime_c/string.h"

/// Struct defined in msg/MCUStatus in the package common_msgs.
typedef struct common_msgs__msg__MCUStatus
{
  std_msgs__msg__Header header;
  /// 固件版本 "预留"
  rosidl_runtime_c__String firmware_version;
  /// 运行时长(秒)
  uint32_t uptime_seconds;
  /// 系统状态
  uint8_t system_state;
  /// 与PC端连接状态
  bool agent_connected;
  /// 发送消息计数
  uint32_t message_tx_count;
  /// 接收消息计数
  uint32_t message_rx_count;
} common_msgs__msg__MCUStatus;

// Struct for a sequence of common_msgs__msg__MCUStatus.
typedef struct common_msgs__msg__MCUStatus__Sequence
{
  common_msgs__msg__MCUStatus * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} common_msgs__msg__MCUStatus__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // COMMON_MSGS__MSG__DETAIL__MCU_STATUS__STRUCT_H_
