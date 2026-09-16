# STM32G474五指数采工程C代码规范

本文汇总当前工程已经确定的命名、分层、返回状态、错误检查、注释、任务和CMake规范。后续新增外设及任务时按本文执行。

## 1. 适用范围

- `BSP`中的Driver和Handler；
- `Tasks`中的FreeRTOS任务；
- 用户新增的Middleware适配层；
- 顶层 `CMakeLists.txt` 中的用户源码配置。

CubeMX生成代码保持原有ST格式，人工修改只能放在 `USER CODE BEGIN/END` 区域，不为统一风格而批量改写生成文件。

## 2. 分层及依赖方向

```text
Tasks
    -> BSP Handler
        -> BSP Driver
            -> STM32 HAL/CMSIS
```

- Driver只处理寄存器、GPIO、总线协议、有效电平和物理通道；
- Handler负责板级设备编号、逻辑映射及Driver状态转换；简单外设经评审后可只保留Driver；
- Task负责动作顺序、周期、阻塞、任务通信和故障处理；
- Task不直接调用HAL；
- BSP不创建FreeRTOS任务，也不使用任务延时；
- 下层不能反向依赖上层。

## 3. 文件和函数命名

目录已经表达模块层级，文件名不重复堆叠目录含义：

```text
Tasks/led_task.c       推荐
Tasks/user_led_task.c  不采用
```

推荐格式：

- Driver文件：`bsp_<device>_driver.c/.h`；
- Handler文件：`bsp_<device>_handler.c/.h`；
- Task文件：`<device>_task.c/.h`；
- 任务注册：`task_manager.c/.h`；
- 任务公共状态、任务栈、优先级和任务句柄：`task_manager.h`。

函数使用小写下划线命名，公开函数带模块前缀，文件内部静态函数只保留必要的模块语义。

### 3.1 头文件集中定义规则

为了便于跨文件查找和统一修改，本工程自写的 `Tasks`、`BSP` 模块遵守以下规则：

- 模块配置宏、时间参数、数组长度、状态枚举、命令枚举和公共结构体写在对应 `.h`；
- 同一模块的 `.c` 不再重复定义这些宏；
- Task层统一返回状态 `task_status_t` 写在 `Tasks/task_manager.h`，不再单独维护 `task_status.h`；
- LED任务时序和通知位写在 `Tasks/led_task.h`；
- SK6805协议及PWM DMA参数写在 `BSP/LED/bsp_led_driver.h`；
- 按键消抖、长按和双击时间写在 `BSP/KEY/bsp_key_handler.h`；
- micro-ROS的Domain ID、Topic、Service及连接参数写在 `Tasks/micro_ros_task.h`；
- 确需跨文件访问的全局对象在 `.h` 中使用 `extern`声明，并且只在一个 `.c` 中定义存储；全局对象使用 `g_`前缀；
- 只在当前 `.c` 使用的运行状态、缓存和辅助对象仍使用 `static`留在 `.c`，不得仅为“方便访问”而暴露内部可写变量；外部访问应优先提供模块函数；
- 头文件只放声明和定义信息，不能在头文件中直接定义普通全局变量，否则多个 `.c` 包含后会产生重复定义；
- CubeMX生成文件、第三方库和自动生成的micro-ROS文件保持原格式，不执行此类搬移。

示例：

```c
/* task_manager.h */
extern TaskHandle_t g_led_task_handle;

/* task_manager.c：全工程只能有一个定义 */
TaskHandle_t g_led_task_handle;
```

## 4. 返回值规范

可能失败的公开函数和内部流程函数不使用 `bool` 表示结果，统一返回状态枚举。`bool`仍可用于真正的二值输入或内部标志，例如蜂鸣器开关参数和初始化标志。

状态枚举至少保留以下类别：handler 和driver分成下面这两种形式

