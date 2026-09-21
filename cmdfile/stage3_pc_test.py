#!/usr/bin/env python3
"""Stage 3 PC-side monitor and time synchronization service."""

import rclpy
from rclpy.node import Node

from common_msgs.msg import KeyState, MCUStatus
from common_msgs.srv import DeviceSynchronization


class Stage3PcTest(Node):
    """Print MCU publications and answer its time synchronization request."""

    def __init__(self) -> None:
        super().__init__("stage3_pc_test")
        self.create_subscription(
            KeyState,
            "/mcu_dev/key_state",
            self._on_key_state,
            10,
        )
        self.create_subscription(
            MCUStatus,
            "/mcu_dev/mcu_status",
            self._on_mcu_status,
            10,
        )
        self.create_service(
            DeviceSynchronization,
            "/mcu_dev/sync",
            self._on_sync,
        )

    def _on_key_state(self, message: KeyState) -> None:
        self.get_logger().info(
            f"key event={message.event_type} "
            f"stamp={message.header.stamp.sec}."
            f"{message.header.stamp.nanosec:09d}"
        )

    def _on_mcu_status(self, message: MCUStatus) -> None:
        self.get_logger().info(
            f"mcu state={message.system_state} "
            f"agent={message.agent_connected} "
            f"uptime={message.uptime_seconds}s "
            f"tx={message.message_tx_count} "
            f"rx={message.message_rx_count}"
        )

    def _on_sync(
        self,
        request: DeviceSynchronization.Request,
        response: DeviceSynchronization.Response,
    ) -> DeviceSynchronization.Response:
        response.header.stamp = self.get_clock().now().to_msg()
        response.header.frame_id = "pc_ros_time"
        response.sync_state = bool(request.sync_request)
        self.get_logger().info("answered MCU time synchronization request")
        return response


def main() -> None:
    rclpy.init()
    node = Stage3PcTest()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
