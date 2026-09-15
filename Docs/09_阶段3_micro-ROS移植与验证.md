# 09 阶段3 micro-ROS移植与验证

## 1. 当前结论

阶段3的固件实现、PC端接口源包和联调脚本已经接入工程，Debug与Release均已完成编译链接。当前状态是“代码侧完成，等待板上ROS 2联合验证”，不能在完成实机断线恢复和长稳测试前标记为最终验收通过。

本次没有新增 `Application` 目录，也没有修改 `glove_UMI_APP.ioc`。新增的唯一业务任务是 `Tasks/micro_ros_task.c`，并继续由 `task_manager_init()`直接完成资源初始化和三个任务的创建。

## 2. 已实现范围

### 2.1 micro-ROS基础

- 从 `handheld-umi/Application/Middleware/Micro-ROS`复用include、`libmicroros.a`、时间适配和FreeRTOS分配器思路；
- 使用独立25 KB micro-ROS heap，避免ROS实体动态内存与FreeRTOS任务栈争用同一个heap；
- MCU节点固定为 `mcu_dev`，Domain ID固定为9；
- `ButtonEvent`、`MCUStatus`、`DeviceSynchronization`保持参考工程线格式不变；
- 新增 `LedCmd` 的六元素结构和micro XRCE-DDS CDR类型支持；
- Executor容量为2，只处理一个subscription和一个service client；两个publisher不占Executor handle。

### 2.2 USART2 DMA传输

当前 `.ioc` 已经满足阶段3要求，所以没有改动：

| 资源 | 当前配置 |
|---|---|
| USART2 TX/RX | PA2/PA3，115200，8N1，无流控 |
| RX DMA | DMA1_Channel2，Circular，High |
| TX DMA | DMA1_Channel3，Normal，High |
| LED DMA | TIM3_CH3使用DMA1_Channel1，Very High |
| IRQ | USART2及其DMA优先级6 |

RX使用2048字节循环DMA缓存并依据NDTR读取；TX使用DMA，超时由实际帧长和波特率计算，不再无限等待。USART2只承载XRCE-DDS字节流，不在该串口混入调试打印。

### 2.3 连接状态机

```text
WAIT_AGENT
    ↓ ping成功
CREATE_ENTITIES
    ↓ 全部成功
RUNNING
    ├── spin_some
    ├── key_state事件发布
    ├── mcu_status 1 Hz发布
    ├── 时间同步请求/响应
    ├── led_cmd接收
    └── 1 Hz Agent健康检查
             ↓ 连续3次失败
       DESTROY_ENTITIES
             ↓
         WAIT_AGENT
```

Agent未启动、USB拔出或同步服务未启动都不会复位MCU。Agent断线时会丢弃尚未发布的按键事件、释放远程灯光控制并恢复本地LED状态，然后继续等待自动重连。

### 2.4 按键PUB

```text
PA0 → bsp_key_handler → key_task
                       ├── 原有本地LED/蜂鸣器动作
                       └── 8元素ButtonEvent Queue
                                      ↓
                              micro_ros_task
                                      ↓
                         /mcu_dev/key_state
```

ROS事件值保持参考工程定义：

| 本地事件 | ROS事件值 | `ButtonEvent`常量 |
|---|---:|---|
| 长按 | 1 | `EVENT_LONG_PRESS` |
| 单击 | 2 | `EVENT_REC_TOGGLE` |
| 双击 | 3 | `EVENT_DOUBLE_CLICK` |

Agent未连接或Queue满时只丢弃ROS上报，本地动作不受影响，也不会在重连后回放过期按键。

### 2.5 LED cmd SUB

```text
/mcu_dev/led_cmd → micro_ros_task回调
                  ↓ 校验六个模式值
              单元素最新值Queue
                  ↓
               led_task
                  ↓
        BSP Handler → TIM3 PWM DMA
```

`led_mode[6]`按LED2、LED3、LED4、LED5、LED6、LED7排列。模式定义为：

| 值 | 模式 |
|---:|---|
| 0 | 灭 |
| 1 | 绿色常亮 |
| 2 | 绿色闪烁 |
| 3 | 红色常亮 |
| 4 | 蓝色闪烁 |
| 5 | 蓝色常亮 |

闪烁半周期固定为250 ms，亮度继续使用MCU端 `LED_BRIGHTNESS=1`。任一元素大于5时整条cmd拒绝，不会先更新部分灯。Queue长度为1，新cmd覆盖尚未执行的旧cmd。

本地LED状态在收到第一条合法远程cmd前保持不变；远程控制生效后，按键仍然发布事件并驱动蜂鸣器，但不临时覆盖PC指定的灯色。Agent断线后恢复“相机灯绿色常亮、LED7按当前采集状态显示”的本地画面。

### 2.6 MCU状态PUB和同步srv

`/mcu_dev/mcu_status`每1秒发布一次：固件版本、运行秒数、状态、Agent连接状态和消息收发计数。内部状态映射到参考消息如下：

| 本工程内部状态 | `MCUStatus.system_state` |
|---|---|
| 上电自检、采集准备 | `STATE_CALIBRATING=3` |
| 待机 | `STATE_IDLE=0` |
| 采集中 | `STATE_READY=1` |
| 故障 | `STATE_ERROR=2` |
| IAP预留 | `STATE_UPDATING=4` |

MCU作为 `/mcu_dev/sync` 的service client。PC返回 `sync_state=true` 和ROS时间后，后续消息填同步时间戳；PC同步server未运行时消息时间戳保持0，但pub/sub继续工作。

## 3. 通信协议、生成文件和PC/MCU职责边界

### 3.1 从接口源文件到实际通信代码

本项目的消息生成链路如下：

```text
我们维护的协议源文件
  ButtonEvent.msg / MCUStatus.msg / LedCmd.msg
  DeviceSynchronization.srv
                    │
                    ├── PC端：colcon + rosidl生成
                    │         Python/C类型、类型支持、ament索引
                    │                  ↓
                    │         ros2命令和stage3_pc_test.py使用
                    │
                    └── MCU端：micro-ROS静态库生成流程生成
                              C结构体头文件、序列化代码、类型支持
                                       ↓
                              STM32工程编译、链接并烧录
```

`.msg/.srv`是协议的“源代码”，生成后的 `.h/.c/.py/.a`是协议的“编译产物”。PC和MCU不是在运行时传输 `.msg/.srv`文件；双方各自根据同一份定义，将结构体序列化为一致的二进制字节流。两端字段类型、顺序、数组长度或包名任一不一致，都可能造成实体创建失败或反序列化错误。

### 3.2 哪些内容由项目规定，哪些内容需要我们实现

#### 3.2.1 项目协议需要我们共同规定

这些是PC和MCU必须共同遵守的通信合同：