```c
typedef enum
{
	HANDLER_OK             = 0,      /* Operation completed successfully         */
	HANDLER_ERROR          = 1,      /* General runtime error                    */
	HANDLER_ERRORTIMEOUT   = 2,      /* Operation timed out                      */
	HANDLER_ERRORRESOURCE  = 3,      /* Required resource is unavailable         */
	HANDLER_ERRORPARAMETER = 4,      /* Invalid parameter error                  */
	HANDLER_ERRORNOMEMORY  = 5,      /* Memory allocation failed                 */
	HANDLER_ERRORISR       = 6,      /* Not allowed in ISR context               */
	HANDLER_RESERVED       = 0xFF,   /* Reserved status                          */
} led_handler_status_t;

typedef enum
{
	LED_OK             = 0,      /* Operation completed successfully         */
	LED_ERROR          = 1,      /* General runtime error                    */
	LED_ERRORTIMEOUT   = 2,      /* Operation timed out                      */
	LED_ERRORRESOURCE  = 3,      /* Required resource is unavailable         */
	LED_ERRORPARAMETER = 4,      /* Invalid parameter error                  */
	LED_ERRORNOMEMORY  = 5,      /* Memory allocation failed                 */
	LED_ERRORISR       = 6,      /* Not allowed in ISR context               */
	LED_RESERVED       = 0xFF,   /* Reserved status                          */
} led_driver_status_t;
```

各层使用自己的状态类型：--写在相应的 .h 文件中。

- LED Driver：`led_driver_status_t`；
- LED Handler：`led_handler_status_t`；
- BEEP Driver：`beep_driver_status_t`；
- KEY Handler：`key_handler_status_t`；
- Task：`task_status_t`。

Handler不能直接把Driver状态当作自己的状态返回，应通过转换函数进行映射。这样调试器能够判断错误发生在哪一层，也方便后续扩展层内专有错误。

## 5. 状态判断规范

每个可能失败的步骤单独调用、保存状态、判断并返回原始错误：

```c
led_handler_status_t status = bsp_led_handler_set(BSP_LED_SYSTEM, color);
if (HANDLER_OK != status)
{
    /* 日志预留：记录系统状态灯设置失败及status。 */
    return status;
}

status = bsp_led_handler_commit();
if (HANDLER_OK != status)
{
    /* 日志预留：记录帧提交失败及status。 */
    return status;
}

return HANDLER_OK;
```

禁止使用下列写法：

```c
return action_a() && action_b() && action_c();
```

原因是短路求值会隐藏未执行步骤，同时只能得到真/假，无法定位Timeout、Parameter、Resource或NoMemory错误。

判断时统一写成：

```c
if (HANDLER_OK != status)
```

不能只写 `if (HANDLER_ERROR == status)`，否则其他非成功状态会被当成成功继续执行。

## 6. Task规范

所有任务由 `task_manager_init()` 集中创建，`main.c`不直接创建单个业务任务。创建结果必须检查：

```c
task_status_t result = led_task_create();
if (TASK_OK != result)
{
    return result;
}
```

任务入口必须符合FreeRTOS要求的函数原型，并使用函数左大括号同行格式：

```c
void led_task_entry(void *argument){
    /* Task body. */
}
```

`argument`是FreeRTOS预留的任务参数。当前创建任务时传入 `NULL`，所以任务中不使用它。以前的 `(void)argument;` 只用于抑制“未使用参数”编译警告，没有业务功能和运行效果；当前编译选项不会因此报错，已按约定删除。以后任务需要配置参数时，应通过该指针传入结构体并检查空指针。

任务间通信按载荷选择：

- 只有一个接收任务、需要传递事件位且允许同类事件合并时，优先使用Task Notification的 `eSetBits`；
- 需要保存多条事件、传递结构体或保持发生顺序时使用Queue；
- 不允许用普通全局 `volatile`位图加临界区重复实现Task Notification；
- Task Notification发送方必须持有有效 `TaskHandle_t`，接收方使用 `xTaskNotifyWait()`读取并清除已处理位；
- ISR发送通知必须使用对应的 `FromISR` API，并按FreeRTOS要求执行必要的任务切换。

