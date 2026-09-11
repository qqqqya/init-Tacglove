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

## 3. 主要文件

| 路径 | 用途 |
|---|---|
| `Tasks/micro_ros_task.c/.h` | ROS实体、连接、重连、PUB/SUB和同步 |
| `Tasks/led_task.c/.h` | 接收六灯最新cmd并保持LED commit唯一所有权 |
| `Tasks/key_task.c` | 本地动作后额外投递按键ROS事件 |
| `Tasks/task_manager.c` | 初始化Queue并直接创建三个任务 |
| `Middleware/Micro-ROS` | 参考静态库、头文件、内存/时间/UART DMA适配 |
| `Interfaces/common_msgs` | PC端ROS 2的3个msg和1个srv源包 |
| `Interfaces/stage3_pc_test.py` | 状态监听、按键打印和时间同步server |

## 4. 构建结果

| 构建 | Flash | RAM | 结果 |
|---|---:|---:|---|
| Debug | 135348 B / 512 KB（25.82%） | 95768 B / 128 KB（73.07%） | 通过 |
| Release | 114368 B / 512 KB（21.81%） | 95760 B / 128 KB（73.06%） | 通过 |

RAM中包含FreeRTOS 25 KB heap、micro-ROS 25 KB专用heap、2048字节UART RX DMA缓存以及micro XRCE-DDS静态实体缓存。micro-ROS任务初始栈为4096 words；该值沿用参考工程作为首轮上板安全值，完成压力测试后再依据栈高水位收敛。

生成固件：

- Debug：`build/Debug/glove_UMI_APP.elf/.hex/.bin`；
- Release：`build/Release/glove_UMI_APP.elf/.hex/.bin`。

## 5. PC端准备

以下示例按ROS 2 Jazzy和WSL/Linux编写。将接口包复制到工作空间：

```bash
source /opt/ros/jazzy/setup.bash
mkdir -p ~/tacapp_ros2_ws/src
cp -r /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Interfaces/common_msgs \
      ~/tacapp_ros2_ws/src/
cd ~/tacapp_ros2_ws
colcon build --packages-select common_msgs
source install/setup.bash
export ROS_DOMAIN_ID=9
```

检查接口：

```bash
ros2 interface show common_msgs/msg/ButtonEvent
ros2 interface show common_msgs/msg/MCUStatus
ros2 interface show common_msgs/msg/LedCmd
ros2 interface show common_msgs/srv/DeviceSynchronization
```

## 6. 首轮板上验证步骤

### 6.1 启动Agent

先确认CH340设备名，再启动Agent：

```bash
ls -l /dev/ttyUSB*
source /opt/ros/jazzy/setup.bash
source ~/tacapp_ros2_ws/install/setup.bash
export ROS_DOMAIN_ID=9
ros2 run micro_ros_agent micro_ros_agent serial \
  --dev /dev/ttyUSB0 -b 115200 -v6
```

若普通用户无串口权限，需要把当前用户加入 `dialout` 组并重新登录。

### 6.2 启动监听和同步server

新终端执行：

```bash
source /opt/ros/jazzy/setup.bash
source ~/tacapp_ros2_ws/install/setup.bash
export ROS_DOMAIN_ID=9
python3 /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Interfaces/stage3_pc_test.py
```

预期：脚本打印一次同步响应日志，随后每秒打印MCU状态；按键动作发生时打印事件值。同步完成后事件时间戳不再为0。

### 6.3 检查ROS图

```bash
ros2 node list
ros2 topic list -t
ros2 topic hz /mcu_dev/mcu_status
ros2 topic echo /mcu_dev/key_state
```

预期节点为 `/mcu_dev`，topic至少包含 `/mcu_dev/mcu_status`、`/mcu_dev/key_state` 和 `/mcu_dev/led_cmd`。

### 6.4 下发六灯cmd

下列cmd预期为：LED2绿常亮、LED3绿闪烁、LED4红常亮、LED5蓝闪烁、LED6蓝常亮、LED7熄灭。

```bash
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [1, 2, 3, 4, 5, 0]}"
```

恢复常规待机画面可以下发：

```bash
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [1, 1, 1, 1, 1, 0]}"
```

非法值整帧拒绝测试：先保持一个已知合法画面，再下发：

```bash
ros2 topic pub --once /mcu_dev/led_cmd common_msgs/msg/LedCmd \
  "{header: {frame_id: pc}, led_mode: [0, 0, 0, 0, 0, 6]}"
```

预期六灯保持上一帧，不能出现前五颗先熄灭、只有第六颗拒绝的部分更新。

### 6.5 按键和断线恢复

1. 单击、长按、双击各操作，确认事件值依次对应2、1、3；
2. 停止Agent，确认本地按键、灯和蜂鸣器仍工作，MCU不复位；
3. 重新启动Agent，等待节点自动出现；
4. 再次下发LED cmd并操作按键，确认SUB和PUB都恢复；
5. 连续重复Agent停止/启动和USB拔插至少10次。

## 7. 需要用户回填的验收结果

- `mcu_status`实际频率和是否连续；
- 三类按键各100次的正确数、漏报数、重复数；
- 六种灯模式及六灯混合cmd的肉眼现象；
- 非法cmd是否保持上一帧；
- Agent停止/恢复、USB拔插后的自动重连结果；
- micro-ROS任务栈高水位和两个heap最小剩余量；
- 8小时运行结果及期间的UART/DMA异常。

如果首轮联调只有Agent能建立但ROS实体创建失败，优先核对PC端 `common_msgs` 是否来自本工程、ROS Domain ID是否为9以及PC与参考静态库所用ROS 2发行版是否一致。`LedCmd` 当前使用与生成器线格式等价的本地类型支持；具备完整micro-ROS生成工作区后，可将它并入静态库统一重新生成，再移除该单独类型支持文件。