| 规定项 | 本阶段决定 |
|---|---|
| ROS Domain ID | `9` |
| MCU节点名 | `/mcu_dev` |
| MCU上报topic | `/mcu_dev/key_state`、`/mcu_dev/mcu_status` |
| PC下发topic | `/mcu_dev/led_cmd` |
| 时间同步service | `/mcu_dev/sync`；MCU为client，PC为server |
| 消息字段 | 三个 `.msg`和一个 `.srv`中的字段、类型与顺序 |
| 枚举含义 | 按键事件值、MCU状态值、LED模式值 |
| LED数组顺序 | `led_mode[0..5]`对应LED2~LED7 |
| 发布频率 | `mcu_status`为1 Hz；按键按事件发布；LED按需下发 |
| QoS | 阶段3使用default Reliable |
| 断线语义 | 本地功能继续；过期按键不补发；远程灯控退出 |
| 物理链路 | USART2、115200、8N1、CH340 |

这些内容不能只改MCU或只改PC。特别是 `.msg/.srv`字段变化后，需要同时重新生成PC接口包和MCU静态类型支持，然后重新构建两端。

#### 3.2.2 MCU业务和板级通信代码需要我们写

| 文件 | 我们实现的内容 | 运行位置 |
|---|---|---|
| `Tasks/micro_ros_task.c/.h` | 连接状态机、ROS实体创建、PUB/SUB、service client、断线重连 | MCU |
| `Tasks/key_task.c` | 把识别完成的单击/双击/长按事件投递给ROS Queue | MCU |
| `Tasks/led_task.c/.h` | 校验并接收六灯cmd，交给LED BSP执行 | MCU |
| `Tasks/task_manager.c` | 初始化Queue并直接创建任务 | MCU |
| `Middleware/Micro-ROS/extra_sources/microros_transports/dma_transport.c/.h` | 将micro-ROS读写函数接到USART2 RX/TX DMA | MCU |
| `Middleware/Micro-ROS/extra_sources/custom_memory_manager.c`、`microros_allocators.*` | micro-ROS专用内存分配 | MCU |
| `Middleware/Micro-ROS/extra_sources/microros_time.c` | micro-ROS本地时间接口 | MCU |
| `Core/Src/usart.c`、`dma.c`及 `.ioc` | USART2和DMA的硬件初始化 | MCU |

“运行在MCU”表示这些文件会参与STM32固件编译并最终进入 `.elf/.bin`。

#### 3.2.3 ROS/micro-ROS提供、通常不由我们手写

| 文件或组件 | 来源 | 处理原则 |
|---|---|---|
| `Middleware/Micro-ROS/libmicroros.a` | micro-ROS交叉编译产物 | 链接进MCU，不直接改二进制库 |
| `Middleware/Micro-ROS/include/rcl*`、`rmw*`、`uxr*`等 | micro-ROS/ROS 2生成或上游头文件 | 第三方依赖，不做业务修改 |
| `include/common_msgs/.../__struct.h` | rosidl从接口定义生成的C结构 | 原则上重新生成，不手改 |
| `...__type_support.h/.c` | rosidl/micro XRCE-DDS生成的类型支持和序列化代码 | 原则上重新生成，不手改 |
| WSL `build/`、`install/`、`log/` | colcon构建生成 | 可删除重建，不作为源文件修改 |
| WSL生成的 `_button_event.py`等 | rosidl Python生成器 | PC运行时导入，不手改 |

当前 `LedCmd`是一个明确的阶段3例外：参考 `libmicroros.a`没有这个新类型，所以工程暂时加入了：

- `Middleware/Micro-ROS/include/common_msgs/msg/led_cmd.h`；
- `Middleware/Micro-ROS/include/common_msgs/msg/detail/led_cmd__struct.h`；
- `Middleware/Micro-ROS/include/common_msgs/msg/detail/led_cmd__rosidl_typesupport_microxrcedds_c.h`；
- `Middleware/Micro-ROS/extra_sources/custom_types/led_cmd_type_support.c`。

它们按 `LedCmd.msg`的固定六元素线格式实现并参与MCU构建。阶段3验收后，应建立完整的micro-ROS静态库生成工作区，由同一份 `Interfaces/common_msgs`统一重新生成这些文件和 `libmicroros.a`，再删除手工补充的类型支持，避免长期维护两份定义。

### 3.3 XML、msg、srv、CMakeLists分别是什么

#### 3.3.1 `package.xml`

XML是带标签的结构化文本格式。本工程当前与自定义消息直接有关的XML文件只有：

```text
Interfaces/common_msgs/package.xml
```

它是ROS 2包的清单，主要告诉colcon/ament：

- 包名是 `common_msgs`、版本是 `0.1.0`；
- 构建工具是 `ament_cmake`；
- 构建时需要 `rosidl_default_generators`；
- 运行时需要 `rosidl_default_runtime`；
- 消息字段引用了 `std_msgs`；
- 该包属于 `rosidl_interface_packages`。

`package.xml`不定义按键字段或LED字段，也不参与STM32固件编译。它只在PC/WSL中让ROS 2构建系统正确识别接口包。此前 `ros2 pkg prefix common_msgs`失败，正是因为该XML缺少正确的 `ament_cmake` build type声明。

#### 3.3.2 `CMakeLists.txt`

`Interfaces/common_msgs/CMakeLists.txt`不是XML，而是CMake脚本。它列出要让rosidl生成的三个msg和一个srv：

```cmake
rosidl_generate_interfaces(${PROJECT_NAME}
    "msg/ButtonEvent.msg"
    "msg/MCUStatus.msg"
    "msg/LedCmd.msg"
    "srv/DeviceSynchronization.srv"
    DEPENDENCIES std_msgs
)
```

它同样只在PC/WSL的colcon接口包构建中执行，不参与当前STM32工程的CMake构建。工程根目录的 `CMakeLists.txt`才负责将MCU的Task、DMA transport、类型支持和 `libmicroros.a`链接进固件，两份CMake文件不要混淆。

#### 3.3.3 `.msg`

`.msg`定义一次发布或订阅的数据结构。例如：

```text
std_msgs/Header header
uint8[6] led_mode
```

表示 `LedCmd`包含一个ROS标准Header和固定6字节的LED模式数组。文件中的常量如 `MODE_OFF=0`也属于双方通信协议。

#### 3.3.4 `.srv`

`.srv`定义一次请求和一次响应，中间用 `---`分隔。本项目：

```text
bool sync_request
---
std_msgs/Header header
bool sync_state
```

横线上方是MCU发出的同步请求，横线下方是PC server返回的带时间戳响应。它不是两个topic，而是一组有请求/响应配对关系的service类型。

#### 3.3.5 `.idl`和生成的头文件

ROS 2生成器会先把 `.msg/.srv`规范化为IDL，再生成不同语言代码。因此MCU头文件顶部会看到：

