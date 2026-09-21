# 08 阶段3 micro-ROS消息接口设计

## 1. 本版结论

本工程阶段3按照 `handheld-umi/Application` 的micro-ROS接口组织方式收敛为：

```text
MCU PUB：MCU状态
MCU PUB：按键状态/事件
MCU SUB：LED控制cmd
MCU Client：时间同步srv
```

本阶段不发布LED状态，也不增加LED状态查询srv。需要生成的项目接口只有3个msg和1个srv：

```text
common_msgs/
├── msg/
│   ├── KeyState.msg
│   ├── MCUStatus.msg
│   └── LedCmd.msg
└── srv/
    └── DeviceSynchronization.srv
```

## 2. 与参考工程的对应关系

| `handheld-umi`参考接口 | 本工程处理 | 说明 |
|---|---|---|
| `button_device_state` PUB | 保留为 `key_state` PUB | 本工程只有一个数采按键 |
| `button_remote_ctrl` PUB | 删除 | 本工程没有第二个按键 |
| `encoder_state` PUB | 删除 | 当前阶段没有编码器数据 |
| `mcu_status` PUB | 保留 | 周期上报固件、运行和通信状态 |
| `DeviceCommand` SUB | 改为 `LedCmd` SUB | 保留六颗LED和蜂鸣器控制，不包含encoder零点复位 |
| `DeviceSynchronization` Client | 保留 | MCU向PC请求时间同步 |
| LED状态 PUB | 不增加 | 当前需求不需要MCU回报LED状态 |
| 状态查询srv | 不增加 | 当前只保留参考工程已有的时间同步srv |

参考工程的 `DeviceCommand` 同时包含LED、蜂鸣器和encoder复位。本工程使用固定六灯数组并保留蜂鸣器字段，不照搬当前硬件没有的encoder复位字段。所有接口标识中的 `Command/command` 均改用 `Cmd/cmd`，例如 `LedCmd`、`led_cmd`、`led_cmd_sub`。

## 3. 节点和接口名称

沿用参考工程的SN隔离方式：

- 节点名：`mcu_<device_id>`；
- 开发阶段：`mcu_dev`；
- 正式阶段：从SN派生，将 `-` 转换为 `_`；
- Domain ID：暂按参考工程的9，PC端和MCU端必须一致。

| MCU角色 | topic/srv | 类型 | 触发方式 |
|---|---|---|---|
| Publisher | `/mcu_<id>/mcu_status` | `common_msgs/msg/MCUStatus` | 1 Hz周期发布 |
| Publisher | `/mcu_<id>/key_state` | `common_msgs/msg/KeyState` | 短按、长按或双击确认后发布 |
| Subscription | `/mcu_<id>/led_cmd` | `common_msgs/msg/LedCmd` | PC按需下发 |
| Service Client | `/mcu_<id>/sync` | `common_msgs/srv/DeviceSynchronization` | 建立连接后请求时间同步 |

三条topic初版都使用参考工程的default QoS，即Reliable。若串口压力测试证明周期状态使用Reliable会造成明显阻塞，再单独评估把 `mcu_status` 调整为Best Effort；第一版不提前分叉。

## 4. MCU上报：`KeyState.msg`

### 4.1 定义

参考工程使用 `ButtonEvent` 上报已经识别完成的按键动作。本工程保留其字段和常量语义，但将消息类型改名为与 `/key_state` topic一致的 `KeyState`。本地 `KEY_EVENT_SHORT_PRESS=2` 在ROS侧对应 `EVENT_REC_TOGGLE=2`。

```text
uint8 EVENT_LONG_PRESS=1
uint8 EVENT_REC_TOGGLE=2
uint8 EVENT_DOUBLE_CLICK=3
uint8 EVENT_ERROR_ACK=4

std_msgs/Header header
uint8 event_type
```

当前板只产生1、2、3三种事件，值4仅为保持参考消息定义兼容而保留，阶段3不会主动发布。

### 4.2 发布过程

```text
PA11物理电平
    ↓
bsp_key_handler_process()
    ↓
KEY_EVENT_LONG_PRESS / SHORT_PRESS / DOUBLE_CLICK
    ├──> xTaskNotify(eSetBits)通知LED任务执行本地灯和蜂鸣器反馈
    └──> 写入按键事件Queue
                 ↓
          micro_ros_task
                 ↓ rcl_publish()
       /mcu_<id>/key_state
                 ↓
                 PC
```

