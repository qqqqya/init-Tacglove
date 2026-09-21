// generated from rosidl_generator_c/resource/idl__struct.h.em
// with input from common_msgs:srv/DeviceSynchronization.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "common_msgs/srv/device_synchronization.h"


#ifndef COMMON_MSGS__SRV__DETAIL__DEVICE_SYNCHRONIZATION__STRUCT_H_
#define COMMON_MSGS__SRV__DETAIL__DEVICE_SYNCHRONIZATION__STRUCT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Constants defined in the message

/// Struct defined in srv/DeviceSynchronization in the package common_msgs.
typedef struct common_msgs__srv__DeviceSynchronization_Request
{
  bool sync_request;
} common_msgs__srv__DeviceSynchronization_Request;

// Struct for a sequence of common_msgs__srv__DeviceSynchronization_Request.
typedef struct common_msgs__srv__DeviceSynchronization_Request__Sequence
{
  common_msgs__srv__DeviceSynchronization_Request * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} common_msgs__srv__DeviceSynchronization_Request__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'header'
#include "std_msgs/msg/detail/header__struct.h"

/// Struct defined in srv/DeviceSynchronization in the package common_msgs.
typedef struct common_msgs__srv__DeviceSynchronization_Response
{
  std_msgs__msg__Header header;
  bool sync_state;
} common_msgs__srv__DeviceSynchronization_Response;

// Struct for a sequence of common_msgs__srv__DeviceSynchronization_Response.
typedef struct common_msgs__srv__DeviceSynchronization_Response__Sequence
{
  common_msgs__srv__DeviceSynchronization_Response * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} common_msgs__srv__DeviceSynchronization_Response__Sequence;

// Constants defined in the message

// Include directives for member types
// Member 'info'
#include "service_msgs/msg/detail/service_event_info__struct.h"

// constants for array fields with an upper bound
// request
enum
{
  common_msgs__srv__DeviceSynchronization_Event__request__MAX_SIZE = 1
};
// response
enum
{
  common_msgs__srv__DeviceSynchronization_Event__response__MAX_SIZE = 1
};

/// Struct defined in srv/DeviceSynchronization in the package common_msgs.
typedef struct common_msgs__srv__DeviceSynchronization_Event
{
  service_msgs__msg__ServiceEventInfo info;
  common_msgs__srv__DeviceSynchronization_Request__Sequence request;
  common_msgs__srv__DeviceSynchronization_Response__Sequence response;
} common_msgs__srv__DeviceSynchronization_Event;

// Struct for a sequence of common_msgs__srv__DeviceSynchronization_Event.
typedef struct common_msgs__srv__DeviceSynchronization_Event__Sequence
{
  common_msgs__srv__DeviceSynchronization_Event * data;
  /// The number of valid items in data
  size_t size;
  /// The number of allocated items in data
  size_t capacity;
} common_msgs__srv__DeviceSynchronization_Event__Sequence;

#ifdef __cplusplus
}
#endif

#endif  // COMMON_MSGS__SRV__DETAIL__DEVICE_SYNCHRONIZATION__STRUCT_H_
