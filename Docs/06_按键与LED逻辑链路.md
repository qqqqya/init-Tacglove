# 06 按键与LED逻辑链路

> 历史说明：本文的灯效和按键状态机仍可用于理解业务；“不使用Task Notification”的传递方式已被替代，当前链路以 `10_按键任务通知_软件接口与Agent日志说明.md`为准。

## 1. 按键完整调用链

```mermaid
flowchart LR
    A[PA11物理电平<br/>松开高/按下低] --> B[key_read_level]
    B --> C[bsp_key_handler_process<br/>1 ms推进一次]
    C --> D{按键状态机}
    D -->|单击| E[KEY_EVENT_SHORT_PRESS]
    D -->|长按| F[KEY_EVENT_LONG_PRESS]
    D -->|双击| G[KEY_EVENT_DOUBLE_CLICK]
    E --> H[key_task switch]
    F --> H
    G --> H
    H -->|直接调用| I[led_task_on_short_press]
    H -->|直接调用| J[led_task_on_long_press]
    H -->|直接调用| K[led_task_on_double_click]
    I --> L[led_task处理灯效/蜂鸣]
    J --> L
    K --> L
```

这里没有FreeRTOS Task Notification。按键任务直接调用LED任务对外接口；接口快速记录动作后立即返回，耗时动作仍在LED任务中运行，避免阻塞1 ms按键扫描。

## 2. 按键状态机

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> DEBOUNCE_PRESS: 检测到PA11低电平
    DEBOUNCE_PRESS --> IDLE: 20 ms内恢复高电平
    DEBOUNCE_PRESS --> HOLD: 持续低电平20 ms

    HOLD --> HOLD: 持续按下，小于400 ms
    HOLD --> HOLD: 达到400 ms / 产生长按事件一次
    HOLD --> DEBOUNCE_RELEASE: 检测到松开

    DEBOUNCE_RELEASE --> HOLD: 20 ms内再次变低，视为抖动
    DEBOUNCE_RELEASE --> IDLE: 长按已触发且稳定松开
    DEBOUNCE_RELEASE --> WAIT_DOUBLE: 短按稳定松开

    WAIT_DOUBLE --> IDLE: 等待350 ms超时 / 产生单击事件
    WAIT_DOUBLE --> WAIT_DOUBLE: 第二次按下尚未稳定20 ms
    WAIT_DOUBLE --> DOUBLE_DONE: 第二次按下稳定20 ms / 产生双击事件

    DOUBLE_DONE --> IDLE: 第二次稳定松开20 ms
```

### 单击时间链

```mermaid
sequenceDiagram
    participant GPIO as PA11
    participant Handler as KEY Handler
    participant KeyTask as key_task
    participant LedTask as led_task

    GPIO->>Handler: 第一次按下并稳定20 ms
    GPIO->>Handler: 第一次松开并稳定20 ms
    Handler->>Handler: 等待双击窗口350 ms
    Handler-->>KeyTask: KEY_EVENT_SHORT_PRESS
    KeyTask->>LedTask: led_task_on_short_press()
```

### 双击时间链

```mermaid
sequenceDiagram
    participant GPIO as PA11
    participant Handler as KEY Handler
    participant KeyTask as key_task
    participant LedTask as led_task

    GPIO->>Handler: 第一次按下/松开均稳定20 ms
    GPIO->>Handler: 350 ms内第二次按下
    Handler->>Handler: 第二次按下稳定20 ms
    Handler-->>KeyTask: KEY_EVENT_DOUBLE_CLICK
    KeyTask->>LedTask: led_task_on_double_click()
    GPIO->>Handler: 第二次稳定松开后回到IDLE
```

### 长按时间链

```mermaid
sequenceDiagram
    participant GPIO as PA11
    participant Handler as KEY Handler
    participant KeyTask as key_task
    participant LedTask as led_task

    GPIO->>Handler: 按下并稳定20 ms
    Handler->>Handler: 继续保持低电平400 ms
    Handler-->>KeyTask: KEY_EVENT_LONG_PRESS，仅一次
    KeyTask->>LedTask: led_task_on_long_press()
    GPIO->>Handler: 稳定松开后回到IDLE
```

## 3. LED物理映射

SK6805数据先到LED7，再依次到LED6、LED5、LED4、LED3、LED2：

| 业务对象 | 灯号 | 发送缓存序号 |
|---|---:|---:|
| 系统状态 | LED7 | 0 |
| 相机5 | LED6 | 1 |
| 相机4 | LED5 | 2 |
| 相机3 | LED4 | 3 |
| 相机2 | LED3 | 4 |
| 相机1 | LED2 | 5 |

鱼眼相机当前不设置独立指示灯。

## 4. LED主状态逻辑

```mermaid
stateDiagram-v2
    [*] --> SELF_TEST: 上电
    SELF_TEST: LED2~LED6绿色闪烁3次\nLED7熄灭
    SELF_TEST --> IDLE: 自检通过
    SELF_TEST --> FAULT: 初始化或发送失败

    IDLE: LED2~LED6绿色常亮\nLED7熄灭
    IDLE --> PREPARING: 单击

    PREPARING: LED2~LED6绿色常亮\nLED7蓝色闪烁3次
    PREPARING --> CAPTURING: 蜂鸣120 ms后
    PREPARING --> FAULT: LED或蜂鸣器失败

    CAPTURING: LED2~LED6绿色常亮\nLED7蓝色常亮
    CAPTURING --> IDLE: 再次单击并蜂鸣120 ms
    CAPTURING --> FAULT: LED或蜂鸣器失败

    FAULT: 六颗灯红色常亮
```

## 5. 灯效与需求表逐项对应

| 状态 | LED2~LED6 | LED7 | 蜂鸣器 |
|---|---|---|---|
| 上电自检 | 绿色闪烁 | 熄灭 | 关闭 |
| 待机 | 绿色常亮 | 熄灭 | 关闭 |
| 数采准备 | 绿色常亮 | 蓝色闪烁 | 暂不鸣叫 |
| 开始采集 | 绿色常亮 | 蓝色常亮 | 切换前短鸣120 ms |
| 故障 | 红色常亮 | 红色常亮 | 关闭 |

黄色状态未实现。长按和双击的临时识别反馈结束后，会恢复进入反馈前的待机或采集显示，不改变LED主状态。
