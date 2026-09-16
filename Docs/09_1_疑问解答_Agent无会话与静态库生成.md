# 09_1 疑问解答：Agent无会话、阶段3迁移与静态库生成

> 日期：2026-09-16。代码基准：当前 `Tacapp_init` 工作树。本文以本次截图和当前源码为准；截图里的终端输出是诊断依据，不是来自截图的操作指令。

## 1. 截图中的终端A到底是不是卡住了

不是。截图中的关键日志是：

```text
TermiosAgentLinux.cpp | init              | running...  | fd: 30
Root.cpp              | set_verbose_level | logger setup | verbose_level: 6
```

这两行说明：

1. `micro_ros_agent` 包已经能被普通用户找到并启动；
2. Agent成功打开了 `/dev/ttyUSB0`，Linux文件描述符为30；
3. 日志等级6已生效；
4. Agent随后阻塞等待串口数据，这是常驻服务的正常工作方式；
5. 截图最后的 `^C [ros2run]: Interrupt` 是手动按 `Ctrl+C` 结束进程，不是Agent自行崩溃。

真正缺少的是下面这类日志：

```text
create_client
session established
create_participant
create_topic / publisher / subscriber / requester
```

因此目前可以把故障范围收窄为：**Agent已打开Linux串口，但没有从MCU收到能够建立XRCE会话的有效字节流**。故障发生在ROS Topic、`LedCmd`字段和时间同步之前。

### 1.1 终端B“没有反应”为什么可能是正常现象

如果终端B运行的是：

```bash
python3 /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Interfaces/stage3_pc_test.py
```

脚本进入 `rclpy.spin()` 后只在收到 `key_state`、`mcu_status`，或者收到MCU同步请求时打印。MCU尚未建立会话时，它保持安静是正常的。`ros2 topic echo` 同理，会一直等待第一条消息。

可用立即返回的命令判断ROS图，而不是把“echo不打印”理解成终端卡死：

```bash
ros2 node list
ros2 topic list -t
ros2 service list -t
```

若只启动了PC测试脚本而MCU未连接，应至少能看到PC测试节点及其 `/mcu_dev/sync` 服务；不会看到由MCU创建的 `/mcu_dev` 节点、两个publisher和LED subscription。

### 1.2 已经排除什么

- 截图中 `ros2 pkg prefix micro_ros_agent` 和 `ros2 pkg prefix common_msgs` 都成功，因此不是“当前普通用户找不到包”。
- Agent能够打开 `/dev/ttyUSB0`，因此不是最基本的串口权限拒绝；但仍可能选错CH340设备。
- `ROS_DOMAIN_ID=9` 已设置。Domain ID不一致会影响ROS图发现，但不能解释连 `create_client/session` 日志都没有。
- `.msg`里的空格和 `#`注释不会上串口，不会阻止最初的XRCE握手。新增/删除**字段**才会改变线格式。

### 1.3 按这个顺序定位，不要先反复重装Agent

#### 第一步：确认烧录的是包含micro-ROS的最新固件

重新构建并烧录当前固件。上电后先确认原有绿色自检、按键和蜂鸣器仍工作。当前 `main()` 若创建micro-ROS任务失败会进入 `Error_Handler()`，这种情况下连LED任务也不会开始；如果上电自检正常，至少说明调度器和LED任务在运行，但不能单独证明micro-ROS任务没有挂起。

#### 第二步：确认Agent用的是板子对应的CH340

先停止Agent，再执行：

```bash
ls -l /dev/serial/by-id/* 2>/dev/null
ls -l /dev/ttyUSB* 2>/dev/null
udevadm info --query=property --name=/dev/ttyUSB0 | grep -E 'ID_VENDOR|ID_MODEL|ID_SERIAL'
```

优先用 `/dev/serial/by-id/...` 启动Agent，避免重插后 `ttyUSB0` 变为 `ttyUSB1`。启动前用下面命令确认没有其他串口软件占用；Agent启动后看到Agent自身PID占用则正常：

```bash
fuser -v /dev/ttyUSB0
```

#### 第三步：检查接线方向和公共地

```text
MCU PA2 / USART2_TX  → CH340 RX
MCU PA3 / USART2_RX  ← CH340 TX
MCU GND              ↔ CH340 GND
波特率                = 115200, 8N1, 无流控
```

TX/RX不能同名直连。若模块上只有丝印 `TXD/RXD`，以模块的数据方向为准。

