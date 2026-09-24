#!/usr/bin/env python3
"""TacGlove Bootloader交互式SN与启动菜单工具。"""

from __future__ import annotations

import argparse
import re
import sys
import time
from typing import Iterable, Optional, Set

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover - 依赖缺失时给终端用户明确提示
    raise SystemExit("缺少pyserial，请执行: python -m pip install pyserial") from exc


DEFAULT_BAUD = 115200
DEFAULT_PORT = "/dev/ttyUSB0"
SN_PATTERN = re.compile(r"^SN-TacGlove-[0-9]{6}$")
SN_LENGTH = 18

BOOT_CODE_MAP = {
    0xF000: "准备跳转到APP",
    0xF008: "APP分区无效",
    0xF009: "APP分区有效",
    0xF00E: "已进入Bootloader菜单",
    0xF00F: "等待用户输入指令",
    0xF010: "命令错误或功能尚未开放",
    0xF013: "等待SN数据",
    0xF014: "SN接收失败",
    0xF015: "SN已存在",
    0xF016: "SN写入成功",
    0xF017: "SN读取、格式或写入失败",
}

BACKGROUND_CODES = {0xF00E, 0xF00F}
    # 0xF00E: "已进入Bootloader菜单",
    # 0xF00F: "等待用户输入指令",

