#!/usr/bin/env python3
"""发现TacGlove设备，通过菜单下发LED和蜂鸣器命令。"""

from __future__ import annotations

import argparse
import os
import sys
import time
from typing import Iterable

os.environ.setdefault("ROS_DOMAIN_ID", "9")

import rclpy
from common_msgs.msg import LedCmd
from rclpy.node import Node


DEVICE_PREFIX = "mcu_SN_"
DISCOVERY_TOPIC_SUFFIXES = {"key_state", "mcu_status", "led_cmd"}
LED_COUNT = 6

LED_MODE_NAMES = {
    0: "关闭",
    1: "绿色常亮",
    2: "绿色闪烁",
    3: "红色常亮",
    4: "蓝色闪烁",
    5: "蓝色常亮",
}

BEEP_MODE_NAMES = {
    0: "关闭",
    1: "短鸣",
    2: "长鸣",
    3: "连续鸣叫",
}


def normalize_device_name(value: str) -> str:
    """把SN或节点名统一为mcu_SN_xxx格式。"""
    normalized = value.strip().strip("/").replace("-", "_")
    if normalized.startswith("mcu_"):
        return normalized
    if normalized.startswith("SN_"):
        return f"mcu_{normalized}"
    return f"mcu_SN_{normalized}"


class DeviceController(Node):
    """自动发现设备并向指定设备发布LedCmd。"""

    def __init__(self) -> None:
        super().__init__("tacglove_pc_publisher")
        self.selected_device = ""
        self.led_modes = [0] * LED_COUNT
        self.beep_mode = 0
        self.publish_count = 0

    def discover_devices(self, timeout_seconds: float) -> list[str]:
        """在指定时间内收集发布了TacGlove接口的设备名。"""
        deadline = time.monotonic() + max(timeout_seconds, 0.1)
        devices: set[str] = set()

        while rclpy.ok() and time.monotonic() < deadline:
            rclpy.spin_once(self, timeout_sec=0.1)
            for topic_name, _ in self.get_topic_names_and_types():
                parts = topic_name.strip("/").split("/")
                if (len(parts) >= 2 and
                        parts[0].startswith(DEVICE_PREFIX) and
                        parts[1] in DISCOVERY_TOPIC_SUFFIXES):
                    devices.add(parts[0])
            if devices:
                rclpy.spin_once(self, timeout_sec=0.3)

        return sorted(devices)

    def configure(self, device_name: str) -> None:
        """为选定设备创建LED命令Publisher。"""
        self.selected_device = device_name
        self.publisher = self.create_publisher(
            LedCmd,
            f"/{device_name}/led_cmd",
            10,
        )

    def publish_command(self) -> None:
        """发布当前缓存的全部LED和蜂鸣器状态。"""
        message = LedCmd()
        message.header.stamp = self.get_clock().now().to_msg()
        message.header.frame_id = "pc_led_cmd"
        message.led_mode = list(self.led_modes)
        message.beep_mode = self.beep_mode
        self.publisher.publish(message)
        self.publish_count += 1

        rclpy.spin_once(self, timeout_sec=0.05)
        print(
            f"\n已发送 #{self.publish_count}: "
            f"LED={self.led_modes}, BEEP={self.beep_mode}"
        )


def choose_device(devices: Iterable[str]) -> str:
    """打印设备菜单并返回用户选择的设备名。"""
    device_list = list(devices)
    print("\n在线设备：")
    for index, device in enumerate(device_list, start=1):
        print(f"  {index}. {device}")

    if len(device_list) == 1:
        print(f"\n仅发现一个设备，自动选择 {device_list[0]}")
        return device_list[0]

    while True:
        selection = input("\n请输入设备序号：").strip()
        if selection.isdigit():
            index = int(selection) - 1
            if 0 <= index < len(device_list):
                return device_list[index]
        print("输入无效，请重新输入。")


def read_number(prompt: str, minimum: int, maximum: int) -> int:
    """读取指定闭区间内的整数。"""
    while True:
        value = input(prompt).strip()
        if value.isdigit() and minimum <= int(value) <= maximum:
            return int(value)
        print(f"请输入 {minimum}~{maximum} 之间的整数。")