任务内的周期等待使用 `vTaskDelay()` 或 `vTaskDelayUntil()`，不使用 `HAL_Delay()`。故障任务不能通过关闭全局中断冻结整个系统。

## 7. 日志规范

当前阶段不集成日志库，已删除临时 `APP_LOG` 实现及全部调用。关键错误分支只保留统一注释：

```c
/* 日志预留：记录模块、步骤和status。 */
```

后续移植EasyLogger时：

1. 先完成UART、USB CDC或其他真实输出后端；
2. 在任务和Handler的关键失败分支接入日志；
3. 日志必须包含模块、步骤和状态值；
4. 高频循环和SK6805时序临界区禁止打印；
5. ISR中只能使用明确支持中断上下文的非阻塞接口；
6. 重新测量Flash、任务栈和执行时间。

## 8. 编译器属性规范

不为普通函数随意添加 `__attribute__`。确实需要时必须同时满足：

1. 有明确的硬件、链接或ABI原因；
2. 代码附近写明原因；
3. Debug和Release均验证；
4. 属性移除会导致可复现的问题。

普通业务函数不使用函数级 `optimize("O2")`。SK6805提交函数不添加函数级优化属性。

当前STM32G474的SK6805位时序使用TIM3_CH3 PWM + DMA：TIM3在170 MHz时采用 `PSC=0`、`ARR=203`，DMA逐码元更新CCR3，其中逻辑0和1的比较值分别为51和153。编译优化等级不会改变定时器输出时序；修改TIM3时钟、PSC、ARR、CCR编码值或GPIO电气配置后，必须用逻辑分析仪重新测量0.3 us/0.9 us脉宽及不小于300 us的复位低电平。

## 9. 注释和格式

- 公开函数、状态类型和关键静态函数使用Doxygen注释；
- `@param`说明单位、范围和空指针约束；
- `@retval`列出重要状态；
- 注释说明“为什么”，避免重复代码本身；
- 延时注释必须写清单位，`HAL_Delay(125)`不能注释为12 us；
- 一行只完成一个关键动作；
- 错误分支使用大括号；
- 比较状态时把常量写在左侧，例如 `TASK_OK != result`；
- 不覆盖开发者后续已经调整的业务参数、灯效顺序和板级映射。

函数定义的第一个左大括号必须跟在函数声明结尾的同一行，便于IDE折叠后仍能看到完整函数名：

```c
void func(void){
}

static task_status_t module_process(
    const uint8_t *data,
    uint32_t size){
}
```

`if`、`for`、`while`和 `switch`等控制语句继续使用当前工程的换行大括号格式。函数调用不是函数定义，不在调用末尾添加大括号。新增或修改函数必须使用上述格式；旧文件在功能修改时逐步统一，禁止为了纯格式一次性改写CubeMX、第三方库或自动生成代码。

开发者已经写入的解释性注释原则上保留。接口、参数或实际行为改变时，应就地修正已经失真的注释；不能为了格式统一而删除业务背景、调试结论或硬件说明。

## 10. CMake规范

当前工程明确列出源码，不使用自动通配：

- 新增 `.c`：加入 `target_sources()`；
- 新增头文件目录：加入 `target_include_directories()`；
- 仅新增同目录 `.h`：通常不用修改CMake；
- 新增任务：除了加入CMake，还必须在 `task_manager_init()` 注册；
- 修改源码列表或路径后：先 `cmake --preset Debug`，再Build；
- 用户源码写在顶层CMake，CubeMX生成源码由 `cmake/stm32cubemx/CMakeLists.txt` 管理。

提交前至少完成Debug和Release构建，并检查RAM、Flash、FreeRTOS heap及任务栈余量。