```text
generated ... with input from common_msgs:msg/ButtonEvent.idl
```

这不表示还需要在STM32工程中手写一份XML或IDL；它只是生成链路留下的来源说明。当前RCLC通过C API创建node、publisher、subscription和client，没有使用需要我们维护的“XRCE实体XML配置文件”。STM32的 `.ioc`也不是ROS package XML，它是CubeMX的外设配置文件。

#### 3.3.6 截图中 `install/common_msgs` 其他文件的含义

这些都是PC执行colcon后自动生成或安装的文件：

| 截图中的文件 | 作用 | 我们是否手改 | 是否放入MCU |
|---|---|---|---|
| `package.xml` | 安装后的包清单副本 | 修改源目录中的版本，不改install副本 | 否 |
| `package.bash`、`.sh`、`.zsh`、`.ps1` | 供不同shell加载该包环境的脚本 | 否 | 否 |
| `package.dsv` | ament用于描述环境变量修改的中间格式 | 否 | 否 |
| `msg/*.idl`、`srv/*.idl` | 由 `.msg/.srv`转换得到的标准接口描述 | 否 | 否 |
| `msg/*.json`、`srv/*.json` | 生成器输出的类型描述信息 | 否 | 否 |
| `lib/python*/site-packages/common_msgs/...` | PC端Python消息和service类 | 否 | 否 |
| `include/common_msgs/...` | PC端生成的C/C++接口头文件 | 否 | 当前不会从WSL直接拷入MCU |
| `share/common_msgs/cmake/*.cmake` | 让其他PC端CMake包可以 `find_package(common_msgs)` | 否 | 否 |
| `share/ament_index/resource_index/...` | 让 `ros2 pkg`能够发现 `common_msgs` | 否 | 否 |

平时应source工作空间顶层的 `install/setup.bash`或 `install/local_setup.bash`，不需要逐个source `package.bash`。`build/`、`install/`和 `log/`都可以由源包重新生成；真正需要版本管理的是 `Interfaces/common_msgs`中的源文件。

### 3.4 哪些文件只在PC端执行

| 文件或命令 | 作用 | 是否烧录MCU |
|---|---|---|
| `Interfaces/common_msgs/package.xml` | 声明ROS接口包元数据和依赖 | 否 |
| `Interfaces/common_msgs/CMakeLists.txt` | 调用rosidl生成接口代码 | 否 |
| `Interfaces/common_msgs/msg/*.msg` | PC/MCU共同协议的源定义；当前由PC工作空间直接构建 | 否 |
| `Interfaces/common_msgs/srv/*.srv` | service共同协议的源定义；当前由PC工作空间直接构建 | 否 |
| `Interfaces/stage3_pc_test.py` | 订阅两个MCU PUB并提供同步server | 否，只在WSL运行 |
| `micro_ros_agent` | 串口XRCE-DDS到ROS 2/DDS的桥接进程 | 否，只在WSL运行 |
| `ros2 topic/node/service/interface` | 查看、验证和下发消息 | 否，只在WSL运行 |
| `usbipd` | 将Windows USB设备attach给WSL | 否，只在Windows运行 |

需要区分“接口定义同时约束MCU”和“文件本身在MCU执行”：`.msg/.srv`是两端共同协议源，但当前STM32编译器不直接读取它们；MCU实际编译的是对应生成的C头文件、类型支持和静态库。

### 3.5 为什么参考工程没有msg、srv和package.xml

已经检查整个 `D:/InternWork/Code/handheld-umi`参考仓库：没有提交任何 `.msg`、`.srv`或自定义接口包的 `package.xml`。但它包含：

```text
Application/Middleware/Micro-ROS/include/common_msgs/...
Application/Middleware/Micro-ROS/libmicroros.a
```

这些头文件顶部明确写着由 `common_msgs:msg/...idl`生成，说明参考工程保存的是MCU可直接编译的生成产物，而生成它们的原始 `common_msgs`接口仓库位于参考工程之外，或者当时没有一并提交。

所以参考工程存在两个特点：

1. MCU固件可以靠现成头文件和静态库编译，不需要在STM32工程目录里保留 `.msg/.srv`；
2. PC若要识别这些自定义类型，仍必须从别处安装完全一致的 `common_msgs`包，单靠参考MCU工程无法生成PC类型。

本工程把 `Interfaces/common_msgs`纳入仓库，是为了让协议定义、PC构建和后续MCU静态库再生成都有唯一可追溯来源。它不是运行在MCU上的新业务目录，也不会增加MCU Flash/RAM占用。

### 3.6 当前主要文件速查

| 路径 | 用途 |
|---|---|
| `Tasks/micro_ros_task.c/.h` | MCU端ROS实体、连接、重连、PUB/SUB和同步 |
| `Tasks/led_task.c/.h` | MCU端接收六灯最新cmd并保持LED commit唯一所有权 |
| `Tasks/key_task.c` | MCU端本地动作后额外投递按键ROS事件 |
| `Tasks/task_manager.c` | MCU端初始化Queue并直接创建三个任务 |
| `Middleware/Micro-ROS` | MCU端参考静态库、生成头文件、内存/时间/UART DMA适配 |
| `Interfaces/common_msgs` | 共同协议源文件及PC端ROS 2接口包 |
| `Interfaces/stage3_pc_test.py` | PC端状态监听、按键打印和时间同步server |

## 4. 构建结果

| 构建 | Flash | RAM | 结果 |
|---|---:|---:|---|
| Debug | 135348 B / 512 KB（25.82%） | 95768 B / 128 KB（73.07%） | 通过 |
| Release | 114368 B / 512 KB（21.81%） | 95760 B / 128 KB（73.06%） | 通过 |

RAM中包含FreeRTOS 25 KB heap、micro-ROS 25 KB专用heap、2048字节UART RX DMA缓存以及micro XRCE-DDS静态实体缓存。micro-ROS任务初始栈为4096 words；该值沿用参考工程作为首轮上板安全值，完成压力测试后再依据栈高水位收敛。

生成固件：

- Debug：`build/Debug/glove_UMI_APP.elf/.hex/.bin`；
- Release：`build/Release/glove_UMI_APP.elf/.hex/.bin`。

## 5. PC端接口包构建与逐步检查

### 5.1 先说明截图中的问题

截图中的 `Finished <<< common_msgs` 只表示CMake构建步骤结束；紧接着出现 `Unknown package 'common_msgs'`，说明当前shell的ament资源索引中没有找到安装后的 `common_msgs`。此时尚未进入MCU、串口或Agent验证阶段。

本次截图已经确认根因：旧版 `package.xml` 缺少 `<export><build_type>ament_cmake</build_type></export>`，colcon把 `common_msgs` 当成普通CMake包处理。因此构建时出现 `CATKIN_INSTALL_INTO_PREFIX_ROOT` 警告，安装前缀只加入了 `CMAKE_PREFIX_PATH`，没有加入ROS资源检索所需的 `AMENT_PREFIX_PATH`。消息文件虽然已经生成和安装，`ros2 pkg`仍然找不到该包。

