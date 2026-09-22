# Bootloader跳转、Agent启动与SN主题说明

## 1. 本次结论

本次按当前实板现象重新核对了启动链路。现象是：

1. Bootloader已经收到跳转命令；
2. Agent只显示 `running` 和 `logger setup`；
3. 进入Application调试并单击全速运行后，Agent才出现会话和Topic创建日志。

这说明Agent本身能够运行，USART2和Application中的micro-ROS代码也至少在“调试器直接启动APP”
这条路径上能够工作。异常集中在 **Bootloader到Application的交接过程**，而不是Agent必须依赖
调试器。

本次完成两类修改：

- 把Bootloader启动策略简化为一个0/1开关，不再使用按键、SN和Debug/Release组合判断；
- 修正跳转实现，保证设置APP栈指针后不再执行依赖Bootloader旧栈的C语句。

## 2. Agent为什么只显示两行

Agent刚启动时的两行：

```text
running...
logger setup
```

只表示：

- Agent进程已经启动；
- `/dev/ttyUSB0`已经被Agent打开；
- 日志等级已经设定。

它们不表示MCU已经建立micro-ROS会话。MCU发出有效的XRCE-DDS帧后，`-v6`日志还应继续出现：

```text
create_client
session established
participant created
topic created
publisher created / subscriber created
datawriter created / datareader created
```

只有前两行，含义就是“PC端在等，但串口上没有收到有效的micro-ROS Client帧”。

### 2.1 为什么进入APP Debug再全速运行就正常

用Application工程启动调试时，调试器通常会：

1. 直接装载Application ELF；
2. 把PC指向Application复位入口或 `main()`；
3. 在入口处暂停；
4. 单击Resume/全速运行后才继续。

这条路径绕过了Bootloader的跳转代码。因此“Debug后正常”只能证明APP本身可运行，不能证明
Bootloader跳APP成功。

原跳转函数存在两个风险：

1. 在普通C函数中执行 `__set_MSP(app_stack_pointer)` 后，又执行 `__DSB()`、`__ISB()`、
   `__enable_irq()` 和C函数调用。MSP已经换成APP栈，但编译器仍可能按Bootloader原C栈帧取值或
   恢复寄存器；不同优化等级会产生不同结果。
2. 原代码先关闭中断，再调用HAL反初始化。若HAL内部等待依赖系统tick，可能无法正常结束。

本次把它改成：

```text
校验APP向量
  -> 发送F000
  -> 关闭USART2并执行HAL/RCC反初始化
  -> 关闭和清理外部中断、PendSV及SysTick挂起状态
  -> 设置APP向量表VTOR
  -> 清理BASEPRI/FAULTMASK/CONTROL
  -> 同一段汇编中设置MSP并BX到APP Reset_Handler
  -> APP入口重新开启中断
```

关键点是设置MSP后的下一条有效动作就是 `bx APP_Reset_Handler`，不会再返回Bootloader，也不会
再使用旧C栈。

Application入口同时增加了显式 `__enable_irq()`。无论APP由硬件复位启动还是由Bootloader在
关中断状态下跳入，都由APP自己接管中断状态。

### 2.2 如果更新后仍只有两行，怎样区分卡在哪里

按下面顺序检查，不要先用“下载并调试Application”覆盖现场：

1. 在SNTool选择5，应返回 `F009`。否则APP向量表或烧录地址错误。
2. 选择3，应返回 `F000`，SNTool随后退出。没有 `F000` 表示MCU没有收到菜单命令。
3. 观察上电自检灯。如果自检灯完全不执行，APP尚未进入任务阶段。
4. 使用调试器的 **Attach to running target/连接正在运行目标**，不要Reset、不要Download，查看PC：
   - 停在 `HardFault_Handler`：检查跳转现场和栈；
   - 停在 `Error_Handler`：继续看调用栈；
   - 停在 `HAL_RCC_OscConfig()`附近：重点检查LSE晶振。
5. 当前Application的 `.ioc`启用了RTC和外部LSE，`SystemClock_Config()`也要求LSE启动。如果模块板
   没有32.768 kHz晶振，APP会在创建FreeRTOS任务之前进入 `Error_Handler()`。这与Boot跳转是两个
   独立问题；只有调试器Attach后的PC/调用栈才能确认。未确认前，本次不擅自关闭RTC或修改IOC。
6. 如果自检灯正常而Agent仍只有两行，再检查串口占用和UART/DMA：

```bash
ls -l /dev/ttyUSB0
fuser -v /dev/ttyUSB0
```

同一时刻只能有SNTool或Agent中的一个占用USART2/CH340。

## 3. 现在只保留一个启动开关

