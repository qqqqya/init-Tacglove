# 阶段5：Bootloader、SNTool与IAP实施记录

## 1. 本次重建结论

Bootloader已按参考工程 `Tacglove/Bootloader` 的代码职责重新组织，不再采用上一版自定义目录：

```text
Tacapp_init/
├── Application/                     # 正式APP：FreeRTOS、BSP、micro-ROS
├── Bootloader/
│   ├── Core/                        # 时钟、GPIO、USART2和程序入口
│   ├── Drivers/                     # Bootloader独立使用的CMSIS/HAL
│   ├── Middleware/YMODEM/
│   │   ├── common.c/.h              # USART2原始字节和16位状态码收发
│   │   ├── flash_if.c/.h            # APP向量检查、SN页读写
│   │   └── menu.c/.h                # 菜单协议、SN命令、APP跳转
│   ├── glove_UMI_BOOT.ioc           # 最小Bootloader CubeMX配置
│   └── STM32G474XX_BOOT_FLASH.ld    # 0x08000000、64 KiB
├── cmdfile/
│   ├── sn_tool.py                   # Bootloader直连串口交互工具
│   ├── micro_ros_subscribe_device_data.py # 自动发现、监听PUB和同步服务
│   ├── micro_ros_publish_device_ctrl_data.py # 选择设备并发布LED cmd
│   └── stage3_pc_test.py            # 兼容旧命令的监听工具入口
└── Common/Inc/firmware_layout.h     # 两个工程共用的Flash分区契约
```

与参考工程的对应关系如下：

| 参考工程职责                   | 当前工程文件                       | 本阶段处理                               |
| ------------------------ | ---------------------------- | ----------------------------------- |
| `Core/Src/main.c`        | `Bootloader/Core/Src/main.c` | 初始化最小外设后进入 `Main_Menu()`            |
| `YMODEM/common`          | `Middleware/YMODEM/common`   | 保持USART2和大端16位状态码协议                 |
| `YMODEM/flash_if`        | `Middleware/YMODEM/flash_if` | 保持APP检查和SN Flash访问职责                |
| `YMODEM/menu`            | `Middleware/YMODEM/menu`     | 保持 `1/2/3`、`#F_SN_W`、`*F_SN_R` 命令体系 |
| `AppEncrypt/sn_tool.py`  | `cmdfile/sn_tool.py`         | 保持终端交互菜单，改用本项目SN格式                  |
| `ymodem/decrypt/mbedTLS` | 暂未加入                         | 等小阶段5.3以后再实施                        |

## 2. 当前阶段边界

本次完成小阶段5.2及5.2B代码：

- Bootloader独立工程和64 KiB链接区域；
- APP1有效性检查；
- 通过USART2命令跳转APP1；
- 固定格式SN的一次性写入和读取；
- PC端交互式SNTool；
- Application只读SN并动态生成micro-ROS节点、Topic和Service名称；
- PC端按 `mcu_SN_*` 自动发现设备、监听上报和下发LED/蜂鸣器cmd；
- Application和Bootloader的按键引脚均同步为PA11，内部上拉；
- `stage3_pc_test.py`和`sn_tool.py`统一放到根目录 `cmdfile`。

本阶段明确不包含：

- YMODEM固件接收和APP分区擦写；
- APP2备份、回滚和掉电恢复；
- 固件加密、签名和解密；
- 看门狗策略。

因此菜单第4项已经保留，但Bootloader收到命令 `2` 只返回 `F010`，不会擦除或写入任何APP分区。

## 3. Flash布局

| 区域             | 地址范围                    | 大小      | 当前用途            |
| -------------- | ----------------------- | -------:| --------------- |
| Bootloader     | `0x08000000~0x0800FFFF` | 64 KiB  | 启动、菜单、SN和跳转     |
| APP1           | `0x08010000~0x0803FFFF` | 192 KiB | 当前正式Application |
| APP2           | `0x08040000~0x0806FFFF` | 192 KiB | 后续IAP备份区，当前不读写  |
| 配置区            | `0x08070000~0x0807F7FF` | 62 KiB  | 预留              |
| SN页            | `0x0807F800~0x0807FFFF` | 2 KiB   | 一次性SN记录         |

地址统一定义在 `Common/Inc/firmware_layout.h`，Bootloader和Application不得各自再维护另一套分区地址。

## 4. Bootloader上电与菜单行为

当前启动策略只由 `Bootloader/Middleware/YMODEM/menu.h` 中的一个开关决定：

```c
#define BOOT_POWER_ON_JUMP_APP (0U)
```

- `0U`：上电停留在Bootloader菜单；写SN后仍留在菜单，由SNTool菜单3跳转APP；
- `1U`：APP1有效时上电直接跳转APP；APP1无效时仍留在菜单；
- 启动路径不再判断Debug/Release、不再判断SN，也不再使用PA11按键。

菜单模式启动后先发送 `F00E`，随后每3秒发送 `F00F`。详细跳转原理、烧录和WSL操作见
`Docs/12_Bootloader跳转_Agent启动与SN主题说明.md`。

