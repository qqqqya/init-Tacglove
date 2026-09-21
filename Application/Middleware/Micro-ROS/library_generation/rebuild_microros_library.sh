#!/usr/bin/env bash
# 从本工程唯一的common_msgs接口源重新生成并安装MCU端micro-ROS静态库和头文件。

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MICROROS_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
APPLICATION_DIR="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"
INTERFACE_PACKAGE="${APPLICATION_DIR}/Interfaces/common_msgs"
MICROROS_WORKSPACE="${MICROROS_WS:-${HOME}/microros_jazzy_ws}"
FIRMWARE_DIR="${MICROROS_WORKSPACE}/firmware"
CUSTOM_PACKAGES_DIR="${FIRMWARE_DIR}/mcu_ws/custom_packages"
CUSTOM_PACKAGE="${CUSTOM_PACKAGES_DIR}/common_msgs"
OUTPUT_DIR="${FIRMWARE_DIR}/build"
INSTALL_OUTPUT=0

if [[ "${1:-}" == "--install" ]]; then
    INSTALL_OUTPUT=1
elif [[ $# -ne 0 ]]; then
    echo "用法: $0 [--install]" >&2
    exit 2
fi

if [[ ! -f /opt/ros/jazzy/setup.bash ]]; then
    echo "错误: 未找到/opt/ros/jazzy/setup.bash。" >&2
    exit 1
fi
if [[ ! -f "${MICROROS_WORKSPACE}/install/local_setup.bash" ]]; then
    echo "错误: 未找到${MICROROS_WORKSPACE}/install/local_setup.bash。" >&2
    echo "请先在普通用户工作区构建micro_ros_setup。" >&2
    exit 1
fi
if [[ ! -f "${INTERFACE_PACKAGE}/package.xml" ]]; then
    echo "错误: 未找到接口源包${INTERFACE_PACKAGE}。" >&2
    exit 1
fi
if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    echo "错误: PATH中没有arm-none-eabi-gcc。" >&2
    exit 1
fi

# ROS 2生成的setup脚本会探测若干尚未定义的环境变量，加载期间临时关闭nounset。
set +u
# shellcheck disable=SC1091
source /opt/ros/jazzy/setup.bash
# shellcheck disable=SC1091
source "${MICROROS_WORKSPACE}/install/local_setup.bash"
set -u

cd "${MICROROS_WORKSPACE}"
if [[ ! -f "${FIRMWARE_DIR}/PLATFORM" ]]; then
    ros2 run micro_ros_setup create_firmware_ws.sh generate_lib
fi

mapfile -t platform_lines < "${FIRMWARE_DIR}/PLATFORM"
if [[ "${platform_lines[0]:-}" != "generate_lib" ]]; then
    echo "错误: ${FIRMWARE_DIR}不是generate_lib工作区，请先人工确认，脚本不会覆盖。" >&2
    exit 1
fi

mkdir -p "${CUSTOM_PACKAGES_DIR}"
if [[ -e "${CUSTOM_PACKAGE}" ]]; then
    case "${CUSTOM_PACKAGE}" in
        "${MICROROS_WORKSPACE}"/firmware/mcu_ws/custom_packages/common_msgs)
            rm -rf -- "${CUSTOM_PACKAGE}"
            ;;
        *)
            echo "错误: 拒绝删除非预期路径${CUSTOM_PACKAGE}。" >&2
            exit 1
            ;;
    esac
fi
cp -a "${INTERFACE_PACKAGE}" "${CUSTOM_PACKAGE}"

if ! colcon list --base-paths "${FIRMWARE_DIR}/mcu_ws" | \
    grep '^common_msgs[[:space:]]' >/dev/null; then
    echo "错误: colcon没有发现common_msgs。" >&2
    exit 1
fi

ros2 run micro_ros_setup build_firmware.sh \
    "${SCRIPT_DIR}/stm32g474_toolchain.cmake" \
    "${SCRIPT_DIR}/colcon.meta"

test -f "${OUTPUT_DIR}/libmicroros.a"
test -f "${OUTPUT_DIR}/include/common_msgs/msg/detail/key_state__struct.h"
test -f "${OUTPUT_DIR}/include/common_msgs/msg/detail/led_cmd__struct.h"
grep -q 'beep_mode' \
    "${OUTPUT_DIR}/include/common_msgs/msg/detail/led_cmd__struct.h"

echo "生成完成: ${OUTPUT_DIR}/libmicroros.a"
echo "生成完成: ${OUTPUT_DIR}/include"

if [[ ${INSTALL_OUTPUT} -eq 0 ]]; then
    echo "本次只生成未安装；检查无误后重新执行: $0 --install"
    exit 0
fi

STAGE_DIR="$(mktemp -d "${MICROROS_DIR}/.generated.XXXXXX")"
BACKUP_DIR="${MICROROS_DIR}/.backup_before_regeneration"
trap 'rm -rf -- "${STAGE_DIR}"' EXIT

cp "${OUTPUT_DIR}/libmicroros.a" "${STAGE_DIR}/libmicroros.a"
cp -a "${OUTPUT_DIR}/include" "${STAGE_DIR}/include"

if [[ -e "${BACKUP_DIR}" ]]; then
    echo "错误: 备份目录已存在，请先人工处理${BACKUP_DIR}。" >&2
    exit 1
fi

mkdir "${BACKUP_DIR}"
mv "${MICROROS_DIR}/libmicroros.a" "${BACKUP_DIR}/libmicroros.a"
mv "${MICROROS_DIR}/include" "${BACKUP_DIR}/include"
mv "${STAGE_DIR}/libmicroros.a" "${MICROROS_DIR}/libmicroros.a"
mv "${STAGE_DIR}/include" "${MICROROS_DIR}/include"

echo "已安装新的libmicroros.a和include。"
echo "旧产物暂存于${BACKUP_DIR}；Application编译和板测通过后可删除。"