文件：`Bootloader/Middleware/YMODEM/menu.h`

```c
#define BOOT_POWER_ON_JUMP_APP (0U)
```

| 值 | 上电行为 | 适用场景 |
|---:|---|---|
| `0U` | 停留在Bootloader菜单 | 首次写SN、检查APP、后续IAP开发 |
| `1U` | APP有效时直接跳APP | 日常Application和micro-ROS调试 |

这个开关与下列内容都无关：

- 不区分Debug或Release；
- 不检查是否已经写入SN；
- 不读取PA11或其他按键；
- 写SN成功后不自动跳APP。

上一版“菜单3无响应”的直接原因也与此有关：上一版Debug或已有SN的设备可能在SNTool打开串口前
就已经自动进入APP。SNTool虽然能打开 `/dev/ttyUSB0`，但连接到的已经是micro-ROS APP，不是
Bootloader菜单；此时发送ASCII字符 `1`当然收不到Bootloader的 `F000`。复位后仍按旧规则自动跳，
所以按复位键也无法回到菜单。现在默认值为 `0U`，复位后会稳定停在菜单等待SNTool。

修改数值后必须重新编译并重新烧录 **Bootloader**。只重新编译Application不会改变启动模式。

### 3.1 正常写SN模式

保持：

```c
#define BOOT_POWER_ON_JUMP_APP (0U)
```

上电后运行SNTool：

1. 菜单5检查APP；
2. 菜单2写SN；
3. 菜单1回读SN；
4. 菜单3跳转APP。

写SN成功只返回 `F016`，不会自动跳转。菜单3没有二次确认。

### 3.2 开发阶段直接进APP

改为：

```c
#define BOOT_POWER_ON_JUMP_APP (1U)
```

重新编译和烧录Bootloader。之后按复位键或重新上电，只要APP向量有效就直接进入APP，不需要
运行SNTool。以后需要重新写SN时，把开关改回 `0U`，重新烧录Bootloader即可。

## 4. Windows构建与烧录流程

### 4.1 编译Bootloader

在PowerShell执行：

```powershell
cd D:\InternWork\Code\Test_mygit\Tacapp_init\Bootloader
cmake --fresh --preset Release
cmake --build --preset Release
```

输出文件：

```text
D:\InternWork\Code\Test_mygit\Tacapp_init\Bootloader\build\Release\glove_UMI_BOOT.hex
```

### 4.2 编译Application

```powershell
cd D:\InternWork\Code\Test_mygit\Tacapp_init\Application
cmake --fresh --preset Release
cmake --build --preset Release
```

输出文件：

```text
D:\InternWork\Code\Test_mygit\Tacapp_init\Application\build\Release\glove_UMI_APP.hex
```

### 4.3 第一次完整烧录

使用ST-LINK/SWD：

1. 只在第一次需要清理旧布局时执行一次全片擦除；全片擦除也会清除SN页。
2. 烧录Bootloader HEX。它的链接起始地址是 `0x08000000`。
3. 不再全片擦除，烧录Application HEX。它的链接起始地址是 `0x08010000`。
4. 复位MCU。

HEX文件已经包含地址信息，不需要手动修改下载地址。后续只更新Application时，不要执行全片擦除，
否则Bootloader和SN也会被删除。

### 4.4 验证Boot跳转时不要混用两种启动方式

- 验证产品启动链路：烧录完成后退出Application调试会话，直接按板上复位键。
- 调试APP内部代码：可以启动Application Debug并Resume，但这是调试器直接启动APP，不是
  Bootloader跳转验证。
- 定位Boot跳转故障：使用Attach，不下载、不复位，保留故障现场。

## 5. WSL终端操作流程

以下ROS和Agent命令全部使用普通用户 `embedded`。不要使用 `wsl -u root`；root和embedded具有
不同的HOME、ROS工作空间和环境变量。

### 5.1 把USB串口连接到WSL

在Windows管理员PowerShell中查看设备：

```powershell
usbipd list
```

第一次使用某个BUSID时执行：

```powershell
usbipd bind --busid <BUSID>
```

每次重新连接或重启后需要时执行：

```powershell
usbipd attach --wsl --busid <BUSID>
```

进入普通用户WSL：

```powershell
wsl
```

检查串口：

```bash
whoami
ls -l /dev/ttyUSB0
```

`whoami`应输出 `embedded`。若只有权限问题，首次可把该用户加入 `dialout`组，然后重新打开WSL：

```bash
sudo usermod -aG dialout embedded
```

### 5.2 模式0：先写SN，再手动跳APP

先确认Agent已经停止，然后在WSL终端A执行：

```bash
cd /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/cmdfile
python3 sn_tool.py
```