当前工程中的 `package.xml` 已补齐build type。必须重新复制接口包并删除旧的 `build/install/log` 后构建，旧install空间不会自动修复。

`Finished <<< common_msgs`只说明各自终端执行过构建，不能说明root和普通用户加载了同一工作空间。当前普通用户已能识别包，后续统一使用 `embedded`；root的 `$HOME`是 `/root`，不需要也不应再为日常联调单独构建一份。

### 5.2 进入WSL并确认ROS 2环境

在Windows PowerShell中进入普通用户WSL：

```powershell
wsl -u embedded
```

在WSL终端中执行：

```bash
source /opt/ros/jazzy/setup.bash
echo "ROS_DISTRO=$ROS_DISTRO"
which ros2
ros2 --help >/dev/null && echo "ROS 2 CLI OK"
```

预期：

```text
ROS_DISTRO=jazzy
/opt/ros/jazzy/bin/ros2
ROS 2 CLI OK
```

若第一条 `source` 报不存在，应先停止本阶段验证并检查ROS 2 Jazzy安装；不要继续构建接口包。

### 5.3 清理旧的PC端生成物并复制最新接口包

以下清理只删除 `~/tacapp_ros2_ws` 内由colcon生成的 `build/install/log` 和之前复制的 `src/common_msgs`，不会删除D盘固件工程：

```bash
rm -rf "$HOME/tacapp_ros2_ws/build" \
       "$HOME/tacapp_ros2_ws/install" \
       "$HOME/tacapp_ros2_ws/log" \
       "$HOME/tacapp_ros2_ws/src/common_msgs"

mkdir -p "$HOME/tacapp_ros2_ws/src"
cp -a /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Interfaces/common_msgs \
      "$HOME/tacapp_ros2_ws/src/common_msgs"
cd "$HOME/tacapp_ros2_ws"
```

确认复制结果完整：

```bash
find src/common_msgs -maxdepth 2 -type f | sort
grep -n '<name>common_msgs</name>' src/common_msgs/package.xml
grep -n -A2 '<export>' src/common_msgs/package.xml
colcon list | grep '^common_msgs[[:space:]]'
```

预期至少看到：

```text
src/common_msgs/CMakeLists.txt
src/common_msgs/package.xml
src/common_msgs/msg/ButtonEvent.msg
src/common_msgs/msg/LedCmd.msg
src/common_msgs/msg/MCUStatus.msg
src/common_msgs/srv/DeviceSynchronization.srv
<export>
  <build_type>ament_cmake</build_type>
</export>
```

`colcon list`必须输出名为 `common_msgs` 的包、路径，并且包类型应为 `ament_cmake`。若显示普通 `cmake` 或没有输出，说明仍在使用旧 `package.xml`，此时不要执行下一步。

### 5.4 重新构建接口包

在同一个WSL终端中执行：

```bash
source /opt/ros/jazzy/setup.bash
cd "$HOME/tacapp_ros2_ws"
colcon build \
  --packages-select common_msgs \
  --event-handlers console_direct+
echo "colcon_exit_code=$?"
```

必须满足：

- 输出包含 `Finished <<< common_msgs`；
- 最后一行是 `colcon_exit_code=0`；
- 修复后的构建不应再出现 `CATKIN_INSTALL_INTO_PREFIX_ROOT` 警告；如果仍出现，说明WSL的 `src/common_msgs/package.xml`不是本工程最新文件；
- 若出现 `Failed <<< common_msgs`，先查看本节末尾的故障信息收集命令。

### 5.5 在source之前检查install空间

本工程使用colcon默认的isolated install布局，因此包安装在 `install/common_msgs`，不是只看顶层 `install` 目录。

```bash
cd "$HOME/tacapp_ros2_ws"

test -f install/common_msgs/share/ament_index/resource_index/packages/common_msgs \
  && echo "ament package index OK" \
  || echo "ERROR: ament package index missing"

find install/common_msgs/share/common_msgs -maxdepth 2 -type f | sort
find install/common_msgs -type f -name '_led_cmd.py' -print
```

预期：

- 第一条检查打印 `ament package index OK`；
- `share/common_msgs/msg`下能看到三个 `.msg`；
- `share/common_msgs/srv`下能看到同步 `.srv`；
- 能找到生成的 `_led_cmd.py`。

若资源索引文件缺失，即使构建日志写着Finished，ROS也会报告Unknown package。请确认已经复制包含最新显式install规则的 `CMakeLists.txt`，然后重新执行5.3和5.4。

### 5.6 加载overlay并检查环境变量

仍在同一个普通用户终端中执行：

```bash
source /opt/ros/jazzy/setup.bash
source "$HOME/tacapp_ros2_ws/install/setup.bash"
export ROS_DOMAIN_ID=9

echo "$AMENT_PREFIX_PATH" | tr ':' '\n'
echo "$CMAKE_PREFIX_PATH" | tr ':' '\n'
ros2 pkg prefix common_msgs
```

预期：

- `AMENT_PREFIX_PATH`包含 `/home/embedded/tacapp_ros2_ws/install/common_msgs`；
- `ros2 pkg prefix common_msgs`输出同一个目录。

如果顶层 `install/setup.bash` 没有加载该包，但5.5中的文件都存在，可以先用下面的临时命令证明问题只在环境前缀：

```bash
export AMENT_PREFIX_PATH="$HOME/tacapp_ros2_ws/install/common_msgs:$AMENT_PREFIX_PATH"
ros2 pkg prefix common_msgs
```

截图已经证明 `install/common_msgs/local_setup.bash`并不存在，所以不要再source该路径。如果临时export有效，说明消息产物本身正确、只是包被错误分类或顶层setup未聚合前缀。该export只用于诊断；随后仍应确认 `package.xml` 的build type并按5.3清理后重建，不要把手工修改 `AMENT_PREFIX_PATH`当作永久方案。

### 5.7 检查四个接口

只有 `ros2 pkg prefix common_msgs` 成功后，才执行：

```bash
ros2 interface show common_msgs/msg/ButtonEvent
ros2 interface show common_msgs/msg/MCUStatus
ros2 interface show common_msgs/msg/LedCmd
ros2 interface show common_msgs/srv/DeviceSynchronization
```

再检查Python类型能否导入：

```bash
python3 -c 'from common_msgs.msg import ButtonEvent, MCUStatus, LedCmd; from common_msgs.srv import DeviceSynchronization; print("common_msgs Python import OK")'
```

预期打印：

```text
common_msgs Python import OK
```

至此只证明PC端消息包正确，尚未证明MCU已经连接。

### 5.8 仍然Unknown package时需要保存的信息