#### 第四步：用逻辑分析仪看PA2、PA3

Agent运行时，MCU在 `WAIT_AGENT` 中每约500 ms调用一次 `rmw_uros_ping_agent(100 ms, 1)`。逻辑分析仪按115200、8N1解码：

| 观测结果 | 判断 |
|---|---|
| PA2完全没有波形 | MCU没有运行到ping发送；检查固件版本、任务创建、任务是否在初始化处挂起/HardFault |
| PA2有周期性数据，CH340 RX脚无数据 | MCU到CH340连线或测点错误 |
| PA2和CH340 RX都有数据，但Agent无帧日志 | 可能选错Linux设备、CH340/USB转发链路或波特率不一致 |
| Agent方向有响应，但PA3没有波形 | CH340 TX到MCU PA3路径错误 |
| PA2/PA3双向都有波形，但无session | 捕获字节并检查framing、库ABI或transport实现 |

#### 第五步：调试器断点定位到具体函数

依次在以下函数设置断点：

```text
micro_ros_task_entry()
cubemx_transport_open()
rmw_uros_ping_agent()
cubemx_transport_write()
cubemx_transport_read()
```

预期是进入 `micro_ros_task_entry()`，注册custom transport，ping过程中调用 `open`，随后 `write` 返回非0长度。若任务在 `micro_ros_init_allocator()` 或 `rmw_uros_set_custom_transport()`失败后执行 `vTaskSuspend(NULL)`，Agent端只会一直显示两行等待日志。若 `HAL_UART_Transmit_DMA()`失败或 `write`返回0，就继续检查 `huart2.gState`、DMA1 Channel3、中断和HAL返回值。

#### 第六步：核对当前参考静态库的目标ABI

当前固件自身的编译参数是：

```text
-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard
```

但对当前复用的 `libmicroros.a` 执行 `arm-none-eabi-readelf -A`，可看到：

```text
Tag_FP_arch: FPv5/FP-D16 for ARMv8
Tag_ABI_VFP_args: VFP registers
```

STM32G474是Cortex-M4F，工程使用FPv4。这个差异不能只凭截图判定为唯一故障，但属于真实的ABI/指令集风险，应通过第4章的正确工具链重建库来排除。生成后应看到与工程一致的 Cortex-M4/FPv4-SP-D16/hard-float 属性。

### 1.4 这时不要优先排查什么

在Agent出现 `create_client/session established` 之前，先不要把精力放在：

- `LedCmd`字段是否正确；
- PC是否能显示自定义msg；
- `DeviceSynchronization.srv`时间戳；
- LED模式数组是否合法；
- ROS Domain中的Topic QoS。

这些都位于会话建立之后。当前第一目标只是确认PA2有ping帧、PA3有Agent响应，并在Agent日志中看到client/session。

## 2. 从阶段2到阶段3到底要移植、生成和编写什么

阶段2结束时已有 FreeRTOS、按键状态机、LED PWM+DMA和蜂鸣器本地功能。阶段3不是重写这些驱动，而是在它们外面增加ROS接口、传输和任务桥接。

### 2.1 直接复用或由工具生成的部分

| 内容 | 当前目录 | 来源/处理 |
|---|---|---|
| micro-ROS公共头文件 | `Middleware/Micro-ROS/include` | 应由静态库生成流程输出；当前大部分来自参考工程 |
| micro-ROS静态库 | `Middleware/Micro-ROS/libmicroros.a` | 应按STM32G474重新交叉编译；当前来自参考工程 |
| C消息结构和类型支持 | `include/common_msgs/...`、静态库目标文件 | `rosidl`根据同一份 `.msg/.srv`生成 |
| PC Python/C接口 | `~/tacapp_ros2_ws/install/common_msgs` | `colcon build`根据 `Interfaces/common_msgs`生成 |
| allocator/time适配框架 | `extra_sources/custom_memory_manager.c`、`microros_allocators.*`、`microros_time.c` | 从micro-ROS STM32适配思路复用并接入本项目 |

“生成”不等于不需要维护：我们维护 `.msg/.srv`、工具链和配置；生成器维护派生的 `.h/.c/.py/.a`，不应长期手改生成产物。

### 2.2 必须为本板编写或适配的部分

