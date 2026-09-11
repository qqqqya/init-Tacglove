// NOLINT: This file starts with a BOM since it contain non-ASCII characters
// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from common_msgs:msg/EncoderState.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "common_msgs/msg/encoder_state.h"


#ifndef COMMON_MSGS__MSG__DETAIL__ENCODER_STATE__STRUCT_H_
#define COMMON_MSGS__MSG__DETAIL__ENCODER_STATE__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants defined in the message

/// Constant 'ENCODER_OK'.
/**
  * 编码器状态常量
 */
enum
{
  common_msgs__msg__EncoderState__ENCODER_OK = 0
};

/// Constant 'ENCODER_ERROR'.
enum
{
  common_msgs__msg__EncoderState__ENCODER_ERROR = 1
};

/// Constant 'ENCODER_CALIBRATING'.
/**
  * 编码器标定/归零
 */
enum
{
  common_msgs__msg__EncoderState__ENCODER_CALIBRATING = 2
};

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.h"

/**
 * @brief 时间戳和frame_id
 * @details enc角度 状态
 * @param gripper_angle 夹爪角度 (度)
 * @param encoder_raw_value 编码器原始值 (14bit?)
 * @param encoder_status 编码器状态 (正常/异常)
 */
/// Struct defined in msg/EncoderState in the package common_msgs.
typedef struct common_msgs__msg__EncoderState
{
  /// 时间戳和frame_id
  std_msgs__msg__Header header;
  /// 编码器数值
  /// 夹爪角度 (度)
  float gripper_angle;
  /// 编码器原始值 (14bit?)
  uint16_t encoder_raw_value;
  /// 编码器状态
  /// 编码器状态 (正常/异常)
  uint8_t encoder_status;
} common_msgs__msg__EncoderState;

/**
 * @brief 编码器状态序列
 * @param data 编码器状态序列
 * @param size 有效项数量
 * @param capacity 分配项数量
 */
// Struct for a sequence of common_msgs__msg__EncoderState.
typedef struct common_msgs__msg__EncoderState__Sequence
{
  common_msgs__msg__EncoderState * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} common_msgs__msg__EncoderState__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // COMMON_MSGS__MSG__DETAIL__ENCODER_STATE__STRUCT_H_
