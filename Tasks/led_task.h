/**
 * @file led_task.h
 * @brief LED状态任务及按键动作入口。
 */
#ifndef LED_TASK_H
#define LED_TASK_H

#include <stdint.h>

#include "task_status.h"

#define LED_TASK_LED_COUNT 6U/** @brief `LedCmd.led_mode`固定控制的逻辑灯数量。 */

/** @brief PC端可下发的单灯显示模式。 */
typedef enum
{
    LED_TASK_MODE_OFF         = 0, /**< 熄灭。 */
    LED_TASK_MODE_GREEN_SOLID = 1, /**< 绿色常亮。 */
    LED_TASK_MODE_GREEN_BLINK = 2, /**< 绿色闪烁。 */
    LED_TASK_MODE_RED_SOLID   = 3, /**< 红色常亮。 */
    LED_TASK_MODE_BLUE_BLINK  = 4, /**< 蓝色闪烁。 */
    LED_TASK_MODE_BLUE_SOLID  = 5  /**< 蓝色常亮。 */
} led_task_mode_t;

/** @brief 对外上报的MCU运行状态。 */
typedef enum
{
    LED_TASK_SYSTEM_SELF_TEST  = 0, /**< 正在执行上电自检。 */
    LED_TASK_SYSTEM_IDLE       = 1, /**< 自检通过，等待采集。 */
    LED_TASK_SYSTEM_PREPARING  = 2, /**< 正在执行采集准备反馈。 */
    LED_TASK_SYSTEM_COLLECTING = 3, /**< 正在采集。 */
    LED_TASK_SYSTEM_ERROR      = 4, /**< LED或蜂鸣器故障。 */
    LED_TASK_SYSTEM_UPDATING   = 5  /**< 为后续IAP升级预留。 */
} led_task_system_state_t;

/**
 * @brief 创建LED cmd邮箱。
 * @retval TASK_OK 邮箱创建成功或已经存在。
 * @retval TASK_ERROR_NO_MEMORY FreeRTOS无法分配Queue。
 * @note 必须在调度器启动前由task_manager_init()调用。
 */
task_status_t led_task_resources_init(void);

/**
 * @brief 提交一帧由PC下发的六灯模式。
 * @param led_mode 非空数组，按LED2、LED3、LED4、LED5、LED6、LED7排列。
 * @retval TASK_OK cmd已写入单元素最新值邮箱。
 * @retval TASK_ERROR_RESOURCE 邮箱尚未创建。
 * @retval TASK_ERROR_PARAMETER 指针为空或存在非法模式。
 * @note 新cmd覆盖尚未执行的旧cmd，避免断线或拥塞后回放过期灯效。
 */
task_status_t led_task_submit_cmd(
    const uint8_t led_mode[LED_TASK_LED_COUNT]);

/**
 * @brief 释放PC远程灯光控制并恢复本地按键状态显示。
 * @retval TASK_OK 请求已写入邮箱。
 * @retval TASK_ERROR_RESOURCE 邮箱尚未创建。
 */
task_status_t led_task_release_remote_control(void);

/**
 * @brief 读取当前LED任务表示的MCU运行状态。
 * @return led_task_system_state_t枚举值。
 */
uint8_t led_task_get_system_state(void);

/**
 * @brief 请求执行单击对应的采集开始/停止灯效。
 * @note 本函数由按键任务直接调用，不使用FreeRTOS任务通知。
 */
void led_task_on_short_press(void);

/**
 * @brief 请求执行长按识别反馈。
 * @note 当前仅用于验证长按识别，不改变采集状态。
 */
void led_task_on_long_press(void);

/**
 * @brief 请求执行双击识别反馈。
 * @note 当前仅用于验证双击识别，不改变采集状态。
 */
void led_task_on_double_click(void);

/**
 * @brief 执行上电绿色自检和LED状态处理。
 * @param argument FreeRTOS预留任务参数，当前固定传入NULL。
 */
void led_task_entry(void *argument);

#endif /* LED_TASK_H */