| 文件 | 从阶段2到3增加的职责 |
|---|---|
| `Interfaces/common_msgs/msg/*.msg`、`srv/*.srv` | 定义PC/MCU共同通信合同 |
| `Interfaces/common_msgs/CMakeLists.txt`、`package.xml` | 让PC的ROS 2工作空间生成并发现接口 |
| `Tasks/micro_ros_task.c/.h` | Agent等待/重连状态机、2 PUB、1 SUB、1 service client、Executor和统计 |
| `Tasks/task_manager.h` | Tasks层统一状态码、栈/优先级和公共任务句柄 |
| `dma_transport.c/.h` | 把micro-ROS自定义流传输接到USART2 RX循环DMA和TX DMA |
| `Tasks/key_task.c` | 保留本地动作，同时把已识别按键事件送入ROS Queue |
| `Tasks/led_task.c/.h` | 增加单元素最新值cmd Queue，执行PC下发的六灯和蜂鸣器模式，断线回本地控制 |
| `Tasks/task_manager.c/.h` | 初始化两个Queue并直接创建micro-ROS任务 |
| 根 `CMakeLists.txt` | 编译适配源文件、加入头文件、链接 `libmicroros.a` |
| `Interfaces/stage3_pc_test.py` | PC端监听两个PUB并实现时间同步server |

### 2.3 阶段2原有代码中哪些不应该重新移植

- `bsp_led_driver` 的TIM3 PWM+DMA波形仍是唯一灯链硬件提交层；ROS回调不能直接写PWM。
- `bsp_led_handler` 仍负责LED2~LED7业务编号到SK6805物理序号映射。
- `bsp_key_handler` 仍负责消抖、单击/双击/长按识别；ROS只接收最终事件。
- `bsp_beep_driver` 仍只提供低电平有效的开/关；持续时间由LED任务决定。
- `.ioc` 已有USART2和DMA配置时阶段3不应无理由重新生成外设代码；如确需改配置，要同时改 `.ioc`并记录。

### 2.4 推荐迁移顺序

1. 先冻结3个msg、1个srv以及名称、Domain ID、QoS和数组语义。
2. 在PC工作空间构建接口包，确认 `ros2 interface show` 和Python import。
3. 按第4章为STM32G474生成匹配的 `.a + include`，不要先复用未知ABI库。
4. 接入allocator、时间函数和USART2 DMA custom transport，先只完成ping握手。
5. 编写 `micro_ros_task` 状态机，先验证node/entity创建和断线重连。
6. 接入 `key_task → Queue → KeyState PUB`。
7. 接入 `LedCmd SUB → Queue → led_task → BSP`，最后再接蜂鸣器字段。
8. 接入 `MCUStatus PUB` 和同步Client。
9. 做非法cmd、Agent停止、USB拔插和8小时长稳测试。

## 3. 本次已增加的蜂鸣器cmd

按键消息更名时将你在 `ButtonEvent.msg` 中的注释原样迁移到 `KeyState.msg`；`MCUStatus.msg`和 `LedCmd.msg`中的用户注释继续保留。蜂鸣器字段为：

```text
# 蜂鸣器模式
uint8 beep_mode

# 蜂鸣器命令常量
uint8 MODE_BEEP_OFF   = 0
uint8 MODE_SHORT_BEEP = 1
uint8 MODE_LONG_BEEP  = 2
uint8 MODE_BEEPING    = 3
```

对应行为与参考工程一致：

| `beep_mode` | MCU行为 |
|---:|---|
| 0 | 立即静音 |
| 1 | 鸣叫200 ms后关闭 |
| 2 | 鸣叫600 ms后关闭 |
| 3 | 鸣叫150 ms、静音250 ms、再鸣叫150 ms后关闭 |

链路如下：

```text
PC发布 LedCmd{led_mode[6], beep_mode}
  → Agent → USART2 DMA → rclc_executor_spin_some()
  → micro_ros_led_cmd_callback()
  → led_task_submit_cmd(led_mode, beep_mode)
  → 单元素最新值Queue
  → led_task_entry()
     ├─ 六灯：BSP Handler → TIM3 PWM DMA
     └─ 蜂鸣器：bsp_beep_driver_set() + FreeRTOS延时
```

已同步修改：

- `Interfaces/common_msgs/msg/LedCmd.msg`；
- 临时 `led_cmd__struct.h`；
- 临时 `led_cmd_type_support.c` 的序列化、反序列化和长度计算；
- `micro_ros_task.c` 回调参数；
- `led_task.c/.h` 的Queue结构、合法性检查和蜂鸣动作；
- `Docs/08_阶段3_micro-ROS消息接口设计.md` 和原09验证命令。