处理规则：

- key_task继续保持1 ms扫描，不直接调用 `rcl_publish()`；
- 按键Queue只传已经识别完成的事件，不持续发布PA11高低电平；
- `micro_ros_task`从Queue取出事件，填写时间戳后发布；
- 尚未完成时间同步时，`header.stamp` 置0；
- Agent未连接时保留本地按键功能，不在重连后补发已经过期的按键事件；
- Queue满或断线时不回放过期事件；该情况作为内部错误计数记录，暂不扩展msg字段。

## 5. MCU上报：`MCUStatus.msg`

### 5.1 定义

`MCUStatus` 的字段和常量保持 `handheld-umi` 参考定义不变：

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

本工程内部状态到该公共消息的映射为：上电自检/采集准备→`STATE_CALIBRATING`，待机→`STATE_IDLE`，采集中→`STATE_READY`，故障→`STATE_ERROR`，IAP→`STATE_UPDATING`。这样不修改公共类型即可表达当前阶段的大状态。

### 5.2 字段来源

| 字段 | MCU数据来源 |
|---|---|
| `header.stamp` | micro-ROS时间同步结果；未同步时为0 |
| `firmware_version` | 固件版本宏绑定的固定缓冲区 |
| `uptime_seconds` | FreeRTOS tick换算 |
| `system_state` | 当前LED/按键演示状态，后续由正式状态机接管 |
| `agent_connected` | micro-ROS连接状态机 |
| `message_tx_count` | 成功publish的消息总数 |
| `message_rx_count` | 收到并进入回调的 `LedCmd` 总数 |

### 5.3 发布过程

```text
FreeRTOS tick + 固件版本 + 系统状态 + 通信计数
                         ↓
                  MCUStatus消息
                         ↓ 每1 s
                  micro_ros_task
                         ↓ rcl_publish()
             /mcu_<id>/mcu_status
                         ↓
                         PC
```

消息对象和 `firmware_version` 缓冲在初始化阶段一次性建立，周期发布时只更新字段，不能每秒动态申请和释放字符串内存。

## 6. PC下发：`LedCmd.msg`

### 6.1 为什么合并颜色和效果

旧草案同时定义了 `COLOR_OFF=0` 和 `EFFECT_OFF=0`，并要求PC同时填写 `color[6]` 与 `effect[6]`。对于当前只有灭、常亮和闪烁几种固定灯效的项目，这确实冗余，而且可能产生“颜色为OFF但效果为BLINK”这类矛盾组合。

本版改成与参考工程 `led_mode` 相同的单一模式枚举。颜色和效果合并到一个值中，不再分别传 `color` 与 `effect`。

### 6.2 定义

```text
uint8 MODE_OFF=0
uint8 MODE_GREEN_SOLID=1
uint8 MODE_GREEN_BLINK=2
uint8 MODE_RED_SOLID=3
uint8 MODE_BLUE_BLINK=4
uint8 MODE_BLUE_SOLID=5

uint8 MODE_BEEP_OFF=0
uint8 MODE_SHORT_BEEP=1
uint8 MODE_LONG_BEEP=2
uint8 MODE_BEEPING=3

std_msgs/Header header
uint8[6] led_mode
uint8 beep_mode
```

模式编号0~5沿用参考工程主要LED模式的顺序；修正参考工程 `GREEN_BLIK` 的拼写为 `GREEN_BLINK`，并删除当前需求中已经取消的黄色模式。

### 6.3 六灯下标

ROS消息使用业务顺序，BSP Handler继续负责业务灯号到物理串联序号的转换：

| `led_mode`下标 | 逻辑灯 | 含义 |
|---:|---|---|
| 0 | LED2 | 相机1 |
| 1 | LED3 | 相机2 |
| 2 | LED4 | 相机3 |
| 3 | LED5 | 相机4 |
| 4 | LED6 | 相机5 |
| 5 | LED7 | 系统状态 |

鱼眼相机没有指示灯，不进入数组。

### 6.4 接收过程

```text
PC
 ↓ publish LedCmd
/mcu_<id>/led_cmd
 ↓
micro_ros_task订阅回调
 ↓ 检查6个led_mode和beep_mode是否合法
LED/beep cmd Queue
 ↓
led_task
 ↓
BSP LED Handler
 ↓
TIM3_CH3 PWM + DMA
```

