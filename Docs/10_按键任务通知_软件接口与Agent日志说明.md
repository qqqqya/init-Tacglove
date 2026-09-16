# 10 按键任务通知、软件接口与Agent日志说明

> 日期：2026-09-16。本文以当前 `Tacapp_init` 源码和本次Agent截图为准。截图内容只作为运行结果分析，不作为操作指令。

## 1. 本轮结论

本轮完成四项调整：

1. 按键识别后的本地LED/蜂鸣器反馈，由自建全局位图改为FreeRTOS Task Notification；
2. `/mcu_dev/key_state` 的消息类型由 `common_msgs/msg/ButtonEvent`正式改为 `common_msgs/msg/KeyState`；
3. 项目公共宏、任务状态、栈/优先级和可共享任务句柄集中到对应 `.h`，函数定义采用 `func(){`格式；
4. Agent日志已经从“只打开串口”推进到“XRCE会话建立、ROS/DDS实体创建和消息转发”，串口基础链路已经打通。

这次改名改变了ROS消息类型。MCU固件和PC `common_msgs`必须同时更新；只更新一端会造成topic类型不匹配。

## 2. 按键到本地反馈改为Task Notification

### 2.1 当前代码链路

```text
PA0低电平
  → key_task每1 ms调用bsp_key_handler_process()
  → Handler完成消抖、长按/单击/双击识别
  → key_task根据key_event_t分两路处理

本地反馈：
  led_task_on_xxx_press()
    → xTaskNotify(g_led_task_handle, 动作位, eSetBits)
    → LED任务xTaskNotifyWait()取出并清除动作位
    → 执行对应LED和蜂鸣器反馈

ROS上报：
  micro_ros_task_enqueue_key_event(event)
    → uint8事件写入8元素Queue
    → micro_ros_task取出
    → rcl_publish(KeyState)
```

`task_manager_init()`创建LED任务时保存任务句柄：

```c
TaskHandle_t g_led_task_handle;

xTaskCreate(led_task_entry,
            "led",
            LED_TASK_STACK_WORDS,
            NULL,
            LED_TASK_PRIORITY,
            &g_led_task_handle);
```

三个原有公开函数继续保留，因此 `key_task`调用关系没有变；函数内部实现已经改为RTOS通知：

```c
void led_task_on_long_press(void){
    led_task_notify_action(LED_TASK_NOTIFY_LONG_PRESS);
}

static void led_task_notify_action(uint32_t action){
    if ((!s_led_ready) || (NULL == g_led_task_handle))
    {
        return;
    }

    (void)xTaskNotify(g_led_task_handle, action, eSetBits);
}
```

LED任务每轮非阻塞获取通知位：

```c
uint32_t actions = 0U;
(void)xTaskNotifyWait(0U,
                      LED_TASK_NOTIFY_ALL,
                      &actions,
                      0U);
```

这里使用0 tick等待，是因为同一LED任务还要处理远程 `LedCmd` Queue和250 ms闪烁相位，不能只阻塞等待按键。任务末尾仍有1 ms周期延时，所以通知处理延迟通常不超过一个任务周期；如果未来把灯效拆成独立定时状态机，可再评估带超时阻塞等待。

当前 `Middleware/FreeRTOS/FreeRTOSConfig.h`已经设置 `configUSE_TASK_NOTIFICATIONS=1`、`configTASK_NOTIFICATION_ARRAY_ENTRIES=1`，因此使用默认通知槽0，不需要修改FreeRTOS配置。

### 2.2 修改前后两种本地反馈方法的代码差异

