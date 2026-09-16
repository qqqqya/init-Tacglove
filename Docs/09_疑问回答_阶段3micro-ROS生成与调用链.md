# 09-疑问回答：阶段3 micro-ROS 的生成、交接与代码调用链

> 核对基准：本工程 `d11b63c`（阶段2结束）到 `d635988`（阶段3当前版本），以及本文件编写时的工作树。本文解释**当前已经实现的行为**；未在本机实测的 Agent 运行结果不写成验收结论。原验证步骤仍见 [09_阶段3_micro-ROS移植与验证.md](09_阶段3_micro-ROS移植与验证.md)。

## 1. 先把最容易混淆的三件事分开

1. `Interfaces/common_msgs/*.msg`、`*.srv` 是**人维护的接口源定义**。PC 与 MCU 必须使用同一版定义；不应让上位机团队和嵌入式团队各自手写一份。`package.xml`、`CMakeLists.txt` 是构建这个 ROS 2 接口包所需的源文件。
2. `libmicroros.a` 是**MCU 交叉编译静态库**，生成流程执行时由工具链自动编译/归档，但**本工程当前没有执行重新生成流程**。当前 `Middleware/Micro-ROS/libmicroros.a` 和大部分 `include/` 来自参考工程；PC 执行 `colcon build common_msgs`、或者构建 Agent，均不会自动更新这个 MCU 静态库。
3. `KeyState`和 `LedCmd`是当前例外：参考静态库中有旧 `ButtonEvent`、`MCUStatus`和 `DeviceSynchronization`，却没有本工程命名后的 `KeyState`和新增 `LedCmd`。所以工程暂时为两个类型补入声明、结构头和CDR类型支持。这是临时**实现补丁**，接口源定义仍以 `Interfaces/common_msgs`中的 `.msg`为准。

检查静态库的依据是 `arm-none-eabi-ar t Middleware/Micro-ROS/libmicroros.a`：它列出参考接口的 `libcommon_msgs__rosidl_*` 目标文件，而没有 `led_cmd` 目标文件。项目 [CMakeLists.txt](../CMakeLists.txt) 另外编译 [led_cmd_type_support.c](../Middleware/Micro-ROS/extra_sources/custom_types/led_cmd_type_support.c) 并链接这份现成 `.a`。因此“自动生成”指**未来运行生成流程时**，不等于“改了 `.msg` 后本工程的普通 CMake 构建会自动刷新 `.a`”。

## 2. `libmicroros.a` 到底是什么、从什么生成

`.a` 是 ARM GNU 工具链归档的若干 `.o` 目标文件，最终由 MCU 固件链接器从中取出用到的函数/类型支持，合入 ELF/BIN。它不是 PC 程序，不是 Agent，也不是可烧录的完整固件。典型内容包括 `rcl`/`rclc`、`rmw_microxrcedds`、Micro XRCE-DDS Client、Micro CDR、ROSIDL 运行时，以及选中的消息/服务 C 代码和 micro XRCE-DDS 类型支持；本工程归档中能直接看到 `librmw_microxrcedds-*.obj`、`libcommon_msgs__rosidl_*.obj`。

生成它至少需要四类输入：

| 输入 | 本项目对应物 | 为什么需要 |
|---|---|---|
| micro-ROS/ROS 2 客户端源码及其依赖 | 生成工作区中的 `rcl`、`rclc`、`rmw`、XRCE Client 等包 | 提供 `rcl_publish`、Executor、传输协议等实现 |
| 消息源包 | `Interfaces/common_msgs`，以及 `std_msgs` 等依赖 | 生成 C 结构、函数、序列化/反序列化和类型支持 |
| 交叉编译工具链与目标参数 | `arm-none-eabi-*`、目标 Cortex-M4F/ABI、编译标志、链接约束 | 产物必须能与本 STM32 固件 ABI 一致地链接 |
| micro-ROS 配置 | transport、内存/实体数量等 `colcon.meta` 或生成器配置 | 决定库编入的传输实现与资源上限 |

所以编译过程**包含 CMake**，也包含 `rosidl` 代码生成、`colcon` 编排、ARM 编译器和 `ar` 归档；不能说“这一个文件只是 CMake 生成的”。历史参考 `.a` 的确切源码提交、toolchain 和配置文件无法仅凭 `.a` 还原，未来做可复现生成工作区时应把这些版本与配置记录下来。