def print_mode_table() -> None:
    """打印协议支持的LED和蜂鸣器模式。"""
    print("\nLED模式:")
    for value, name in LED_MODE_NAMES.items():
        print(f"  {value}: {name}")
    print("蜂鸣器模式:")
    for value, name in BEEP_MODE_NAMES.items():
        print(f"  {value}: {name}")


def print_menu(controller: DeviceController) -> None:
    """打印主操作菜单和当前命令缓存。"""
    print("\n" + "=" * 64)
    print(f" 当前设备: {controller.selected_device}")
    print(f" 目标Topic: /{controller.selected_device}/led_cmd")
    print(f" 当前LED: {controller.led_modes}")
    print(f" 当前蜂鸣器: {controller.beep_mode}")
    print("-" * 64)
    print("1. 设置单颗LED（序号1~6对应物理LED2~LED7）")
    print("2. 设置全部LED")
    print("3. 设置蜂鸣器")
    print("4. 显示模式说明")
    print("5. 重发当前命令")
    print("0. 退出")
    print("=" * 64)


def run_menu(controller: DeviceController) -> None:
    """循环处理用户命令，并在状态变更后发送完整LedCmd。"""
    while rclpy.ok():
        print_menu(controller)
        selection = input("请选择：").strip()

        if selection == "0":
            return
        if selection == "1":
            led_number = read_number("LED序号(1~6)：", 1, LED_COUNT)
            print_mode_table()
            mode = read_number("LED模式(0~5)：", 0, 5)
            controller.led_modes[led_number - 1] = mode
            controller.publish_command()
        elif selection == "2":
            print_mode_table()
            mode = read_number("全部LED模式(0~5)：", 0, 5)
            controller.led_modes = [mode] * LED_COUNT
            controller.publish_command()
        elif selection == "3":
            print_mode_table()
            controller.beep_mode = read_number("蜂鸣器模式(0~3)：", 0, 3)
            controller.publish_command()
        elif selection == "4":
            print_mode_table()
        elif selection == "5":
            controller.publish_command()
        else:
            print("输入无效，请重新选择。")


def parse_arguments() -> tuple[argparse.Namespace, list[str]]:
    """解析工具参数，并保留ROS 2自身参数。"""
    parser = argparse.ArgumentParser(
        description="向指定TacGlove设备下发LED和蜂鸣器命令",
    )
    parser.add_argument(
        "--device",
        help="直接指定SN或设备名，例如SN-TacGlove-000001",
    )
    parser.add_argument(
        "--discovery-seconds",
        type=float,
        default=2.0,
        help="ROS 2设备发现等待时间，默认2秒",
    )
    return parser.parse_known_args()


def print_banner() -> None:
    """打印控制工具启动信息。"""
    print("\n" + "=" * 64)
    print(" TacGlove micro-ROS LED/蜂鸣器控制工具")
    print("=" * 64)
    print(f"ROS_DOMAIN_ID: {os.environ['ROS_DOMAIN_ID']}")


def main() -> int:
    """程序入口。"""
    arguments, ros_arguments = parse_arguments()
    rclpy.init(args=ros_arguments)
    node = DeviceController()

    try:
        print_banner()
        if arguments.device:
            device_name = normalize_device_name(arguments.device)
            print(f"指定设备: {device_name}")
        else:
            print(f"正在发现设备，等待 {arguments.discovery_seconds:.1f} 秒...")
            devices = node.discover_devices(arguments.discovery_seconds)
            if not devices:
                print("未发现mcu_SN_*设备。请检查Agent、DOMAIN_ID和串口连接。")
                return 1
            device_name = choose_device(devices)

        node.configure(device_name)
        print(f"控制Topic: /{device_name}/led_cmd")
        print_mode_table()
        run_menu(node)
    except (EOFError, KeyboardInterrupt):
        print("\n已退出控制工具。")
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()

    return 0


if __name__ == "__main__":
    sys.exit(main())