| 对比项 | 修改前：共享位图+临界区 | 修改后：FreeRTOS Task Notification |
|---|---|---|
| 数据存放 | `static volatile uint32_t s_pending_actions` | LED任务TCB内部的通知值 |
| 发送 | `taskENTER_CRITICAL(); s_pending_actions |= bit; ...` | `xTaskNotify(handle, bit, eSetBits)` |
| 接收 | 自己进入临界区，复制后清零全局位图 | `xTaskNotifyWait()`由内核原子读取并清位 |
| 任务归属 | 普通全局变量，代码约定谁能读写 | 通知值天然属于目标任务 |
| 唤醒能力 | 不能直接唤醒阻塞任务 | 可以唤醒正在等待通知的任务 |
| 额外对象 | 不创建RTOS对象，但要维护共享变量和临界区 | 不创建Queue/Semaphore，使用每个任务已有通知槽 |
| 重复事件 | 同一位处理前重复设置会合并 | `eSetBits`同样会合并 |
| 顺序和计数 | 不保存先后顺序或重复次数 | `eSetBits`也不保存先后顺序或重复次数 |
| 适合场景 | 简单但容易自行实现出竞态 | 单接收任务、少量动作位、允许合并的快速通知 |

因此本地反馈采用Notification；ROS上报仍采用Queue。ROS侧必须按顺序保存多个按键事件，也要保存同一事件的重复发生，不能用 `eSetBits`代替Queue。

### 2.3 当前通知位

```c
LED_TASK_NOTIFY_SHORT_PRESS  = 1 << 0
LED_TASK_NOTIFY_LONG_PRESS   = 1 << 1
LED_TASK_NOTIFY_DOUBLE_CLICK = 1 << 2
```

这些是MCU内部任务通信位，不属于ROS协议，不交给上位机团队。

## 3. 为什么topic和message类型必须匹配

ROS 2通信匹配至少包含topic名称、消息类型以及兼容QoS。`/mcu_dev/key_state`并不在语法上强制类型也叫 `KeyState`，但本项目采用同名语义便于维护：

```text
topic：/mcu_dev/key_state
type： common_msgs/msg/KeyState
source：Interfaces/common_msgs/msg/KeyState.msg
```

更重要的是，PC和MCU必须都使用 `KeyState`。即使 `ButtonEvent`与 `KeyState`字段完全相同，它们仍是两个不同的ROS/DDS类型，不能把一端只改文件名、另一端继续使用旧类型。

当前同步修改的位置包括：

- PC接口源：`Interfaces/common_msgs/msg/KeyState.msg`；
- PC生成配置：`Interfaces/common_msgs/CMakeLists.txt`；
- PC测试程序：`Interfaces/stage3_pc_test.py`；
- MCU结构头：`Middleware/Micro-ROS/include/common_msgs/msg/key_state.h`及detail头；
- MCU CDR类型支持：`key_state_type_support.c`；
- MCU Publisher创建和消息对象：`Tasks/micro_ros_task.c`。

旧 `ButtonEvent.msg`已经从正式接口源包移除。参考静态库中残留的旧 `ButtonEvent`生成头/目标文件不再被当前业务代码引用；下一次按本项目接口统一重新生成 `libmicroros.a + include`后会自然收敛。

## 4. 交给软件团队的正式接口定义

### 4.1 交付文件

软件团队应直接取得整个目录：

```text
Interfaces/common_msgs/
├── CMakeLists.txt
├── package.xml
├── msg/
│   ├── KeyState.msg
│   ├── MCUStatus.msg
│   └── LedCmd.msg
└── srv/
    └── DeviceSynchronization.srv
```

`Interfaces/stage3_pc_test.py`可作为订阅和时间同步Server示例一起提供，但它不是接口定义本身。不要把MCU的 `libmicroros.a`、手工临时类型支持或 `build/`目录交给PC团队作为消息源。

### 4.2 通用配置

| 项目 | 当前约定 |
|---|---|
| ROS 2发行版 | Jazzy |
| ROS Domain ID | `9` |
| 开发期MCU节点名 | `mcu_dev` |
| topic/service前缀 | `/mcu_dev` |
| MCU-Agent物理链路 | USART2/CH340，115200 bit/s，8N1 |
| topic QoS | 当前均使用rclc default，即Reliable |
| 时间戳 | 同步前为0；同步成功后使用PC返回的ROS时间基准 |