### 2.1 PC 接口生成与 MCU 静态库生成是两条不同流水线

```text
同一份 Interfaces/common_msgs/{msg,srv,package.xml,CMakeLists.txt}
       ├─ PC：ROS 2 Jazzy + colcon build
       │       └─ ~/tacapp_ros2_ws/install/common_msgs
       │          Python/C 类型、接口描述、ament 包索引 → ros2 interface / rclpy
       └─ MCU：micro-ROS 静态库生成器 + ARM 交叉工具链
               └─ ARM 头文件 + libmicroros.a → MCU 工程 CMake 链接
```

PC 的 [common_msgs/CMakeLists.txt](../Interfaces/common_msgs/CMakeLists.txt) 调用 `rosidl_generate_interfaces()`，把三个 `.msg` 和一个 `.srv` 交给生成器；[package.xml](../Interfaces/common_msgs/package.xml) 声明包名、依赖和 `ament_cmake` 构建类型。`colcon build` 通常先解析定义成 IDL，再调用各语言生成器，最后编译/安装 Python、C/C++ 类型支持及接口资源索引。安装目录里看到的 `.idl`、`.json`、`_led_cmd.py`、`package.bash`、ament 索引等都是**PC 构建产物**，通常不作为人工协议定义修改。XML `package.xml` 是 ROS 包清单；Agent 构建时出现的 XML profile 又是 Agent 用的实体配置，两者不是 MCU 业务代码。MCU 侧使用 C 头、类型支持和 `.a`，不运行 Python，也不执行 `package.xml`。