## 5. PC菜单与MCU命令映射

SNTool运行后显示：

```text
========================================
         主菜单
========================================
1. 读取 SN
2. 写入 SN
3. 跳转到 APP
4. 下载 APP (Ymodem)
5. 检查 APP 分区

0. 退出
```

注意：PC菜单编号和MCU底层命令并非全部相同。映射沿用参考工程：

| PC菜单 | PC发送给MCU    | MCU行为                 |
| ----:| ----------- | --------------------- |
| 1    | `*F_SN_R`   | 读取SN                  |
| 2    | `#F_SN_W`   | 握手后接收固定18字节SN         |
| 3    | ASCII字符 `1` | 检查并跳转APP1             |
| 4    | ASCII字符 `2` | 当前返回 `F010`，YMODEM未开放 |
| 5    | ASCII字符 `3` | 检查APP1向量表             |
| 0    | 不发送         | PC工具关闭串口并退出           |

## 6. SN格式与Flash记录

本项目SN固定为：

```text
SN-TacGlove-000000
```

规则：

- 长度固定为18字节；
- 固定前缀为 `SN-TacGlove-`；
- 最后6位只允许十进制数字；
- 串口和Flash中均不存储结尾NUL；
- 不使用参考工程中的日期字段和日期混淆。

SN页记录保持参考工程易读的布局：

```text
0x0807F800  uint32_t 有效标记 0xA55A5AA5
0x0807F804  uint8_t  SN[18]
0x0807F816  2字节填充
```

写入规则：

1. MCU再次校验SN格式，不能只依赖PC工具校验；
2. 已存在有效标记时返回 `F015`，不擦除、不覆盖；
3. 首次写入时只擦除最后一个2 KiB SN页；
4. 先写记录后16字节，最后写入包含有效标记的首个Double Word；
5. 写入完成后逐字节回读校验；
6. 当前没有串口擦除或改写SN命令，返修时需通过受控SWD流程处理。

## 7. 串口协议和状态码

USART2/CH340参数为115200-8-N-1。16位状态码按高字节在前发送。

读取SN：

```text
PC  -> MCU : *F_SN_R
MCU -> PC  : SN-TacGlove-000001       固定18字节
          或 F017                     SN不存在或记录无效
```

写入SN：

```text
PC  -> MCU : #F_SN_W
MCU -> PC  : F013                     已准备接收SN
PC  -> MCU : SN-TacGlove-000001       固定18字节
MCU -> PC  : F016 / F015 / F014 / F017
```

本阶段使用的状态码：

| 状态码    | 含义                |
| ------:| ----------------- |
| `F000` | 准备跳转APP           |
| `F008` | APP1向量无效          |
| `F009` | APP1向量有效          |
| `F00E` | 已进入Bootloader菜单   |
| `F00F` | 等待用户命令            |
| `F010` | 命令错误或功能尚未开放       |
| `F013` | 等待18字节SN数据        |
| `F014` | SN接收超时或不完整        |
| `F015` | SN已存在，禁止覆盖        |
| `F016` | SN写入并回读成功         |
| `F017` | SN读取、格式或Flash写入失败 |

## 8. 构建和运行SNTool

构建Bootloader：

```powershell
cd D:\InternWork\Code\Test_mygit\Tacapp_init\Bootloader
cmake --fresh --preset Release
cmake --build --preset Release
```

当前Release构建结果：

| 固件         | 向量表地址        | Flash占用 | RAM占用  | 分区上限   |
| ---------- | ------------:| -------:| ------:| ------:|
| Bootloader | `0x08000000` | 10012 B | 1760 B | 64 KiB |
| Application | `0x08010000` | 144652 B | 89584 B | 192 KiB |

安装PC依赖并运行：

```powershell
python -m pip install pyserial
python D:\InternWork\Code\Test_mygit\Tacapp_init\cmdfile\sn_tool.py --port COM12
```

不提供 `--port` 时，工具自动优先使用 `/dev/ttyUSB0`；若不存在，则按顺序自动选择
其他 `/dev/ttyUSB*` 或 `/dev/ttyACM*`，不再要求用户选择。菜单3不再二次确认，收到
选择后立即发送跳转命令。SNTool直连USART2，不能与micro-ROS Agent同时占用同一个CH340串口。

### 8.1 Application的SN动态命名

Application通过只读驱动 `Application/BSP/SN/bsp_sn_driver.c`读取公共SN页。它不会擦除或写入SN；写权限仍只属于Bootloader。创建micro-ROS实体前，将SN中的连字符转换为下划线并统一生成：

```text
mcu_<SN下划线形式>
/mcu_<SN下划线形式>/key_state
/mcu_<SN下划线形式>/mcu_status
/mcu_<SN下划线形式>/led_cmd
/mcu_<SN下划线形式>/sync
```

因此当前格式 `SN-TacGlove-000001`会得到：