USART2只连接MCU与Agent。PC业务Python/C++节点通过ROS图通信，不直接打开CH340串口。

### 4.3 MCU PUB：按键状态

```text
Topic: /mcu_dev/key_state
Type:  common_msgs/msg/KeyState
方向:  MCU Publisher → PC Subscriber
触发:  一个按键动作完成识别时发布一次
```

```text
uint8 EVENT_LONG_PRESS=1
uint8 EVENT_REC_TOGGLE=2
uint8 EVENT_DOUBLE_CLICK=3
uint8 EVENT_ERROR_ACK=4

std_msgs/Header header
uint8 event_type
```

当前板实际产生1、2、3；4为故障确认预留。它表示经过消抖和状态机确认后的离散动作，不是PA0实时高低电平。

### 4.4 MCU PUB：MCU状态

```text
Topic: /mcu_dev/mcu_status
Type:  common_msgs/msg/MCUStatus
方向:  MCU Publisher → PC Subscriber
周期:  1 Hz
```

```text
uint8 STATE_IDLE=0
uint8 STATE_READY=1
uint8 STATE_ERROR=2
uint8 STATE_CALIBRATING=3
uint8 STATE_UPDATING=4

std_msgs/Header header
string firmware_version
uint32 uptime_seconds
uint8 system_state
bool agent_connected
uint32 message_tx_count
uint32 message_rx_count
```

当前MCU内部状态映射：

| MCU内部状态 | `system_state` |
|---|---:|
| 上电自检、采集准备 | `STATE_CALIBRATING=3` |
| 待机 | `STATE_IDLE=0` |
| 采集中 | `STATE_READY=1` |
| LED/蜂鸣器故障 | `STATE_ERROR=2` |
| IAP升级预留 | `STATE_UPDATING=4` |

`message_tx_count`统计成功发布的按键和MCU状态消息；`message_rx_count`在进入 `LedCmd`回调时增加，当前不等同于“cmd已通过参数校验并执行”的ACK。

### 4.5 MCU SUB：LED和蜂鸣器cmd

```text
Topic: /mcu_dev/led_cmd
Type:  common_msgs/msg/LedCmd
方向:  PC Publisher → MCU Subscriber
触发:  PC按需发布
```

关键字段：

```text
std_msgs/Header header
uint8[6] led_mode
uint8 beep_mode
```

`led_mode`顺序固定：

| 下标 | 逻辑灯 |
|---:|---|
| 0 | LED2 / 相机1 |
| 1 | LED3 / 相机2 |
| 2 | LED4 / 相机3 |
| 3 | LED5 / 相机4 |
| 4 | LED6 / 相机5 |
| 5 | LED7 / 系统状态 |

鱼眼相机没有独立指示灯。每个元素模式：0灭、1绿常亮、2绿闪烁、3红常亮、4蓝闪烁、5蓝常亮。闪烁半周期250 ms。

蜂鸣器模式：0关闭、1短鸣200 ms、2长鸣600 ms、3鸣叫150 ms两次且中间静音250 ms。

任一灯模式大于5或蜂鸣器模式大于3，整条cmd拒绝，不能先执行部分字段。合法新cmd覆盖尚未执行的旧cmd。当前没有LED状态PUB，也没有逐cmd ACK。

### 4.6 时间同步srv

```text
Service: /mcu_dev/sync
Type:    common_msgs/srv/DeviceSynchronization
MCU角色: Client
PC角色:  Server
```

请求：

```text
bool sync_request
```

响应：

```text
std_msgs/Header header
bool sync_state
```

MCU发送 `sync_request=true`；PC将当前ROS时间写入 `header.stamp`并返回 `sync_state=true`。这不是PC主动调用MCU服务。

## 5. PC工作空间在类型改名后必须重建

原工作空间可能保留生成过的 `ButtonEvent`，不能只覆盖文件后直接source旧install。使用普通用户 `embedded`，先备份旧的包级目录，再复制并重建：