class BootloaderController:
    """封装参考工程兼容的Bootloader串口命令。"""

    def __init__(self, port: str, baud: int) -> None:
        self.port = port
        self.baud = baud
        self.serial = serial.Serial(
            port=port,                      #  串口路径，如 /dev/ttyUSB0 
            baudrate=baud,  
            timeout=0.1,
            write_timeout=1.0,
            parity=serial.PARITY_NONE,      # 无校验位
            stopbits=serial.STOPBITS_ONE,   # 1位停止位
            bytesize=serial.EIGHTBITS,      # 8位数据位
        )
        time.sleep(0.2)                     # 等待200ms让硬件稳定
        self.serial.reset_input_buffer()    # 清空输入缓冲区

    def close(self) -> None:
        """关闭串口。"""
        if self.serial.is_open:
            self.serial.close()

    def _start_command(self, command: bytes) -> None:
        self.serial.reset_input_buffer()
        self.serial.write(command)  # 发送命令
        self.serial.flush()         # 清空缓冲区

    def _read_exact(self, length: int, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        data = bytearray()
        while len(data) < length and time.monotonic() < deadline:
            chunk = self.serial.read(length - len(data))     # 读取数据
            if chunk:
                data.extend(chunk)                          # 累加数据
        return bytes(data)

    def _read_status(
        self,
        accepted: Iterable[int],
        timeout: float = 2.0,
    ) -> Optional[int]:
        accepted_codes: Set[int] = set(accepted)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            frame = self._read_exact(2, min(0.4, deadline - time.monotonic()))
            if len(frame) != 2:
                continue
            code = (frame[0] << 8) | frame[1]
            if code in accepted_codes:
                return code
            if code not in BACKGROUND_CODES:        # 排除背景状态码    
                return code
        return None

    @staticmethod
    def _describe(code: Optional[int]) -> str:
        if code is None:
            return "无响应"
        return BOOT_CODE_MAP.get(code, f"未知状态码0x{code:04X}")

    def read_sn(self) -> bool:
        """读取设备固定18字节SN。"""
        self._start_command(b"*F_SN_R")     # 发送读取SN命令
        deadline = time.monotonic() + 2.0
        first = b""
        while time.monotonic() < deadline:
            first = self._read_exact(2, min(0.4, deadline - time.monotonic()))
            if len(first) != 2:
                continue
            if first[0] != 0xF0:
                break
            code = (first[0] << 8) | first[1]
            if code in BACKGROUND_CODES:
                continue
            print(f"读取失败：{self._describe(code)}。")
            return False
        if len(first) != 2:
            print("读取失败：设备无响应。")
            return False

        payload = first + self._read_exact(SN_LENGTH - 2, 1.0)
        if len(payload) != SN_LENGTH:
            print(f"读取失败：只收到{len(payload)}/{SN_LENGTH}字节。")
            return False
        try:
            sn = payload.decode("ascii")
        except UnicodeDecodeError:
            print(f"读取失败：返回数据不是ASCII，原始数据={payload.hex()}。")
            return False
        if SN_PATTERN.fullmatch(sn) is None:
            print(f"读取失败：设备返回的SN格式不正确：{sn!r}。")
            return False
        print(f"SN：{sn}")
        return True

    def write_sn(self, sn: str) -> bool:
        """按握手协议一次性写入设备SN。"""
        if SN_PATTERN.fullmatch(sn) is None:
            print("格式错误，必须为SN-TacGlove-后跟6位数字。")
            print("示例：SN-TacGlove-000001")
            return False

        self._start_command(b"#F_SN_W")
        ready = self._read_status({0xF013, 0xF014, 0xF017})
        if ready != 0xF013:
            print(f"设备未进入SN接收状态：{self._describe(ready)}。")
            return False

        self.serial.write(sn.encode("ascii"))
        self.serial.flush()
        result = self._read_status({0xF014, 0xF015, 0xF016, 0xF017}, 4.0)
        print(self._describe(result))
        return result == 0xF016

    def jump_app(self) -> bool:
        """命令Bootloader跳转到APP1。"""
        self._start_command(b"1")     # 发送跳转命令 cmd=1
        result = self._read_status({0xF000, 0xF008})
        print(self._describe(result))
        return result == 0xF000

    def download_app(self) -> bool:
        """保留YMODEM菜单入口；小阶段5.2尚不执行下载。"""
        self._start_command(b"2")
        result = self._read_status({0xF010})
        if result == 0xF010:
            print("YMODEM下载属于后续小阶段，当前固件不会擦除或写入APP分区。")
        else:
            print(self._describe(result))
        return False

    def check_app(self) -> bool:
        """检查APP1向量表并显示结果。"""
        self._start_command(b"3")
        result = self._read_status({0xF008, 0xF009})
        print(self._describe(result))
        return result == 0xF009


def choose_port(configured_port: Optional[str]) -> str:
    """自动选择串口；命令行参数优先，其次优先使用/dev/ttyUSB0。"""
    if configured_port:
        return configured_port

    ports = list(list_ports.comports())                 # 枚举所有串口
    detected_devices = [port.device for port in ports]  # 提取串口路径
    if DEFAULT_PORT in detected_devices:                # DEFAULT_PORT = "/dev/ttyUSB0" in detected_devices 中
        print(f"自动检测到默认串口：{DEFAULT_PORT}")
        return DEFAULT_PORT

    for prefix in ("/dev/ttyUSB", "/dev/ttyACM"):
        matching_devices = sorted(
            device for device in detected_devices if device.startswith(prefix)
        )
        if matching_devices:
            print(f"默认串口不存在，自动使用：{matching_devices[0]}")
            return matching_devices[0]

    print(f"未枚举到USB串口，按项目默认值尝试：{DEFAULT_PORT}")
    return DEFAULT_PORT


def print_menu() -> None:
    print()
    print("=" * 40)
    print("         主菜单")
    print("=" * 40)
    print("1. 读取 SN")
    print("2. 写入 SN")
    print("3. 跳转到 APP")
    print("4. 下载 APP (Ymodem)")
    print("5. 检查 APP 分区")
    print()
    print("0. 退出")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        help="USART2/CH340串口；未指定时自动优先使用/dev/ttyUSB0",
    )
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        port = choose_port(args.port)       # 选择串口--检测到串口 用的库工具 list_ports 
        controller = BootloaderController(port, args.baud)  # 初始化Bootloader控制器
                                                            # 直接用instance 方法 调用的是init 传的参数除了self 都有
    except (ValueError, serial.SerialException) as exc:
        print(f"打开串口失败：{exc}")
        return 1

    print(f"已连接 {port}，{args.baud}-8-N-1。")
    try:
        while True:
            print_menu()
            choice = input("请选择：").strip()              # 等待用户按键
            if choice == "1":
                controller.read_sn()                        # 读取SN  instance 方法调用read_sn 方法
            elif choice == "2":
                sn = input("请输入SN（例如SN-TacGlove-000001）：").strip()
                if controller.write_sn(sn):
                    print("SN写入完成；设备仍停留在Bootloader，可选择3跳转APP。")
            elif choice == "3":
                if controller.jump_app():
                    return 0
            elif choice == "4":
                controller.download_app()
            elif choice == "5":
                controller.check_app()
            elif choice == "0":
                return 0
            else:
                print("无效选择，请输入0~5。")
    except KeyboardInterrupt:
        print("\n已取消。")
        return 0
    finally:
        controller.close()


if __name__ == "__main__":
    sys.exit(main())
