#!/usr/bin/env python3
"""发现TacGlove设备，监听MCU上报并提供时间同步服务。"""

from __future__ import annotations

import argparse
import os
import sys
import time
from typing import Iterable

os.environ.setdefault("ROS_DOMAIN_ID", "9")

import rclpy
from common_msgs.msg import KeyState, MCUStatus
from common_msgs.srv import DeviceSynchronization
from rclpy.node import Node


DEVICE_PREFIX = "mcu_SN_"
DISCOVERY_TOPIC_SUFFIXES = {"key_state", "mcu_status", "led_cmd"}

KEY_EVENT_NAMES = {
    1: "长按",
    2: "单击/数采切换",
    3: "双击",
    4: "故障确认",
}

MCU_STATE_NAMES = {
    0: "空闲",
    1: "就绪/数采中",
    2: "故障",
    3: "校准中",
    4: "固件更新中",
}


def normalize_device_name(value: str) -> str:
    """把SN或节点名统一为mcu_SN_xxx格式。"""
    normalized = value.strip().strip("/").replace("-", "_")
    if normalized.startswith("mcu_"):
        return normalized
    if normalized.startswith("SN_"):
        return f"mcu_{normalized}"
    return f"mcu_SN_{normalized}"


class DeviceMonitor(Node):
    """自动发现设备并监听KeyState、MCUStatus消息。"""

    def __init__(self) -> None:
        super().__init__("tacglove_pc_subscriber")
        self.start_time = time.monotonic()
        self.selected_device = ""
        self.key_count = 0
        self.status_count = 0
        self.sync_count = 0

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
                # 留出少量时间，让同一ROS Domain中的其他设备完成发现。
                rclpy.spin_once(self, timeout_sec=0.3)

        return sorted(devices)

    def configure(self, device_name: str) -> None:
        """根据选定设备创建订阅器和同步服务。"""
        self.selected_device = device_name
        root = f"/{device_name}"

        self.key_subscription = self.create_subscription(
            KeyState,
            f"{root}/key_state",
            self._on_key_state,
            10,
        )
        self.status_subscription = self.create_subscription(
            MCUStatus,
            f"{root}/mcu_status",
            self._on_mcu_status,
            10,
        )
        self.sync_service = self.create_service(
            DeviceSynchronization,
            f"{root}/sync",
            self._on_sync,
        )

    def _elapsed(self) -> float:
        return time.monotonic() - self.start_time

    def _on_key_state(self, message: KeyState) -> None:
        self.key_count += 1
        event_name = KEY_EVENT_NAMES.get(
            message.event_type,
            f"未知({message.event_type})",
        )
        stamp = message.header.stamp
        print(
            f"[{self._elapsed():8.3f}] [KEY #{self.key_count}] "
            f"{event_name} | ROS时间={stamp.sec}.{stamp.nanosec:09d}",
            flush=True,
        )

    def _on_mcu_status(self, message: MCUStatus) -> None:
        self.status_count += 1
        state_name = MCU_STATE_NAMES.get(
            message.system_state,
            f"未知({message.system_state})",
        )
        connected = "已连接" if message.agent_connected else "未连接"
        print(
            f"[{self._elapsed():8.3f}] [MCU #{self.status_count}] "
            f"状态={state_name} | Agent={connected} | "
            f"运行={message.uptime_seconds}s | "
            f"TX={message.message_tx_count} RX={message.message_rx_count} | "
            f"固件={message.firmware_version}",
            flush=True,
        )

    def _on_sync(
        self,
        request: DeviceSynchronization.Request,
        response: DeviceSynchronization.Response,
    ) -> DeviceSynchronization.Response:
        self.sync_count += 1
        response.header.stamp = self.get_clock().now().to_msg()
        response.header.frame_id = "pc_sync_server"
        response.sync_state = bool(request.sync_request)
        print(
            f"[{self._elapsed():8.3f}] [SYNC #{self.sync_count}] "
            "已回复MCU时间同步请求",
            flush=True,
        )
        return response


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


def parse_arguments() -> tuple[argparse.Namespace, list[str]]:
    """解析工具参数，并保留ROS 2自身参数。"""
    parser = argparse.ArgumentParser(
        description="监听TacGlove按键、MCU状态并响应时间同步",
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
    """打印监听工具启动信息。"""
    print("\n" + "=" * 64)
    print(" TacGlove micro-ROS 数据监听与时间同步工具")
    print("=" * 64)
    print(f"ROS_DOMAIN_ID: {os.environ['ROS_DOMAIN_ID']}")
    print("监听内容: key_state、mcu_status")
    print("提供服务: sync")


def main() -> int:
    """程序入口。"""
    arguments, ros_arguments = parse_arguments()
    rclpy.init(args=ros_arguments)
    node = DeviceMonitor()

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
        root = f"/{device_name}"
        print("\n已建立以下接口：")
        print(f"  SUB  {root}/key_state")
        print(f"  SUB  {root}/mcu_status")
        print(f"  SRV  {root}/sync")
        print("\n开始监听，按 Ctrl+C 退出。\n")
        rclpy.spin(node)
    except KeyboardInterrupt:
        print("\n已停止监听。")
    finally:
        print(
            f"统计: Key={node.key_count}, Status={node.status_count}, "
            f"Sync={node.sync_count}"
        )
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()

    return 0


if __name__ == "__main__":
    sys.exit(main())