如果严格执行到5.7仍失败，请不要切换root重复构建；在同一个失败终端中执行以下命令并把完整输出发回：

```bash
cd "$HOME/tacapp_ros2_ws"
echo "ROS_DISTRO=$ROS_DISTRO"
echo "AMENT_PREFIX_PATH=$AMENT_PREFIX_PATH"
echo "CMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH"
colcon list
find install -path '*/ament_index/resource_index/packages/common_msgs' -print
find install/common_msgs/share/common_msgs -maxdepth 2 -type f -print
ls -la install install/common_msgs
cat log/latest_build/common_msgs/stdout_stderr.log
```

这组信息可以区分“没有安装资源索引”“顶层setup没有聚合包前缀”“ROS环境被其他发行版覆盖”三类问题。

## 6. 首轮板上验证步骤

进入本节前必须已经通过5.7：PC能够识别四个接口并成功导入Python类型。否则先解决PC接口包，不要同时排查MCU通信。

### 6.1 烧录后先做离线基线

烧录 `build/Release/glove_UMI_APP.bin` 后先不要启动Agent，确认：

1. LED2~LED6完成绿色上电自检并进入绿色常亮；
2. LED7初始熄灭；
3. 单击后准备灯效、蜂鸣和LED7蓝色常亮仍正常；
4. 长按和双击反馈仍正常；
5. PC未连接时MCU没有周期复位。

离线基线异常时先回到板级任务排查，不进入ROS测试。

### 6.2 将CH340连接到WSL