```bash
source /opt/ros/jazzy/setup.bash
cd "$HOME/tacapp_ros2_ws"

test ! -d src/common_msgs || \
  mv src/common_msgs "$HOME/common_msgs_src_before_key_state"
test ! -d build/common_msgs || \
  mv build/common_msgs "$HOME/common_msgs_build_before_key_state"
test ! -d install/common_msgs || \
  mv install/common_msgs "$HOME/common_msgs_install_before_key_state"

cp -a /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Interfaces/common_msgs \
  "$HOME/tacapp_ros2_ws/src/"

colcon build --packages-select common_msgs \
  --event-handlers console_direct+
source install/local_setup.bash
export ROS_DOMAIN_ID=9
```

检查：

```bash
ros2 pkg prefix common_msgs
ros2 interface show common_msgs/msg/KeyState
ros2 interface show common_msgs/msg/MCUStatus
ros2 interface show common_msgs/msg/LedCmd
ros2 interface show common_msgs/srv/DeviceSynchronization

python3 -c 'from common_msgs.msg import KeyState, MCUStatus, LedCmd; from common_msgs.srv import DeviceSynchronization; print("interface import OK")'
```

旧 `ButtonEvent`即使还能从另一个被source的旧overlay中找到，也不能再用于 `/mcu_dev/key_state`。

## 6. 本次Agent日志逐层解释

### 6.1 这次已经不是“卡住”

第二次启动后截图出现：

```text
create_client       | create
establish_session   | session established
create_participant  | participant created
create_topic        | topic created
create_publisher    | publisher created
create_datawriter   | datawriter created
create_subscriber   | subscriber created
create_datareader   | datareader created
```

这说明MCU已经发出了有效XRCE帧，Agent已经回应，并代表MCU在ROS/DDS侧创建实体。前一次只有 `running`和 `logger setup`，随后出现 `^C`，是当时还没有建立会话且被手动停止；第二次日志证明原来的“没有XRCE握手”问题已经消失。