Debug和Release均已重新编译链接通过。新增字段后，旧PC接口与新MCU接口不兼容，联调前必须重建PC `common_msgs`，并重新烧录当前固件：

```bash
source /opt/ros/jazzy/setup.bash
cd "$HOME/tacapp_ros2_ws"
cp -a /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Interfaces/common_msgs/. \
  "$HOME/tacapp_ros2_ws/src/common_msgs/"
colcon build --packages-select common_msgs --cmake-clean-cache \
  --event-handlers console_direct+
source install/local_setup.bash
ros2 interface show common_msgs/msg/LedCmd
```

预期最后能看到 `uint8[6] led_mode` 和 `uint8 beep_mode`。蜂鸣器测试示例：

```bash
# 短鸣
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [0, 0, 0, 0, 0, 0], beep_mode: 1}"

# 长鸣
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [0, 0, 0, 0, 0, 0], beep_mode: 2}"

# 两次鸣叫
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [0, 0, 0, 0, 0, 0], beep_mode: 3}"

# 静音
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [0, 0, 0, 0, 0, 0], beep_mode: 0}"
```

蜂鸣器动作当前在LED任务内顺序执行，所以长鸣会让LED任务最多约600 ms不处理下一条灯命令；按键扫描和micro-ROS任务是独立任务，不会被该延时阻塞。阶段3验证足够；若后续要求高频cmd或严格闪烁相位，应再改成非阻塞蜂鸣状态机。

## 4. 在当前工程真正重新生成 `libmicroros.a`

### 4.1 先分清三个工作空间

```text
~/tacapp_ros2_ws       PC common_msgs生成和上位机运行
~/microros_jazzy_ws    micro_ros_setup、Agent，以及即将创建的firmware生成目录
Tacapp_init            STM32业务工程，最终消费生成后的.a和include
```