WSL 2默认不能直接使用Windows USB设备。若WSL中已经存在 `/dev/ttyUSB0`，可以直接跳到下一小节；否则按 [Microsoft：连接USB设备](https://learn.microsoft.com/zh-cn/windows/wsl/connect-usb) 的USB/IP流程连接。

保持一个WSL终端处于打开状态。在“管理员PowerShell”中查看设备并对CH340执行一次bind，其中 `<BUSID>`替换为 `usbipd list` 中CH340对应值：

```powershell
usbipd list
usbipd bind --busid <BUSID>
```

随后在普通PowerShell中attach：

```powershell
usbipd attach --wsl --busid <BUSID>
usbipd list
```

在WSL中确认：

```bash
lsusb
ls -l /dev/ttyUSB* 2>/dev/null
ls -l /dev/serial/by-id/* 2>/dev/null
```

优先使用 `/dev/serial/by-id/...`稳定名称；没有该目录时使用实际出现的 `/dev/ttyUSB0`。设备attach到WSL期间不能同时被Windows串口工具占用。

普通用户没有权限时执行：

```bash
sudo usermod -aG dialout "$USER"
```

然后退出全部WSL终端并重新进入，再用 `groups`确认包含 `dialout`。不要长期依赖root运行Agent。

### 6.3 先理解Agent、工作空间和用户目录

#### 6.3.1 Agent是什么

MCU里运行的是micro-ROS Client，PC/WSL里运行的是micro-ROS Agent。两者职责不同：

```text
STM32 micro-ROS Client
        │ USART2 / CH340 / 115200 bit/s
        ▼
WSL中的 micro-ROS Agent
        │ 将Micro XRCE-DDS数据转换并接入ROS 2/DDS
        ▼
PC上的ROS 2节点、ros2命令、stage3_pc_test.py
```

因此，Agent不是 `.msg`文件，也不烧录到MCU。它是PC侧必须持续运行的桥接进程。没有Agent时，MCU本地LED、按键和蜂鸣功能仍应工作，但ROS 2中不会出现 `/mcu_dev`节点及其topic/service。

官方说明见：

- [micro-ROS Agent仓库](https://github.com/micro-ROS/micro-ROS-Agent)
- [micro_ros_setup构建说明](https://github.com/micro-ROS/micro_ros_setup#building-micro-ros-agent)

#### 6.3.2 什么是ROS 2工作空间

工作空间只是开发者自己选定的一个目录，不是系统账户，也不是后台服务。执行一次 `colcon build`后通常包含：

| 目录 | 含义 | 是否手工修改 |
|---|---|---|
| `src/` | ROS 2包的源代码 | 是 |
| `build/` | CMake等构建过程的中间文件 | 否 |
| `install/` | 编译完成后供ROS 2查找和运行的文件 | 否 |
| `log/` | colcon构建日志 | 否 |

本项目建议明确分成三个层次：

| 路径 | 内容 | 所属范围 |
|---|---|---|
| `/opt/ros/jazzy` | 系统安装的ROS 2 Jazzy基础环境 | 所有Linux用户可读 |
| `/home/embedded/microros_jazzy_ws` | PC侧micro-ROS Agent及其构建工具 | 普通用户 `embedded` |
| `/home/embedded/tacapp_ros2_ws` | 本项目 `common_msgs`以及PC测试环境 | 普通用户 `embedded` |

Agent和 `common_msgs`不要求位于同一个工作空间。只要当前终端按正确顺序source两个工作空间，ROS 2就能同时找到它们。

#### 6.3.3 为什么embedded能找到common_msgs而root找不到

`$HOME`会随当前Linux用户变化：

| 当前提示符 | `whoami` | `$HOME` | `$HOME/tacapp_ros2_ws`实际路径 |
|---|---|---|---|
| `embedded@Embedded:...$` | `embedded` | `/home/embedded` | `/home/embedded/tacapp_ros2_ws` |
| `root@Embedded:...#` | `root` | `/root` | `/root/tacapp_ros2_ws` |

因此下面两行看起来一样，实际source的不是同一个目录：

```bash
source "$HOME/tacapp_ros2_ws/install/setup.bash"
```

截图中普通用户能够得到 `/home/embedded/tacapp_ros2_ws/install/common_msgs`，已经证明普通用户工作空间正常。root提示 `Package not found`不是权限不够，而是root的 `$HOME`指向了另一个位置，并未加载普通用户的工作空间。

source只修改当前终端进程的环境变量，不会写入系统，也不会自动传递到另一个终端，更不会从普通用户终端传到root终端。每开一个联调终端都必须重新source。

#### 6.3.4 PC与MCU联调是否需要root

日常编译、source、启动Agent、运行Python脚本和执行 `ros2`命令，全部使用普通用户 `embedded`。不要使用：

```bash
sudo colcon build
sudo ros2 run micro_ros_agent micro_ros_agent ...
sudo python3 stage3_pc_test.py
```

仅以下一次性系统操作需要管理员权限：

| 操作 | 在哪里执行 | 权限 |
|---|---|---|
| `apt install`安装Linux依赖 | WSL普通用户终端 | 命令前临时加 `sudo` |
| 首次 `rosdep init` | WSL普通用户终端 | 命令前临时加 `sudo` |
| 把用户加入 `dialout`串口组 | WSL普通用户终端 | 命令前临时加 `sudo` |
| 首次 `usbipd bind` | Windows PowerShell | 管理员PowerShell |
| 每次 `usbipd attach` | Windows PowerShell | 普通PowerShell即可 |

这样可以避免root和普通用户各生成一套工作空间，也可避免 `build/`、`install/`文件变成root所有而导致后续普通用户无法覆盖。

### 6.4 确认旧Agent位置，或按普通用户安装

#### 6.4.1 先检查当前环境

用普通用户进入WSL，不要执行 `wsl -u root`：

```powershell
wsl -u embedded
```

然后执行：

```bash
whoami
echo "$HOME"
source /opt/ros/jazzy/setup.bash
ros2 pkg prefix micro_ros_agent
```

预期前两行分别为 `embedded`和 `/home/embedded`。如果最后一行打印某个路径，说明Agent已经安装在当前环境可见的位置，记录该路径并跳到6.5。

截图中的 `Package not found`只能证明“当前source链中没有Agent”，不能单凭这一条判定它一定在root工作空间。若要定位以前是否用root构建过，可只做一次只读搜索：

```bash
sudo find /root /home/embedded -type f \
  -path '*/share/ament_index/resource_index/packages/micro_ros_agent' \
  -print 2>/dev/null
```

结果解释：

- 没有输出：在这两个用户目录中没有找到已经安装完成的Agent，按6.4.2安装；
- 输出位于 `/root/.../install/...`：以前确实由root构建；不建议继续复用，按6.4.2给 `embedded`重新构建一份；
- 输出位于 `/home/embedded/<某工作空间>/install/...`：该 `<某工作空间>`就是Agent工作空间，后续source它的 `install/local_setup.bash`；
- 输出只有 `/opt/ros/jazzy/...`：属于系统安装，source Jazzy后应可直接找到，无需单独Agent工作空间。

不要把root工作空间整体 `chmod 777`，也不要为了复用它而一直用root运行Agent。统一由 `embedded`拥有开发文件更容易维护。

本次实测已经在root下找到两套已安装Agent：

```text
/root/micro_ros_ws/install/micro_ros_agent
/root/tacgloveumi/install/micro_ros_agent
```

这证明旧工程确实曾经用root构建过Agent；它不代表新工程也应继续使用root。下面仍为 `embedded`建立独立Agent工作空间，旧目录保持不动，仅作为历史环境保留。

#### 6.4.2 推荐的一次性安装方式

如果没有可用Agent，推荐在独立的普通用户工作空间 `/home/embedded/microros_jazzy_ws`中按Jazzy分支从源码构建。micro-ROS官方列出Jazzy为受支持发行版，并给出了 `create_agent_ws.sh`、`build_agent.sh`流程。

以下步骤只需做一次。仍然使用 `embedded`登录；只有标出 `sudo`的系统安装命令临时提权：

```bash
whoami
# 必须输出embedded

source /opt/ros/jazzy/setup.bash

sudo apt update
sudo apt install -y git python3-rosdep python3-colcon-common-extensions

# rosdep init是整套WSL系统只执行一次；文件已存在时跳过sudo rosdep init
if [ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]; then
  sudo rosdep init
fi
rosdep update

mkdir -p "$HOME/microros_jazzy_ws/src"
cd "$HOME/microros_jazzy_ws"
git clone -b jazzy \
  https://github.com/micro-ROS/micro_ros_setup.git \
  src/micro_ros_setup

rosdep install --from-paths src --ignore-src -y
colcon build --symlink-install
source install/local_setup.bash

ros2 pkg prefix micro_ros_setup
ros2 run micro_ros_setup create_agent_ws.sh
ros2 run micro_ros_setup build_agent.sh

source /opt/ros/jazzy/setup.bash
source "$HOME/microros_jazzy_ws/install/local_setup.bash"
ros2 pkg prefix micro_ros_agent
ros2 run micro_ros_agent micro_ros_agent --help | head
```

最后两条检查成功后，Agent才算安装完成。如果 `git clone`提示目录已存在，不要重复clone；先执行 `ros2 pkg prefix micro_ros_setup`判断已有构建是否可用。若安装过程中出现依赖错误，保存从 `rosdep install`或 `build_agent.sh`第一条error开始的完整输出，不要切到root重新执行整套流程。

#### 6.4.3 本次 `build_agent.sh` 找不到包的定点修复

本次日志的关键不是最后一条 `ros2 pkg prefix`，而是它前面的：

```text
Package 'micro_ros_agent' specified with --packages-up-to was not found
```

官方Jazzy脚本的行为是：

1. `create_agent_ws.sh`把Agent源码导入当前工作空间的 `src/uros/micro-ROS-Agent`；
2. Agent的ROS包清单应位于 `src/uros/micro-ROS-Agent/micro_ros_agent/package.xml`；
3. `build_agent.sh`实际调用 `colcon build --packages-up-to micro_ros_agent ...`。

截图中 `create_agent_ws.sh`在 `./uros/micro-ROS-Agent`位置显示 `Skipped existing directory`，随后colcon又找不到包。最可能的原因是该目标目录在本次导入之前就已存在，但内容不完整；`--skip-existing`看到目录存在后不会重新下载，也不会验证里面是否有有效ROS包。

先在普通用户终端执行以下只读检查：

```bash
cd "$HOME/microros_jazzy_ws"
source /opt/ros/jazzy/setup.bash
source install/local_setup.bash

test -f src/uros/micro-ROS-Agent/micro_ros_agent/package.xml \
  && echo "Agent package.xml OK" \
  || echo "Agent package.xml MISSING"

git -C src/uros/micro-ROS-Agent status --short --branch
colcon list | grep -E '^(micro_ros_agent|micro_ros_msgs|micro_ros_setup)[[:space:]]' \
  || true
find src -name COLCON_IGNORE -o -name AMENT_IGNORE
```

正常情况下，必须至少看到：

```text
Agent package.xml OK
micro_ros_agent  src/uros/micro-ROS-Agent/micro_ros_agent  (ros.ament_cmake)
micro_ros_msgs   src/uros/micro_ros_msgs                    (ros.ament_cmake)
micro_ros_setup  src/micro_ros_setup                       (ros.ament_cmake)
```

如果显示 `Agent package.xml MISSING`，或者该目录不是有效git仓库，执行下面的可恢复修复。原目录只改名备份，不直接删除：

```bash
cd "$HOME/microros_jazzy_ws"
mv src/uros/micro-ROS-Agent \
   src/uros/micro-ROS-Agent.incomplete_20260914

git clone -b jazzy \
  https://github.com/micro-ROS/micro-ROS-Agent.git \
  src/uros/micro-ROS-Agent

test -f src/uros/micro-ROS-Agent/micro_ros_agent/package.xml \
  && echo "Agent source repaired"

source /opt/ros/jazzy/setup.bash
source install/local_setup.bash
rosdep install --from-paths src --ignore-src -y
colcon list | grep '^micro_ros_agent[[:space:]]'
ros2 run micro_ros_setup build_agent.sh

source /opt/ros/jazzy/setup.bash
source "$HOME/microros_jazzy_ws/install/local_setup.bash"
ros2 pkg prefix micro_ros_agent
ros2 run micro_ros_agent micro_ros_agent --help | head
```

必须先看到 `colcon list`中的 `micro_ros_agent`，再运行 `build_agent.sh`；否则构建脚本一定继续报同样错误。如果 `package.xml`存在但 `colcon list`仍看不到包，不要移动源码，先把上述 `find ... COLCON_IGNORE/AMENT_IGNORE`的输出保存下来，因为很可能是父目录存在忽略标记。

#### 6.4.4 本次下载spdlog失败和长时间构建

本次修复后，`colcon list`已经输出：

```text
micro_ros_agent src/uros/micro-ROS-Agent/micro_ros_agent (ros.ament_cmake)
```

这说明“Agent源码包不存在”的问题已经解决。随后 `build_agent.sh`运行33分钟后失败，是另一个独立问题。关键日志为：

```text
Cloning into 'spdlog'...
error: RPC failed; curl 92 HTTP/2 stream ... was not closed cleanly
fatal: early EOF
Failed to clone repository: 'https://github.com/gabime/spdlog.git'
Failed <<< micro_ros_agent [..., exited with code 2]
```

Agent使用CMake superbuild继续从GitHub下载Micro XRCE-DDS Agent及其依赖。首次完整构建本来就比普通ROS包慢，但本次33分钟主要耗在低速网络和三次spdlog重试，不能视为成功构建。由于失败发生在install之前，随后 `ros2 pkg prefix micro_ros_agent`继续显示 `Package not found`是预期结果，并不是source再次失效。

先强制Git使用HTTP/1.1，并用轻量命令验证GitHub链路；这些命令不需要root：

```bash
cd "$HOME/microros_jazzy_ws"
git config --global http.version HTTP/1.1
git config --global --get http.version
git -c http.version=HTTP/1.1 \
  ls-remote https://github.com/gabime/spdlog.git HEAD
```

预期最后输出一个commit hash和 `HEAD`。如果 `ls-remote`都失败，不要立即再等几十分钟构建；先保存下面输出，以便处理WSL代理或网络问题：

```bash
env | grep -iE '^(http|https|all)_proxy=' || true
git config --show-origin --get-regexp 'http\..*|https\..*' || true
```

如果 `ls-remote`成功，直接在原工作空间续建。不需要重新执行 `create_agent_ws.sh`，也不需要再次clone `micro-ROS-Agent`：

```bash
cd "$HOME/microros_jazzy_ws"
source /opt/ros/jazzy/setup.bash
source install/local_setup.bash

colcon list | grep '^micro_ros_agent[[:space:]]'
ros2 run micro_ros_setup build_agent.sh \
  --event-handlers console_direct+
```

colcon会复用已经完成的源码和构建结果，失败的spdlog下载步骤会重新执行。只有看到：

```text
Finished <<< micro_ros_agent
Summary: ... packages finished
```

才继续：

```bash
source /opt/ros/jazzy/setup.bash
source "$HOME/microros_jazzy_ws/install/local_setup.bash"
ros2 pkg prefix micro_ros_agent
ros2 run micro_ros_agent micro_ros_agent --help | head
```

如果HTTP/1.1续建仍连续失败在spdlog，可改用Ubuntu系统spdlog，避免这一项从GitHub下载。eProsima官方提供 `UAGENT_USE_SYSTEM_LOGGER`构建选项：

```bash
sudo apt update
sudo apt install -y libspdlog-dev

cd "$HOME/microros_jazzy_ws"
source /opt/ros/jazzy/setup.bash
source install/local_setup.bash

colcon build --packages-up-to micro_ros_agent \
  --cmake-clean-cache \
  --event-handlers console_direct+ \
  --cmake-args \
    -DUAGENT_BUILD_EXECUTABLE=OFF \
    -DUAGENT_P2P_PROFILE=OFF \
    -DUAGENT_USE_SYSTEM_LOGGER=ON \
    --no-warn-unused-cli
```

系统spdlog方案是网络重试仍失败时的备用路径，不要和第一轮HTTP/1.1续建同时执行。除 `apt install`前的 `sudo`外，构建和运行仍使用普通用户 `embedded`。

### 6.5 准备三个终端的共同环境

本轮使用三个WSL终端，并且三个终端都以普通用户 `embedded`启动：

| 终端 | 用途 |
|---|---|
| A | micro-ROS Agent，持续观察XRCE日志 |
| B | `stage3_pc_test.py`，同步时间并打印两个PUB |
| C | ROS图检查和下发 `LedCmd` |

每个新终端都执行下面完整的四层环境准备，不可只在终端A中source：

```bash
whoami
source /opt/ros/jazzy/setup.bash
source "$HOME/microros_jazzy_ws/install/local_setup.bash"
source "$HOME/tacapp_ros2_ws/install/local_setup.bash"
export ROS_DOMAIN_ID=9

ros2 pkg prefix micro_ros_agent
ros2 pkg prefix common_msgs
echo "ROS_DOMAIN_ID=$ROS_DOMAIN_ID"
```

这里的顺序是“系统ROS 2基础环境 → Agent工作空间 → 项目消息工作空间”。最后source的工作空间优先级最高。若6.4.1确认Agent实际位于另一个普通用户工作空间，只替换第二条工作空间路径，例如：

```bash
source "$HOME/旧Agent工作空间名/install/local_setup.bash"
```

三项预期结果：

```text
/home/embedded/microros_jazzy_ws/install/micro_ros_agent
/home/embedded/tacapp_ros2_ws/install/common_msgs
ROS_DOMAIN_ID=9
```

如果 `micro_ros_agent`仍然找不到，只排查Agent工作空间；如果 `common_msgs`找不到，只排查项目消息工作空间。两者互不替代。此时不要进入root终端，也不要启动MCU链路测试。

### 6.6 终端A：启动Agent并判断连接层级

将串口路径替换成6.2查到的实际值：

```bash
ros2 run micro_ros_agent micro_ros_agent serial \
  --dev /dev/ttyUSB0 \
  -b 115200 \
  -v6
```

保持终端A运行，不要在同一终端继续输入其他测试命令。按日志把问题分层：

| Agent日志现象 | 当前判断 |
|---|---|
| 无任何新日志 | 串口路径、USB attach、供电、波特率或MCU TX需检查 |
| 出现client/session建立 | UART和XRCE基础链路已通 |
| participant/node创建成功 | Client成功进入ROS实体创建 |
| publisher/subscriber/client创建成功 | 阶段3全部实体已创建 |
| 反复建立后立即删除 | 记录完整Agent日志，检查类型或transport错误 |
| deserialization/type错误 | 优先检查PC `common_msgs`是否为本工程最新版本 |

Agent启动顺序不受限制：可以先启动MCU，也可以先启动Agent，固件会持续ping并自动连接。

### 6.7 终端B：启动监听和时间同步server

终端B执行共同环境命令后运行：

```bash
python3 /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Interfaces/stage3_pc_test.py
```

预期顺序：

1. 打印 `answered MCU time synchronization request`；
2. 每秒打印一条 `mcu state=... agent=True uptime=... tx=... rx=...`；
3. 操作按键时打印 `key event=... stamp=...`；
4. 同步完成后stamp不再是 `0.000000000`。

如果只有MCU状态、没有同步日志，终端C执行：

```bash
ros2 service list -t | grep '/mcu_dev/sync'
```

预期类型为 `common_msgs/srv/DeviceSynchronization`。

### 6.8 终端C：检查节点、topic和连接数量

```bash
ros2 node list
ros2 topic list -t
ros2 topic info /mcu_dev/mcu_status -v
ros2 topic info /mcu_dev/key_state -v
ros2 topic info /mcu_dev/led_cmd -v
ros2 topic echo --once /mcu_dev/mcu_status
timeout 15 ros2 topic hz /mcu_dev/mcu_status
```

预期：

- `ros2 node list`包含 `/mcu_dev`；
- `mcu_status`和`key_state`各有1个MCU publisher；
- `led_cmd`有1个MCU subscription；
- `mcu_status`频率接近1 Hz；
- `firmware_version`为 `0.3.0-dev`；
- `agent_connected`为true；
- `uptime_seconds`递增。

如果Agent显示实体创建成功，但 `ros2 node list`看不到 `/mcu_dev`，先确认终端A、B、C的 `ROS_DOMAIN_ID`全部为9。

### 6.9 验证按键PUB

终端B保持运行，或者在终端C单独执行：

```bash
ros2 topic echo /mcu_dev/key_state
```

分别操作并记录：

| 动作 | 预期 `event_type` |
|---|---:|
| 长按 | 1 |
| 单击 | 2 |
| 双击 | 3 |

先各做5次快速检查，再各做100次统计。一个物理动作应只产生一条已经识别完成的事件；断线期间的动作不会在重连后补发。

### 6.10 验证六种单灯模式

先让全部灯熄灭：

```bash
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [0, 0, 0, 0, 0, 0]}"
```

以下命令只测试LED2，其余灯保持灭；每条执行后观察至少2秒：

```bash
# LED2绿色常亮
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [1, 0, 0, 0, 0, 0]}"

# LED2绿色闪烁，半周期250 ms
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [2, 0, 0, 0, 0, 0]}"

# LED2红色常亮
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [3, 0, 0, 0, 0, 0]}"

# LED2蓝色闪烁，半周期250 ms
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [4, 0, 0, 0, 0, 0]}"

# LED2蓝色常亮
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [5, 0, 0, 0, 0, 0]}"
```

如果实际亮的是其他逻辑灯，记录“cmd数组下标→实际LED编号”；这通常是Handler物理映射问题，而不是ROS类型问题。

### 6.11 验证六灯混合cmd和数组顺序

下列cmd预期为：LED2绿常亮、LED3绿闪烁、LED4红常亮、LED5蓝闪烁、LED6蓝常亮、LED7熄灭。

```bash
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [1, 2, 3, 4, 5, 0]}"
```

恢复本项目的常规待机画面：

```bash
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [1, 1, 1, 1, 1, 0]}"
```

### 6.12 验证非法cmd整帧拒绝

先下发一个明显的合法画面并确认显示，然后下发包含值6的非法帧：

```bash
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [0, 0, 0, 0, 0, 6]}"
```

预期六灯保持上一合法帧，不能出现前五颗先熄灭、只有第六颗拒绝的部分更新。终端B下一帧MCU状态中的 `rx`仍会增加，因为该计数表示进入回调的消息数量，不代表cmd执行成功。

### 6.13 验证Agent停止和恢复

1. 记录当前 `uptime`、`tx`、`rx`和灯状态；
2. 在终端A按 `Ctrl+C`停止Agent；
3. 等待至少5秒，操作单击、长按、双击，确认本地LED和蜂鸣器仍工作；
4. 确认MCU没有重新执行上电自检，即没有被复位；
5. 使用完全相同的Agent命令重新启动终端A；
6. 等待 `/mcu_dev`重新出现并再次启动终端B同步server；
7. 重复按键PUB、MCU状态PUB和LED cmd SUB测试；
8. Agent停止/启动重复10次。

Agent断线会退出远程LED控制，因此断线后的灯应恢复本地状态；重连本身不会自动恢复断线前的远程cmd，必须由PC重新下发。

### 6.14 验证USB拔插

1. 先停止Agent，避免它持续占用已消失的设备；
2. 拔掉USB，等待3秒，再重新插入；
3. WSL 2环境可能需要再次执行 `usbipd attach --wsl --busid <BUSID>`；
4. 再次确认 `/dev/ttyUSB*`或 `/dev/serial/by-id/*`，设备编号可能变化；
5. 用新路径启动Agent并重复6.8~6.11；
6. USB拔插重复10次。

### 6.15 8小时长稳

Agent、测试脚本和MCU都稳定后再开始长稳：

```bash
mkdir -p "$HOME/tacapp_ros2_ws/test_logs"
ros2 topic echo /mcu_dev/mcu_status \
  > "$HOME/tacapp_ros2_ws/test_logs/mcu_status_8h.log"
```

长稳期间每30分钟至少操作一次按键并下发一次六灯混合cmd，记录Agent是否重连、消息计数是否继续增长、LED是否出现错误颜色。8小时结束后再读取任务栈高水位和heap最小剩余量；在这些实测完成前，阶段3只能标记为“代码完成、待实机验收”。

## 7. 需要用户回填的验收结果

- `mcu_status`实际频率和是否连续；
- 三类按键各100次的正确数、漏报数、重复数；
- 六种灯模式及六灯混合cmd的肉眼现象；
- 非法cmd是否保持上一帧；
- Agent停止/恢复、USB拔插后的自动重连结果；
- micro-ROS任务栈高水位和两个heap最小剩余量；
- 8小时运行结果及期间的UART/DMA异常。

如果首轮联调只有Agent能建立但ROS实体创建失败，优先核对PC端 `common_msgs` 是否来自本工程、ROS Domain ID是否为9以及PC与参考静态库所用ROS 2发行版是否一致。`LedCmd` 当前使用与生成器线格式等价的本地类型支持；具备完整micro-ROS生成工作区后，可将它并入静态库统一重新生成，再移除该单独类型支持文件。