```text
mcu_SN_TacGlove_000001
/mcu_SN_TacGlove_000001/key_state
/mcu_SN_TacGlove_000001/mcu_status
/mcu_SN_TacGlove_000001/led_cmd
/mcu_SN_TacGlove_000001/sync
```

用户给出的格式示例 `SN-20260825-A00001`会按同一规则得到
`mcu_SN_20260825_A00001`。当前Bootloader/SNTool仍执行已确认的
`SN-TacGlove-000000`合同；以后若正式切换格式，应同时修改Bootloader校验、SNTool校验和生产数据规则。

SN页无有效标记或内容非法时，Application使用 `mcu_SN_UNPROGRAMMED`。这个回退值用于诊断，不允许作为量产设备的正式名称。

启动Agent后，在另两个已source接口工作区的WSL终端分别运行：

```bash
python3 /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/cmdfile/micro_ros_subscribe_device_data.py
python3 /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/cmdfile/micro_ros_publish_device_ctrl_data.py
```

两个工具默认等待ROS 2发现并列出全部 `mcu_SN_*`设备；只有一台时自动选择，多台时显示序号菜单。也可跳过发现直接指定：

```bash
python3 /mnt/d/InternWork/Code/Test_mygit/Tacapp_init/cmdfile/micro_ros_subscribe_device_data.py \
  --device SN-TacGlove-000001
```

监听工具订阅 `key_state`、`mcu_status`并提供 `sync` Service；控制工具通过菜单设置单颗/全部LED和蜂鸣器，再发布完整 `LedCmd`。旧的 `stage3_pc_test.py`保留为监听工具的兼容入口。

## 9. 烧录与上板验证顺序

1. 对测试板执行一次全片擦除；这一步会清除旧SN。
2. 烧录 `Bootloader/build/Release/glove_UMI_BOOT.hex`。
3. 不再执行全片擦除，烧录 `Application/build/Release/glove_UMI_APP.hex`。
4. 复位后启动SNTool，确认可以进入0~5交互菜单。
5. 选择5，应返回APP分区有效 `F009`。
6. 选择1，空白板应提示SN读取失败 `F017`。
7. 在写SN前选择4，应提示YMODEM尚未开放，且APP1内容不应发生变化。
8. 选择2并写入 `SN-TacGlove-000001`，应依次收到 `F013`、`F016`，设备继续停留在菜单。
9. 选择1，应完整读回同一18字节SN。
10. 再次选择2写入其他SN，应返回 `F015`，原SN保持不变。
11. 选择3，无二次确认；应先收到 `F000`，随后Application启动，SNTool关闭串口。
12. 启动Agent，复测LED、PA11按键、蜂鸣器和micro-ROS pub/sub。

## 10. 阶段5后续小阶段计划

### 10.1 小阶段5.2B：SN绑定ROS接口（当前）

代码、PC菜单工具、Application/Bootloader Release构建和Python语法检查已经完成。实板需要确认：写入SN后进入APP，ROS图中只出现对应的 `mcu_SN_*`设备；按键和状态上报、同步回复、六灯与蜂鸣器cmd均成功；两块不同SN设备同时在线时互不串话。实板通过后才能关闭5.2B。

### 10.2 小阶段5.3：明文YMODEM安全下载到APP2

目标只做“接收和验证”，不直接擦除APP1：

1. 恢复参考工程YMODEM接收状态机，菜单4进入下载；
2. 接收目标固定为APP2 `0x08040000~0x0806FFFF`；
3. 下载前检查文件长度，写入中检查地址边界和Flash错误；
4. 完成后检查向量表、实际长度与CRC32；
5. 合法固件保留在APP2并返回明确成功码，非法固件标记无效；
6. 覆盖正常、取消、超时、错误包、超大文件和传输中断测试。

完成标志：任何下载失败都不影响当前APP1和SN，复位仍能运行原Application。

### 10.3 小阶段5.4：APP安装、持久化状态与掉电回滚

在5.3稳定后，定义升级状态记录并实现APP1备份/安装/恢复。分别在备份、擦除、复制和校验过程中人为断电；复位后必须能继续恢复或进入安全菜单，不能启动半写入镜像。

### 10.4 小阶段5.5：固件头、版本和安全包

统一固件头中的硬件型号、版本、长度、校验和构建信息；先确定真实性/完整性策略，再加入加密。加密不能代替签名或消息认证；防降级、密钥存储和返修权限需要单独评审。

### 10.5 小阶段5.6：首次启动确认与IWDG

Application通过健康检查后写入“新版本启动成功”确认；多次启动失败触发回滚或安全菜单。IWDG最后接入，由Bootloader长操作和Application健康任务分别承担明确的喂狗职责。

### 10.6 小阶段5.7：量产工具与总体验收

在 `cmdfile`补齐构建、打包、升级和日志工具；完成至少20轮升级/回滚、关键步骤断电、8小时micro-ROS通信、双设备SN隔离及升级前后SN不变验证，并固化烧录与返修流程。

下一步进入条件：用户完成5.2B实板验证并反馈结果后，再开始5.3。5.3不会把回滚、加密或IWDG混在第一次YMODEM移植中。