工具自动优先使用 `/dev/ttyUSB0`。推荐操作顺序：

```text
5  检查APP，预期F009
2  写入SN，预期F013后F016
1  回读SN，核对18字节内容
3  跳转APP，预期F000；工具自动退出并释放串口
```

然后在同一个或另一个普通用户WSL终端启动Agent：

```bash
source /opt/ros/jazzy/setup.bash
source "$HOME/microros_jazzy_ws/install/local_setup.bash"
source "$HOME/tacapp_ros2_ws/install/local_setup.bash"
export ROS_DOMAIN_ID=9
ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyUSB0 -b 115200 -v6
```

Application的micro-ROS任务每500 ms重试一次Agent，因此Agent在APP之前或之后启动都可以；但
SNTool与Agent不能同时打开串口。

### 5.3 模式1：上电直接进APP

把 `BOOT_POWER_ON_JUMP_APP`设为 `1U`并重新烧录Bootloader后，不运行SNTool。WSL终端直接执行：

```bash
source /opt/ros/jazzy/setup.bash
source "$HOME/microros_jazzy_ws/install/local_setup.bash"
source "$HOME/tacapp_ros2_ws/install/local_setup.bash"
export ROS_DOMAIN_ID=9
ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyUSB0 -b 115200 -v6
```

再给板子上电或按复位。也可以先让板子启动，随后再启动Agent。

### 5.4 PC端检查

打开另一个普通用户WSL终端，加载同样的ROS环境：

```bash
source /opt/ros/jazzy/setup.bash
source "$HOME/tacapp_ros2_ws/install/local_setup.bash"
export ROS_DOMAIN_ID=9
ros2 node list
ros2 topic list -t
ros2 service list -t
```

当前工程已启用SN动态命名。若写入的是 `SN-TacGlove-000001`，预期接口为：

```text
/mcu_SN_TacGlove_000001/key_state
/mcu_SN_TacGlove_000001/mcu_status
/mcu_SN_TacGlove_000001/led_cmd
/mcu_SN_TacGlove_000001/sync
```

可直接运行自动发现工具，不需要手工输入Topic全名：

```bash
python3 /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/cmdfile/micro_ros_subscribe_device_data.py
python3 /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/cmdfile/micro_ros_publish_device_ctrl_data.py
```

前者监听按键和MCU状态并响应同步请求，后者显示设备及LED/蜂鸣器cmd菜单。SN无效时会出现 `mcu_SN_UNPROGRAMMED`，应先退出Agent，使用SNTool完成写入后复位。

## 6. 参考工程如何根据SN发送消息

参考工程不是把SN附加到每一帧消息后再发送，而是用SN生成 **ROS节点名、Topic名和Service名**。
这样多块MCU同时连接到ROS 2系统时，各设备天然使用不同的路径。

### 6.1 SN从哪里来

Bootloader把记录写在：

```text
0x0807F800  有效标记0xA55A5AA5
0x0807F804  SN字符数据
```

参考Application的 `Service/dev_version.c` 中，`read_device_serial_number()`直接读取这个Flash地址：

- 有效标记正确：复制SN到RAM；
- 有效标记错误：使用字符串 `NO-SN`。

Bootloader和Application必须对SN地址、有效标记和长度使用同一份约定。

### 6.2 怎样把SN变成ROS名称

参考工程 `Application/app_micro_ros.c` 的顺序是：

1. `read_device_serial_number(device_sn)`读取SN；
2. 把SN中的 `-`替换成 `_`，得到合法且稳定的名称片段；
3. 用 `snprintf()`拼接节点、Topic和Service名；
4. 把这些动态字符串传给 `rclc_node_init_default()`、
   `rclc_publisher_init_default()`、`rclc_subscription_init_default()`和
   `rclc_client_init_default()`；
5. 之后 `rcl_publish()`只负责向创建好的Publisher发消息，路由目标已经由Topic名确定。

参考工程生成规则为：

```text
节点：    mcu_<SN下划线形式>
Topic：  /mcu_<SN下划线形式>/button_device_state
         /mcu_<SN下划线形式>/button_remote_ctrl
         /mcu_<SN下划线形式>/encoder_state
         /mcu_<SN下划线形式>/mcu_status
         /mcu_<SN下划线形式>/command
Service：/mcu_<SN下划线形式>/sync
```

以当前项目SN `SN-TacGlove-000001`套用相同规则，名称片段是
`SN_TacGlove_000001`，例如：

```text
节点：/mcu_SN_TacGlove_000001
状态Topic：/mcu_SN_TacGlove_000001/mcu_status
命令Topic：/mcu_SN_TacGlove_000001/led_cmd
同步Service：/mcu_SN_TacGlove_000001/sync
```

