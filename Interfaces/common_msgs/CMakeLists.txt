cmake_minimum_required(VERSION 3.8)
project(common_msgs)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
    "msg/KeyState.msg"
    "msg/MCUStatus.msg"
    "msg/LedCmd.msg"
    "srv/DeviceSynchronization.srv"
    DEPENDENCIES std_msgs
)

# 显式安装原始接口文件，确保ros2 interface show可从install空间读取。
install(
    DIRECTORY msg srv
    DESTINATION share/${PROJECT_NAME}
)

ament_export_dependencies(rosidl_default_runtime std_msgs)
ament_package()