处理规则：

- `LedCmd`使用固定6元素数组，不使用动态sequence；
- 任一 `led_mode` 大于 `MODE_BLUE_SOLID` 时，整条cmd拒绝执行；
- `beep_mode`大于 `MODE_BEEPING` 时，整条cmd拒绝执行；
- `MODE_BEEP_OFF`静音，`MODE_SHORT_BEEP`鸣叫200 ms，`MODE_LONG_BEEP`鸣叫600 ms，`MODE_BEEPING`沿用参考工程的两次150 ms鸣叫及250 ms间隔；
- 回调只校验并入队，不直接调用LED Driver；
- led_task是唯一提交PWM DMA的任务；
- 实际亮度不从ROS消息下发，继续使用MCU端统一亮度值，目前为1；
- 本阶段没有 `LedStatus` publisher，因此MCU不会为每条cmd额外发送LED确认消息；PC通过后续的 `MCUStatus.system_state` 观察系统大状态，但它不是逐灯回执。

## 7. 时间同步：`DeviceSynchronization.srv`

保持参考工程定义不变：

```text
bool sync_request
---
std_msgs/Header header
bool sync_state
```

本接口中MCU是service client，PC是service server：

1. MCU连接Agent并创建ROS实体；
2. MCU向 `/mcu_<id>/sync` 发送 `sync_request=true`；
3. PC在response的 `header.stamp` 中返回当前ROS时间；
4. MCU在 `sync_state=true` 时更新本地时间基准；
5. 同步失败不影响按键扫描和本地LED功能。

## 8. micro-ROS实体和Executor

第一版需要创建的实体为：

```text
Publishers：
  1. key_state_pub
  2. mcu_status_pub

Subscription：
  1. led_cmd_sub

Service client：
  1. sync_client
```

与参考工程相同，publisher不占用Executor handle。Executor只处理一个subscription和一个service client，因此初版容量为2：

```c
rclc_executor_init(&executor, &support.context, 2, &allocator);
```

建议变量名统一为：

```text
key_state_pub
mcu_status_pub
led_cmd_sub
key_state_msg
mcu_status_msg
current_led_cmd
```

不再使用 `DeviceCommand`、`current_device_command`、`topic_command` 等标识。

## 9. 三条消息链路总图

```text
                         ┌──────────────────────────────┐
PA11 -> key_task -> Queue -> key_state PUB             │
                         │                              │
系统/连接/计数 ----------> mcu_status PUB               ├── USART2/CH340 -> PC
                         │                              │
LED2~LED7 <- led_task <- Queue <- led_cmd SUB <---------┘
```

这三条链路分别解决：

- `key_state`：MCU发生了什么按键动作；
- `mcu_status`：MCU当前是否正常、已运行多久、是否连接Agent；
- `led_cmd`：PC要求六颗LED显示什么模式，并可同时测试蜂鸣器。

## 10. 首轮验收

1. PC能发现 `mcu_status`、`key_state` 和 `led_cmd` 三条topic以及 `sync` srv。
2. `mcu_status`稳定1 Hz发布，运行时间和收发计数持续更新。
3. 短按、长按、双击各测试100次，`key_state.event_type`与实际动作一致。
4. PC可通过一个 `LedCmd` 同时设置LED2~LED7的六个模式和蜂鸣器模式。
5. 非法模式不会导致部分灯先更新，也不会破坏后续合法cmd。
6. 停止Agent或拔插USB时MCU不死机，本地按键、灯和蜂鸣器继续工作；恢复后ROS实体自动重建。
7. 连续运行8小时无不可恢复断线，并记录任务栈高水位、最小heap、UART/DMA错误和消息计数。

## 11. 已采用的开发期配置

- Domain ID沿用参考工程的9；
- 节点固定为 `mcu_dev`，本阶段按单板联调；
- PC端类型源包放在工程 `Interfaces/common_msgs`；
- `MCUStatus`、`DeviceSynchronization`保持参考工程类型不变；
- 按键消息由参考 `ButtonEvent`更名为与topic一致的 `KeyState`，字段和事件值不变；
- `KeyState`和 `LedCmd`的临时类型支持已经并入MCU工程，统一静态库重新生成后删除临时实现；
- 正式多板命名和SN派生留到Bootloader/SN阶段，不在当前开发固件中提前引入。