micro-ROS Agent的职责就是在MCU侧XRCE-DDS与PC侧ROS 2/DDS之间代理实体和数据，不是业务消息本身。[micro-ROS Agent官方说明](https://github.com/micro-ROS/micro-ROS-Agent)

### 6.2 每类日志的含义

| 日志 | 层级 | 含义 |
|---|---|---|
| `TermiosAgentLinux.cpp init running fd:30` | Linux串口 | `/dev/ttyUSB0`成功打开，30是本进程文件描述符 |
| `set_verbose_level ... 6` | Agent日志 | 已启用最高细节的排障输出 |
| `create_client` | XRCE Client | Agent识别到一个MCU XRCE Client |
| `session established` | XRCE Session | 双向握手成功，串口收发和基本帧格式都有效 |
| `participant created` | DDS | 为MCU创建DDS Participant，可理解为ROS实体的DDS根对象 |
| `topic created` | DDS | 创建一个topic描述，尚不能单独说明方向 |
| `publisher created` + `datawriter created` | MCU PUB | MCU Publisher对应的DDS写端创建成功 |
| `subscriber created` + `datareader created` | MCU SUB | MCU Subscription对应的DDS读端创建成功 |
| `requester created` | Service | MCU同步Client对应的请求/响应实体创建成功；若截图未截到，应继续向后看或用service list确认 |
| `recv_message` | 串口→Agent | Agent从MCU收到XRCE帧 |
| `send_message` | Agent→串口 | Agent向MCU发送确认、创建响应或PC下行数据 |
| `DataWriter.cpp write <<DDS>>` | Agent→DDS | MCU上行样本已由Agent写入PC的DDS数据空间 |

截图中出现两组 `publisher + datawriter`，与当前固件的 `key_state`、`mcu_status`两个Publisher数量相符；出现一组 `subscriber + datareader`，与 `led_cmd`一个Subscription相符。具体某个object ID对应哪个topic，排障时以 `ros2 topic list -t`为准，不靠十六进制肉眼猜测。

### 6.3 字段怎么看

- `client_key`：Agent区分MCU Client的键值，同一会话中的日志应保持一致；MCU重启后可能变化；
- `session_id: 0x81`：XRCE会话编号和可靠会话属性，不是ROS Domain ID；
- `address: 0`：串行传输下的Agent内部客户端地址，不是IP地址；
- `participant_id/topic_id/publisher_id/...`：XRCE对象ID，只用于会话内关联；
- `len`：该次串口或DDS操作的数据长度；
- `0000:`、`0020:`：原始XRCE/CDR十六进制数据，包含协议头、对象创建描述或序列化payload。

一般不需要人工解码十六进制。只有遇到Agent能建实体但业务数据反序列化失败、长度异常或单向不通时，才结合抓包、消息IDL和CDR对齐规则分析。

### 6.4 Agent日志能证明什么、不能证明什么

已经证明：

- `/dev/ttyUSB0`可打开；
- 115200配置下MCU与Agent能够双向交换有效XRCE帧；
- XRCE session能够建立；
- MCU请求的Participant、Publisher和Subscriber能够创建；
- 至少已经出现MCU上行DDS写入。

还不能仅靠截图证明：

- PC `common_msgs`是否已经更新为 `KeyState`；
- 每个topic的类型是否与PC端完全一致；
- `LedCmd`回调是否执行并驱动灯/蜂鸣器；
- 时间同步Requester/响应是否完成；
- 断线重连和8小时稳定性是否通过。

### 6.5 日常日志级别

`-v6`适合首轮排障，但每个串口帧都会输出十六进制，信息量很大，也会明显增加终端输出。功能稳定后建议降低到 `-v4`观察info级实体和连接事件；出现单向数据、创建失败或掉线问题时再临时切回 `-v6`。具体选项以本机 `micro_ros_agent --help`为准。

## 7. 改名后完整验证顺序

1. Debug和Release重新构建MCU固件并烧录；
2. 按第5章重建PC `common_msgs`；
3. 终端A启动Agent；
4. 看到 `session established`和实体created；
5. 终端B启动 `stage3_pc_test.py`，为MCU提供同步Server；
6. 终端C检查ROS图和消息。

```bash
source /opt/ros/jazzy/setup.bash
source "$HOME/microros_jazzy_ws/install/local_setup.bash"
source "$HOME/tacapp_ros2_ws/install/local_setup.bash"
export ROS_DOMAIN_ID=9

ros2 node list
ros2 topic list -t
ros2 service list -t
ros2 topic type /mcu_dev/key_state
ros2 topic echo /mcu_dev/key_state common_msgs/msg/KeyState
```

按键后预期 `event_type`分别为长按1、单击2、双击3。本地LED/蜂鸣器反馈由Task Notification触发，ROS `KeyState`由Queue独立上报；两条链路应同时正常。

蜂鸣器cmd示例：

```bash
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [0, 0, 0, 0, 0, 0], beep_mode: 1}"
```

## 8. 代码规范落地位置

- `task_status_t`、任务栈/优先级、`g_led_task_handle`集中到 `Tasks/task_manager.h`；
- 删除独立 `Tasks/task_status.h`；
- `led_task.c`原宏移动到 `led_task.h`；
- `bsp_led_driver.c`原SK6805/PWM DMA宏移动到 `bsp_led_driver.h`；
- 按键时序宏移动到 `bsp_key_handler.h`；
- micro-ROS配置宏和连接状态类型移动到 `micro_ros_task.h`；
- 本工程自写的 `Tasks/BSP`函数定义逐步统一为 `func(){`；
- 文件私有缓存和运行状态继续保留为 `.c`中的 `static`，不在头文件暴露可写实现细节；
- 完整规则见 `Docs/C_CODE_STYLE_GUIDE.md`。