PC软件先知道或扫描设备SN，再订阅/发布对应路径。Agent只做XRCE-DDS与ROS 2 DDS之间的桥接，
不会替MCU自动给Topic添加SN。

### 6.3 当前工程的实现

当前工程已经按参考思路完成SN隔离，但保留本项目自己的接口后缀：

1. `Common/Inc/firmware_layout.h`统一定义SN地址、有效标记和长度；
2. `Application/BSP/SN/bsp_sn_driver.c`只读并校验SN，不拥有擦写权限；
3. `micro_ros_task`创建ROS实体前把SN转换成安全名称片段；
4. 节点和四个接口使用同一设备前缀，避免部分Topic仍落在固定路径；
5. PC的监听和控制工具扫描 `mcu_SN_*`并支持多设备选择。

当前SN合同是固定18字节 `SN-TacGlove-000000`。用户示例 `SN-20260825-A00001`同样能被Application的名称转换逻辑处理，但Bootloader和SNTool尚未切换到该生产格式；如需切换，必须统一修改并验证整个SN写入合同。

## 7. 本次源码变更清单

| 文件 | 修改 |
|---|---|
| `Bootloader/Middleware/YMODEM/menu.h` | 删除三组自动判断，增加唯一开关 `BOOT_POWER_ON_JUMP_APP` |
| `Bootloader/Core/Src/main.c` | 仅在唯一开关为1时上电跳APP，否则进入菜单 |
| `Bootloader/Middleware/YMODEM/menu.c` | 删除按键/SN/构建类型判断；写SN后不自动跳；修正MSP与复位入口跳转 |
| `Bootloader/CMakeLists.txt` | 删除Debug专用自动跳转宏 |
| `Application/Core/Src/main.c` | APP接管VTOR后显式开启中断 |
| `Application/BSP/SN/bsp_sn_driver.c/.h` | 只读公共SN页并校验18字节SN |
| `Application/Tasks/micro_ros_task.c/.h` | 从SN生成节点、Topic和Service名称 |
| `cmdfile/sn_tool.py` | 写SN成功后留在菜单；菜单3仍直接跳转且无二次确认 |
| `cmdfile/micro_ros_subscribe_device_data.py` | 自动发现设备、监听两个PUB并提供同步Service |
| `cmdfile/micro_ros_publish_device_ctrl_data.py` | 自动发现设备并用菜单发布六灯/蜂鸣器cmd |
| `Docs/11_阶段5_Bootloader_SNTool_IAP实施记录.md` | 同步新的单开关规则和验证步骤 |

## 8. 本轮实板验收点

### 菜单模式（开关为0）

- 复位后SNTool可立即收到Bootloader状态；
- 菜单5返回 `F009`；
- 写SN后不自动离开菜单；
- 菜单3返回 `F000`并启动APP；
- 不进入Application Debug，Agent也能出现 `create_client`和 `session established`。

### 直接启动模式（开关为1）

- 不运行SNTool，复位后直接出现APP上电自检；
- Agent先启动或后启动都能在500 ms重试周期内建链；
- 进入APP Debug不是建立连接的必要步骤。

若最后一项仍失败，请记录：SNTool是否收到F000、APP自检灯是否执行，以及Attach后PC停在哪个函数。
这三项可以把问题明确分到Boot跳转、APP时钟初始化或USART2/micro-ROS任务三个层级。

## 9. micro-ROS静态库重新生成说明（保留）

本项目的 `.msg/.srv`是唯一需要维护的接口源定义。`libmicroros.a`和 `include/`不是STM32
Application本身的CMake自动生成，而是在WSL中的micro-ROS `generate_lib`固件工作区里完成
rosidl代码生成、ARM交叉编译和静态归档。

接口源目录：

```text
Application/Interfaces/common_msgs
├── msg/KeyState.msg
├── msg/LedCmd.msg
├── msg/MCUStatus.msg
└── srv/DeviceSynchronization.srv
```

以后接口内容变化时，使用普通用户WSL执行：

```bash
bash /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/Application/Middleware/Micro-ROS/library_generation/rebuild_microros_library.sh --install
```

脚本把接口包同步到：

```text
/home/embedded/microros_jazzy_ws/firmware/mcu_ws/custom_packages/common_msgs
```

并将生成结果安装回：

```text
Application/Middleware/Micro-ROS/libmicroros.a
Application/Middleware/Micro-ROS/include/
```

不带 `--install`只生成和检查，不替换工程正式文件。本次已经生成的新库时间为
`2026-09-21 10:44:36`，SHA-256为
`63CB00DA6DFF643EAA357FCA73DE3522D4D4FA1A2745B43DAFE0CC22D8DE2274`。