官方 STM32 工具明确允许把自定义包放入静态库生成器的 `extra_packages/`，并由 builder 连同头文件和库一起输出；这说明“重新生成”的目标是**用同一源包重建 MCU 产物**，而不是手工改 `.a`。[micro-ROS STM32CubeMX utilities](https://github.com/micro-ROS/micro_ros_stm32cubemx_utils)

### 2.2 后续怎样真正重新生成（现在尚未执行）

1. 建立独立的**MCU 静态库生成工作区**。选择与当前 MCU/PC ROS 发行版和 STM32G474 ARM ABI 匹配的生成器、工具链与 `colcon.meta`；先记录参考库版本，不应把当前 `~/microros_jazzy_ws` Agent 工作区误当静态库生成工作区。
2. 把**同一份** `Interfaces/common_msgs` 作为生成器的自定义源包输入，确保 `LedCmd.msg`、其他两个 msg 和 srv 都被发现；它依赖的 `std_msgs/Header` 也必须进入构建。
3. 运行生成器：它使用 `rosidl` 生成 C 头/序列化类型支持，`colcon`/CMake 用 `arm-none-eabi-*` 编译，`ar` 归档为新的 `libmicroros.a`。具体命令取决于选定的生成器及其 toolchain 文件，**本工程目前没有现成、经验证可复用的一条命令**；不能把 `ros2 run micro_ros_setup build_agent.sh` 当成这一步。
4. 在新产物中检查 `LedCmd` 目标/符号存在；替换 MCU 的 `.a` 与**配套** `include/`，移除临时 `led_cmd_type_support.c` 和三份手工头，修改根 `CMakeLists.txt` 不再编译临时代码；Debug/Release 重新链接并验证 MCU↔PC 的全部四种接口。
5. 保存生成器源码版本、ROS 发行版、toolchain、`colcon.meta`、接口包版本及产物校验值，以后协议变更时由同一流水线重建 PC 和 MCU 产物。不要直接把新库覆盖进当前已能工作的工程并假定兼容。

## 3. 给上位机团队什么文件、确认什么

**直接交付整个** [Interfaces/common_msgs](../Interfaces/common_msgs) **源包**，不要只给 `build/` 或 `install/` 目录：

```text
Interfaces/common_msgs/
├── package.xml
├── CMakeLists.txt
├── msg/
│   ├── KeyState.msg
│   ├── MCUStatus.msg
│   └── LedCmd.msg
└── srv/
    └── DeviceSynchronization.srv
```

再附上 [08_阶段3_micro-ROS消息接口设计.md](08_阶段3_micro-ROS消息接口设计.md) 作为业务说明、[stage3_pc_test.py](../Interfaces/stage3_pc_test.py) 作为当前监视/同步服务示例；Python 脚本不是消息定义，且**尚无 `LedCmd` publisher**。PC 团队自己的上位机程序要订阅 MCU 两条 PUB，发布一条 `LedCmd`，实现一个同步服务。

| MCU 方向 | ROS 名称 / 类型 | 目前约定要确认的语义 |
|---|---|---|
| PUB → PC SUB | `/mcu_dev/key_state` / `common_msgs/msg/KeyState` | 事件枚举1长按、2单击/录制切换、3双击、4故障确认预留；是离散事件，不是持续GPIO电平 |
| PUB → PC SUB | `/mcu_dev/mcu_status` / `common_msgs/msg/MCUStatus` | 1 Hz；版本、uptime、状态、连接标志、收发计数；当前 `COLLECTING` 映射到 `STATE_READY=1`，**不是逐帧录制成功确认** |
| SUB ← PC PUB | `/mcu_dev/led_cmd` / `common_msgs/msg/LedCmd` | 固定 `uint8[6] led_mode`，下标 0..5 = LED2..LED7、模式0..5；`uint8 beep_mode`模式0..3；鱼眼无灯；没有逐灯状态回报或每条cmd的ACK |
| Client → PC Server | `/mcu_dev/sync` / `common_msgs/srv/DeviceSynchronization` | MCU 发 `sync_request=true`；PC 返回 `sync_state=true` 和 `header.stamp`，用于 MCU 消息时间戳 |

双方还应确认 `ROS_DOMAIN_ID=9`、节点固定 `mcu_dev`（开发期单板）、115200 USART2/CH340 只是 Agent↔MCU 的物理链路，以及时间戳使用 PC ROS clock。`package.xml` 目前的 `embedded-dev@example.com` 是占位 maintainer 地址；正式交付前应由团队确认实际维护人。`message_rx_count` 在回调收到 `LedCmd` 时先加一，即使六灯模式非法随后被 LED task 拒绝也会计数，**不能当作有效 cmd 成功执行次数**。

当前代码还有两处**名称/注释与实际实现不一致**，交接时不能隐去：`Tasks/led_task.c` 中 `COLOR_BLUE={LED_BRIGHTNESS,0,LED_BRIGHTNESS}`，按 RGB 分量理解是红+蓝（紫/洋红），不是协议名 `MODE_BLUE_*` 所承诺的纯蓝；这次只说明现状，没有改动已验证的灯效。`BSP/KEY/bsp_key_handler.c/.h` 注释写 PA11，但读取的是 `key_cap` 引脚，而当前 `.ioc` 把 `key_cap` 配在 **PA0**；实际硬件链路应以 `.ioc`/生成的引脚宏为准，后续可统一修正注释。

## 4. 终端 Agent 日志在说什么

Agent 是 PC/WSL 侧的 XRCE-DDS↔ROS 2 桥。它拿到串口字节，代表 MCU 在 ROS 图里创建节点、publisher、subscription、client，再把双方消息转发；它不“定义” `LedCmd` 的业务动作。[micro-ROS Agent 官方说明](https://github.com/micro-ROS/micro-ROS-Agent)

先区分**构建日志**和**运行日志**：你前一轮截图中的 `Starting >>> micro_ros_agent`、`[Processing: micro_ros_agent]`、`Cloning into 'spdlog'`、`Failed <<< micro_ros_agent` 是 `build_agent.sh`/`colcon` 构建输出。`spdlog` 的 HTTP/2 clone 错误导致构建失败，所以那时 `ros2 pkg prefix micro_ros_agent` 得到 `Package not found` 正常；这**不是 MCU 串口通信运行日志**。只有包构建安装成功，再执行类似 `ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyUSB0 -b 115200 -v 4` 才会看到运行日志。具体选项请以本机 `--help` 为准。

| 运行日志关键词（不同版本措辞可能变） | 层级与含义 | 下一步应看什么 |
|---|---|---|
| serial open / port / baud | Agent 已打开 Linux 串口 | 只证明端口可用，不证明 MCU 已连接 |
| create client / session established | MCU XRCE 客户端握手成功 | 开始看实体创建；若没有，检查 MCU `micro_ros_create_entities()` |
| create participant / topic / publisher / subscriber / requester | Agent 代表 MCU 创建 DDS/ROS 实体 | 用 `ros2 node list`、`topic list -t`、`service list -t` 对照四条名称 |
| write/read/payload 或高 verbosity 帧日志 | XRCE 数据帧流量 | 仅有帧流量不代表 Python 业务回调已执行 |
| delete session / disconnect / timeout | 会话断开或实体释放 | 对照 MCU 连续 ping 失败、USB 拔插、Agent 停止 |
| deserialization / type / CDR 错误 | 类型定义或序列化不一致的可能性 | 对比 PC `common_msgs` 与 MCU 的 `LedCmd` 类型支持、发行版、数组/字段顺序 |

**没有本次“终端2”真实运行日志文本**，这里只能给关键词和层级，不能逐行断言某行是正常还是异常。把从启动 Agent 到第一次 MCU 连接/失败的原始日志贴出后，才能按行解释。`-v 6` 很详细，首轮排障可保存日志，平时用较低 verbosity。

## 5. 阶段2到当前阶段3，源码上实际变了什么

这是以阶段2提交 `d11b63c` 与当前提交 `d635988` 的**源码差异**分类，不把 `build/Debug`、`build/Release` 里的 `.obj`、索引或 ELF 当手写实现。阶段3没有修改 `.ioc`；`Core/Src/main.c` 的差异只涉及 TIM3 初始化旁的注释，不能解释为重新配置外设。

| 分类 | 增加/改动 | 实际职责 |
|---|---|---|
| 协议源定义（PC/MCU 共用） | `Interfaces/common_msgs/{msg,srv,package.xml,CMakeLists.txt}` | 3 msg + 1 srv、生成配置、包元数据 |
| PC 测试逻辑 | `Interfaces/stage3_pc_test.py` | 订阅按键/MCU 状态，回答同步请求；不直接操作串口 |
| MCU ROS 业务/连接状态机 | 新增 `Tasks/micro_ros_task.c/.h`；修改 `Tasks/task_manager.c/.h`并将任务状态集中到 `task_manager.h` | Agent ping、实体创建/销毁、Executor、PUB/SUB/Client、队列与任务创建 |
| 原有任务衔接 | 修改 `Tasks/key_task.c`、`Tasks/led_task.c/.h` | 按键本地动作后另送 ROS 事件；LED 单元素 cmd 邮箱、远程模式和断线回本地 |
| MCU 通信适配 | 新增 `dma_transport.c/.h` | USART2 RX 循环 DMA / TX DMA 与 micro-ROS 自定义 transport API 对接 |
| MCU 库/资源适配 | 新增参考 `libmicroros.a` 与 `include/`；新增 allocator、25 KB 专用内存管理、时间适配 | micro-ROS API 实现、类型支持、内存/时间接口 |
| 临时新类型补丁 | 新增 `KeyState`、`LedCmd`的C头及CDR类型支持 | 让现成参考静态库可处理当前正式接口；将来统一再生成后删除 |
| 构建 | 修改根 `CMakeLists.txt` | 编译新增 `.c`、加入 include、链接 `.a` |
| 其他历史修改 | `BUTTON/button.c/.h` 在阶段2→当前间删除；BSP 灯/蜂鸣文件也有注释及 LED 复位 DMA 帧长度修改 | 与 ROS 业务接线不同；不能一概称为 micro-ROS 必要改动 |

**软件业务逻辑**主要是 `key_task`、`led_task` 及 `micro_ros_task` 中的状态/消息处理；**物理设备通信**主要是 `dma_transport` + USART2/DMA；`rcl`/`rclc`/`rmw` 等库负责 XRCE/ROS 协议。最终的路径是 `PC ROS 节点 ↔ ROS/DDS ↔ Agent ↔ CH340/USART2 DMA ↔ MCU micro-ROS task`，不是 PC Python 脚本直接串口发 `LedCmd` 结构体。

## 6. MCU 任务代码的逐层调用路径

### 6.1 创建与启动

`main()` 完成 HAL 外设初始化后，`task_manager_init()` 先调用 `led_task_resources_init()` 创建**容量1的最新值 LED cmd Queue**，再调用 `micro_ros_task_resources_init()` 创建**容量8的按键事件 Queue**，然后直接 `xTaskCreate()` 创建 LED、key、micro_ros 三个任务。代码在 [task_manager.c](../Tasks/task_manager.c)。任务不是 Agent 进程；它们在 MCU FreeRTOS 中运行。

`micro_ros_task_entry()` 一次性调用 `micro_ros_init_allocator()`、`micro_ros_init_messages()`、`micro_ros_zero_entities()` 和 `rmw_uros_set_custom_transport(true, &huart2, open, close, write, read)`；其中 `true` 指自定义**串行流**传输需要 framing。自定义四个回调在 [dma_transport.c](../Middleware/Micro-ROS/extra_sources/microros_transports/dma_transport.c)：`open` 启动 2048B RX 循环 DMA；`write` 用 `HAL_UART_Transmit_DMA`；`read` 按 DMA `NDTR` 推算写入尾部并取字节；`close` 停止 UART DMA。`rcl`/`rmw` 处理 ROS/XRCE 协议，HAL 只负责收发字节。

### 6.2 主循环不是“等 PC 发一条再继续”

```text
micro_ros_task_entry() 的 for (;;)
  WAIT_AGENT:
    connected=false
    rmw_uros_ping_agent(100 ms, 1次)
    └─ 成功 → micro_ros_create_entities() → RUNNING
    vTaskDelay(500 ms)

  RUNNING:
    rclc_executor_spin_some(最多5 ms) → 有新LedCmd/同步响应才调回调
    micro_ros_publish_key_events()      → Queue中有事件才 rcl_publish()
    micro_ros_process_time_sync()       → 未同步且到期才 rcl_send_request()
    每1 s micro_ros_publish_mcu_status() → rcl_publish()
    每1 s rmw_uros_ping_agent()          → 连续3次失败，清Queue、放弃远程LED、
                                          micro_ros_fini_entities()、回 WAIT_AGENT
    vTaskDelay(2 ms)
```

连接成功后 `micro_ros_create_entities()` 先设置 Domain ID 9，调用 `rclc_support_init_with_options()`、`rclc_node_init_default()`，再用 `rclc_publisher_init_default()` 建两个 PUB、`rclc_subscription_init_default()` 建 `LedCmd` SUB、`rclc_client_init_default()` 建同步 Client；`rclc_executor_init(...,2,...)` 添加一个 SUB 回调和一个 Client 响应回调。PUB 由循环主动调用，不占 Executor 两个 handle。Agent 不可用时本地 key/LED/beep 任务仍运行，但 ROS 实体不创建。见 [micro_ros_task.c](../Tasks/micro_ros_task.c)。

读代码时可以按这些入口定位（行号按当前版本）：

| 当前文件/行 | 先看什么 | 关注点 |
|---|---|---|
| `Tasks/task_manager.c:21` | `task_manager_init()` | 两个 Queue 资源和三个 FreeRTOS task 的创建 |
| `Tasks/micro_ros_task.c:538` | `micro_ros_task_entry()` | 初始化、`WAIT_AGENT`/`RUNNING` 的 `for (;;)` |
| `Tasks/micro_ros_task.c:250` | `micro_ros_create_entities()` | 节点、2 PUB、1 SUB、1 Client、Executor 的创建 |
| `Tasks/micro_ros_task.c:429`、`:447` | 两个 `micro_ros_publish_*()` | Queue 事件 PUB、1 Hz 状态 PUB 的 `rcl_publish()` |
| `Tasks/micro_ros_task.c:211`、`:225` | 两个 callback | 收到 `LedCmd`、收到同步 service response 的处理 |
| `Tasks/micro_ros_task.c:468` | `micro_ros_process_time_sync()` | 请求/超时/重试；实际发包为 `rcl_send_request()` |
| `Tasks/led_task.c:413` | `led_task_entry()` | 独立消费 cmd Queue，驱动 PWM DMA，而非在 ROS 回调直接点灯 |
| `Interfaces/stage3_pc_test.py:62` | `rclpy.spin()` | PC 的 subscription/service 回调事件循环 |

### 6.3 按键 MCU PUB：什么函数何时调用

```text
PA0 → bsp_key_handler_process()/get_event()（key_task，每1 ms）
    ├─ led_task_on_short_press/long_press/double_click() → 本地LED/beep
    └─ micro_ros_task_enqueue_key_event(event) → 8元素Queue（仅Agent已连时）
         → micro_ros_publish_key_events() → xQueueReceive(0等待)
            → micro_ros_fill_stamp() → rcl_publish(key_state_pub, KeyState)
            → Agent → PC的 /mcu_dev/key_state 订阅回调
```

关键点：按键任务**没有**直接调用 `rcl_publish`；它只把已识别的离散事件入队。本地动作先发生，Agent断线或Queue满只影响远端 PUB；已经离线发生的事件不在重连后补发。时间同步前 `header.stamp` 为 0。`rcl_publish` 返回 `RCL_RET_OK` 后才增加 MCU 的 TX 计数，这代表 API 成功提交，不是 PC 上位机已经执行业务的端到端确认。

### 6.4 MCU 状态 PUB

每 1 s `micro_ros_publish_mcu_status()` 填时间戳、FreeRTOS uptime、`led_task_get_system_state()` 的业务状态映射、`agent_connected`、TX/RX计数，然后 `rcl_publish(mcu_status_pub, MCUStatus)`。同步未完成也会发布，只是时间戳为 0。当前内部 `SELF_TEST`/`PREPARING` → 消息 `STATE_CALIBRATING=3`，`IDLE` → `STATE_IDLE=0`，`COLLECTING` → `STATE_READY=1`，故障 → `STATE_ERROR=2`；这个映射要上位机团队确认其业务解释。

### 6.5 PC PUB 到 MCU SUB：没有阻塞等待函数

```text
PC上位机 / ros2 topic pub LedCmd
  → ROS图 → Agent → USART2 RX DMA → micro-ROS反序列化
  → rclc_executor_spin_some() 检测 ON_NEW_DATA
  → micro_ros_led_cmd_callback()
  → led_task_submit_cmd(led_mode[6], beep_mode)：验证灯0..5、蜂鸣器0..3，xQueueOverwrite(容量1)
  → led_task_entry() xQueueReceive(0等待)，串行执行灯光与蜂鸣器模式
  → LED：bsp_led_handler_commit() → bsp_led_driver_commit() → TIM3 PWM + DMA
  → 蜂鸣器：bsp_beep_driver_set()/bsp_beep_driver_beep()
```

因此“等待 PC PUB”并不是 `while` 中读字符串、也不是用串口 `input()`；循环每次运行 Executor，让库把到达的消息分发给回调。回调不直接驱动LED或蜂鸣器硬件。新cmd覆盖旧未执行cmd；任一字段非法则整条拒绝。当前回调会先增加RX计数，再调用 `led_task_submit_cmd()`，但未向PC发布ACK或LED状态。Agent连续健康检查失败时 `led_task_release_remote_control()` 让LED task恢复本地显示并关闭远程蜂鸣器动作；恢复连接后PC必须重新发布cmd。

## 7. `DeviceSynchronization.srv` 实际怎样同步

[DeviceSynchronization.srv](../Interfaces/common_msgs/srv/DeviceSynchronization.srv) 的 `---` **前面是请求**：`bool sync_request`；**后面是响应**：`std_msgs/Header header` 与 `bool sync_state`。这里 MCU 是 `rclc_client_init_default()` 创建的**Client**，PC Python 是 `create_service()` 创建的**Server**，不是 MCU 提供服务给 PC 调用。

```text
MCU RUNNING、未同步
  micro_ros_process_time_sync():
    s_sync_request.sync_request = true（初始化时已赋值）
    rcl_send_request(sync_client, &request, &sequence)
    s_sync_pending = true，记发送tick
                     ──Agent/ROS──→ PC stage3_pc_test.py::_on_sync(request,response)
                                    response.header.stamp = PC ROS clock现在值
                                    response.sync_state = request.sync_request
                     ←─Agent/ROS── response
  下一次 rclc_executor_spin_some() → micro_ros_sync_callback(response)
    sync_state=true：epoch_base_ns = PC时间ns - MCU当前uptime_ns
    s_time_synced=true；此后 header.stamp = epoch_base_ns + 当前uptime_ns
```

刚连接时把请求 tick 回拨，使请求尽快发出。代码每轮检查，但发送失败/未答复会按约 2 s 重试，已发请求 1.5 s 无响应则清 `pending`；一旦同步成功，目前**不会定期重新校时**。`rcl_send_request` 的 sequence 被库写入 `s_sync_sequence`，当前应用回调**没有自行比较响应 sequence**。实现是一次简化的时间基准估计，未补偿串口/Agent/ROS 往返延迟，也不等于精确 PTP/NTP。PC服务不启动时按键和灯仍可运行，消息时间戳保持0。

直接使用的 micro-ROS/ROS C API 包括 `rmw_uros_set_custom_transport`、`rmw_uros_ping_agent`、`rclc_*_init_default`、`rclc_executor_*`、`rcl_publish`、`rcl_send_request`；`micro_ros_publish_*`、`micro_ros_process_time_sync`、`led_task_submit_cmd`、`cubemx_transport_*` 则是**本工程自己编写的函数**。`HAL_UART_*_DMA` 是 STM32 HAL，不是 ROS API。

## 8. 两种 Python 文件不要混成一种串口交互

当前 [stage3_pc_test.py](../Interfaces/stage3_pc_test.py) 在 PC 的 ROS 2 Python 环境里：`rclpy.init()` → 创建两个 subscription 和一个 service → `rclpy.spin()` 持续调度回调。`_on_key_state()`/`_on_mcu_status()` 只打印 MCU 发布的数据；`_on_sync()` 填当前 PC ROS 时间并返回响应。**脚本没有键盘输入、没有 `serial.Serial()`、没有 LED publisher**；实际串口由 Agent 进程独占、读写。上位机要下 LED cmd，可另写 rclpy publisher 或使用 `ros2 topic pub`。

参考工程 [sn_tool.py](../../Tacglove/AppEncrypt/sn_tool.py) 是另一条**Bootloader 直连串口**路径：`input()` 读取用户在 PC 键盘输入的选择/SN，Python 先处理，再通过 `pyserial.Serial` 的 `ser.write()` 向 Bootloader 发字节（如 `*F_SN_R`、`#F_SN_W`、SN 字符串、跳转指令，升级时还有固件传输）。不是“键盘从串口传给 Python”；方向是**PC键盘 → Python → 串口 → MCU Bootloader**。参考另一个 [micro_ros_publish_device_ctrl_data.py](../../Tacglove/AppEncrypt/micro_ros_publish_device_ctrl_data.py) 也用 `input()`，但它随后调用 ROS `publisher.publish(msg)`，路径是**PC键盘 → Python ROS publisher → Agent → 串口 → MCU App**。当前项目阶段3只做后者的 ROS App 路径，尚未移植 Bootloader/SN/IAP。不要让 `sn_tool.py` 和 Agent 同时占用同一个 USART2/CH340 串口。

## 9. WSL/ROS 安装与工作区到底在哪里

“工作空间”只是一个按 ROS 习惯分成 `src/`（源文件）、`build/`（构建）、`install/`（安装产物）、`log/`（日志）的**目录**，不是另一套 Linux，也不是 ROS 系统安装目录。`source` 仅把所选 `install/` 暴露给**当前终端**；不自动改变别的终端，更不在 MCU 上执行。

| 路径（WSL 内） | 含义 | 当前材料能证明什么 |
|---|---|---|
| `/opt/ros/jazzy` | 系统级 ROS 2 Jazzy 安装（`source /opt/ros/jazzy/setup.bash`） | 已能执行 ROS 工具；不是本项目 `common_msgs` 的源目录 |
| `/home/embedded/tacapp_ros2_ws` | 普通用户 PC 接口工作区，`src/common_msgs` 从 D 盘项目源包复制 | 先前 `ros2 pkg prefix common_msgs` 能找到 `install/common_msgs`；PC 消息产物在这里 |
| `/home/embedded/microros_jazzy_ws` | 普通用户 Agent/setup 工作区 | 截图证实已下载 `micro_ros_setup`/`micro-ROS-Agent` 等源码；当时 Agent 因 `spdlog` Git 网络下载失败，尚**不能据此说 Agent 已安装成功** |
| `/root/micro_ros_ws`、`/root/tacgloveumi` | 旧 root 用户工作区 | 截图搜索到两处 `install/.../micro_ros_agent` 包索引；属于历史 Agent 安装，不是本项目要求的日常联调路径 |
| `/mnt/d/InternWork/Code/Test_mygit/Tacapp_init` | 当前工程的 D 盘 Windows 文件夹在 WSL 里的挂载视图 | 与 `D:\InternWork\Code\Test_mygit\Tacapp_init` 是同一批源文件 |
| `/mnt/c/Users/Embedded-dev` | 终端打开时所在的 Windows C 盘挂载目录 | 提示符显示当前位置，**不表示** `~/tacapp_ros2_ws` 建在此目录 |

普通用户的 `~`/`$HOME` = `/home/embedded`；root 的 `~`/`$HOME` = `/root`。所以 root `source "$HOME/tacapp_ros2_ws/install/setup.bash"` 寻找的是 `/root/tacapp_ros2_ws`，不会找到普通用户的包。日常 PC↔MCU 联调全部用 `embedded` 用户：每个新终端分别 source `/opt/ros/jazzy/setup.bash`、已构建完成的 Agent 工作区 `install/local_setup.bash`、接口工作区 `install/local_setup.bash`，设置 `ROS_DOMAIN_ID=9`。只有 USB/IP attach、安装系统包、加入 `dialout` 组等系统管理步骤可能需要 `sudo`；**不需要进入 root shell 运行 Agent 或 Python**。

在 Windows 上，`/home/embedded/...`、`/root/...`、`/opt/ros/jazzy` 位于该 WSL 发行版的 Linux ext4 虚拟盘中，不等同于 `C:\Users\Embedded-dev\...`；可在资源管理器用 `\\wsl.localhost\Ubuntu-24.04\home\embedded\...` 查看。WSL2 底层通常有一个 `ext4.vhdx` 文件，但它**是否在 C 盘、以及本机的精确 Windows 路径，当前截图不能确认**（也可能是自定义导入位置）。可在 PowerShell 只读查询：

```powershell
wsl --list --verbose
(Get-ChildItem HKCU:\Software\Microsoft\Windows\CurrentVersion\Lxss |
  Where-Object { $_.GetValue('DistributionName') -eq 'Ubuntu-24.04' }).GetValue('BasePath')
```

第二条若显示路径，再在该目录找 `ext4.vhdx`；若无输出，不要猜一个 C 盘路径，也不要直接在 Windows 里修改 VHDX。WSL Linux 目录与 `/mnt/c`、`/mnt/d` 的关系见 [微软 WSL 文件系统说明](https://learn.microsoft.com/en-us/windows/wsl/filesystems)；VHDX 路径定位方法见 [微软 WSL 磁盘说明](https://learn.microsoft.com/en-us/windows/wsl/disk-space)。

下载/安装的位置也分开看：Jazzy 已在 `/opt/ros/jazzy`；`git clone` 的 setup/Agent 源码在 `/home/embedded/microros_jazzy_ws/src`，Agent 构建时还会在该工作区 `build/` 拉取 XRCE Agent、`spdlog` 等依赖；`common_msgs` 是从本工程 D 盘**复制**到 `/home/embedded/tacapp_ros2_ws/src` 并在该工作区 `install/` 生成；`rosdep`/`apt` 的系统依赖安装到 Linux 系统目录而非项目 `src/`。这些操作都没有把 ROS 安装进 MCU；MCU 只链接 `.a` 并烧录最终固件。

## 10. 本阶段仍需明确/未验收的点

- PC 团队确认四个接口的字段、Topic/Service 名、`MCUStatus` 的状态映射和同步时钟来源；完成正式上位机 `LedCmd` publisher。
- 普通用户 Agent 工作区需先从 `spdlog` 网络错误恢复并以 `ros2 pkg prefix micro_ros_agent` 验证安装；旧 root 包能找到并不证明新工作区已经构建成功。
- 实机核对 Agent 运行日志、topic/srv 可见、同步后非零时间戳、非法 cmd/断线重连、长稳；目前不能把构建成功或包找到视为阶段3完整验收。
- 验收后建立 MCU 静态库**可复现**生成工作区，统一生成 `LedCmd` 类型与 `.a`，再删除当前临时代码；这个动作尚未执行。