`build_agent.sh`只构建PC Agent，`colcon build common_msgs`只构建PC接口。真正生成MCU静态库必须运行 `create_firmware_ws.sh generate_lib` 和 `build_firmware.sh`。官方 `generate_lib` 流程输出 `firmware/build/libmicroros.a` 与 `firmware/build/include`。[官方静态库教程](https://github.com/micro-ROS/micro-ROS.github.io/blob/master/_docs/tutorials/advanced/create_custom_static_library/index.md)

本工程已经增加两份可版本管理的生成配置：

```text
Middleware/Micro-ROS/library_generation/
├── stm32g474_toolchain.cmake
└── colcon.meta
```

toolchain明确使用 Cortex-M4、FPv4-SP-D16、hard-float；`colcon.meta`启用custom stream transport并按当前实体数配置1 node、2 publishers、1 subscription、1 client。

### 4.2 准备工具链，仅缺包时才需要sudo

以下日常生成命令都用普通用户 `embedded`。先检查：

```bash
whoami
command -v arm-none-eabi-gcc
arm-none-eabi-gcc --version
```

只有第二条找不到编译器时，才执行一次系统安装：

```bash
sudo apt update
sudo apt install gcc-arm-none-eabi
```

不要使用 `wsl -u root` 创建生成工作区，否则生成物会归root所有。

### 4.3 创建独立firmware生成目录

```bash
source /opt/ros/jazzy/setup.bash
cd "$HOME/microros_jazzy_ws"
source install/local_setup.bash

ros2 pkg prefix micro_ros_setup
```

若当前目录已经存在 `firmware/`，先停止并检查它是否属于先前的生成任务；不要直接覆盖，可改名保存为 `firmware.backup_日期`。确认没有需要保留的 `firmware/` 后执行：

```bash
ros2 run micro_ros_setup create_firmware_ws.sh generate_lib
```

该命令会下载/整理MCU端micro-ROS源码到：

```text
~/microros_jazzy_ws/firmware/mcu_ws
```

它与已经安装的Agent是两种产物，互不替代。

### 4.4 把本工程唯一的接口源包加入MCU生成输入

```bash
cd "$HOME/microros_jazzy_ws"
mkdir -p firmware/mcu_ws/custom_packages

cp -a /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Interfaces/common_msgs \
  firmware/mcu_ws/custom_packages/

colcon list --base-paths firmware/mcu_ws | grep '^common_msgs'
```

最后一条必须显示 `common_msgs`。如果此前已复制过，不要再次复制成 `common_msgs/common_msgs`；先检查目标目录层级。源包中必须是本次已经带 `beep_mode` 的版本。

### 4.5 使用本工程配置交叉编译

```bash
cd "$HOME/microros_jazzy_ws"
source /opt/ros/jazzy/setup.bash
source install/local_setup.bash

ros2 run micro_ros_setup build_firmware.sh \
  /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Middleware/Micro-ROS/library_generation/stm32g474_toolchain.cmake \
  /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Middleware/Micro-ROS/library_generation/colcon.meta
```

首次生成会下载较多依赖，时间可能较长。成功的唯一判据不是“运行了很久”，而是命令返回0并出现：

```text
~/microros_jazzy_ws/firmware/build/libmicroros.a
~/microros_jazzy_ws/firmware/build/include/
```

### 4.6 生成后必须做的四项检查

```bash
cd "$HOME/microros_jazzy_ws"

test -f firmware/build/libmicroros.a && echo "library OK"
test -f firmware/build/include/common_msgs/msg/detail/led_cmd__struct.h \
  && echo "LedCmd header OK"
test -f firmware/build/include/common_msgs/msg/detail/key_state__struct.h \
  && echo "KeyState header OK"
grep -n 'beep_mode' \
  firmware/build/include/common_msgs/msg/detail/led_cmd__struct.h
arm-none-eabi-ar t firmware/build/libmicroros.a | grep -i 'led_cmd'
arm-none-eabi-ar t firmware/build/libmicroros.a | grep -i 'key_state'

arm-none-eabi-readelf -A firmware/build/libmicroros.a \
  | grep -E 'Tag_CPU_name|Tag_FP_arch|Tag_ABI_VFP_args' \
  | sort -u
```

需要确认：

- 头文件里同时有 `led_mode[6]` 和 `beep_mode`；
- `.a` 中存在 `KeyState`和 `LedCmd` generator/type-support目标；
- 属性与 Cortex-M4F、FPv4-SP-D16、hard-float一致，不再是当前参考库的FPv5属性。

### 4.7 不要立刻覆盖当前可回退产物

先把新结果放到工程的待验证目录：

```bash
mkdir -p /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Middleware/Micro-ROS/generated_jazzy
cp firmware/build/libmicroros.a \
  /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Middleware/Micro-ROS/generated_jazzy/
cp -a firmware/build/include \
  /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Middleware/Micro-ROS/generated_jazzy/
```

确认上述检查全部通过后，再做一次单独的工程变更：

1. 根 `CMakeLists.txt` 临时指向 `generated_jazzy/include` 和新 `.a`；
2. 从编译源中移除 `extra_sources/custom_types/key_state_type_support.c`和 `led_cmd_type_support.c`，否则新库已包含正式类型支持，临时实现不应继续存在；
3. 不再使用两种消息的临时头，确保编译器优先包含生成目录；
4. Debug/Release重新编译；
5. 烧录后先验证Agent session，再验证两个PUB、LED/beep SUB和同步srv；
6. 完成断线与长稳后，才把新生成产物提升为正式 `Middleware/Micro-ROS/libmicroros.a + include`。

这样做可以在新库有版本/ABI问题时立即回到当前参考产物。静态库和头文件必须整套替换，不能只换 `.a` 而继续使用旧头文件。

## 5. 本轮状态与下一次板测顺序

本轮已经完成：

- 保留用户新增的 `.msg`/源码注释；
- `LedCmd`新增蜂鸣器字段和4种模式；
- MCU临时结构、CDR类型支持、回调、Queue和蜂鸣业务同步修改；
- Debug编译通过：RAM 94744 B / 128 KB，Flash 135884 B / 512 KB；
- Release编译通过：RAM 94736 B / 128 KB，Flash 114544 B / 512 KB；
- 新增STM32G474静态库生成toolchain与 `colcon.meta`；
- 更新接口设计和原09中的 `ros2 topic pub` 示例。

下一次板测严格按顺序：

1. 重新烧录当前固件；
2. 重建并source PC `common_msgs`；
3. 启动Agent，看是否出现 `create_client/session`；
4. 若仍只有两行，立即用逻辑分析仪看PA2/PA3并打上述5个断点，不再排查Topic层；
5. session建立后再启动PC测试脚本，确认同步响应、`mcu_status`与按键PUB；
6. 最后执行LED与4种蜂鸣器cmd；
7. 并行完成第4章的正确ABI静态库生成，以替换当前参考库和临时 `LedCmd` 实现。
