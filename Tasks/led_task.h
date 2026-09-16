/**
 * @file led_task.h
 * @brief LED状态任务及按键动作入口。
 */
#ifndef LED_TASK_H
#define LED_TASK_H

#include <stdint.h>

#include "task_manager.h"

/** @brief `LedCmd.led_mode`固定控制的逻辑灯数量。 */
#define LED_TASK_LED_COUNT 6U

/** @brief 本地灯效和蜂鸣器时序参数。 */
#define SELF_TEST_BLINK_COUNT        3U   /**< 自检闪烁次数。 */
#define SELF_TEST_HALF_PERIOD_MS     250U /**< 自检闪烁半周期，单位ms。 */
#define PREPARE_BLINK_COUNT          3U   /**< 准备闪烁次数。 */
#define PREPARE_HALF_PERIOD_MS       250U /**< 准备闪烁半周期，单位ms。 */
#define SHORT_BEEP_TIME_MS           120U /**< 本地短蜂鸣时间，单位ms。 */
#define LONG_BEEP_TIME_MS            600U /**< 本地长蜂鸣时间，单位ms。 */
#define DOUBLE_BEEP_TIME_MS          100U /**< 本地双蜂鸣单次时间，单位ms。 */
#define DOUBLE_BEEP_INTERVAL_MS      100U /**< 本地双蜂鸣间隔，单位ms。 */
#define REMOTE_SHORT_BEEP_TIME_MS    200U /**< 远程短鸣时间，单位ms。 */
#define REMOTE_LONG_BEEP_TIME_MS     600U /**< 远程长鸣时间，单位ms。 */
#define REMOTE_BEEPING_TIME_MS       150U /**< 远程双鸣单次时间，单位ms。 */
#define REMOTE_BEEPING_INTERVAL_MS   250U /**< 远程双鸣间隔，单位ms。 */
#define LONG_FEEDBACK_TIME_MS        600U /**< 长按灯光反馈时间，单位ms。 */
#define DOUBLE_FEEDBACK_TIME_MS      100U /**< 双击灯光反馈时间，单位ms。 */
#define REMOTE_BLINK_HALF_PERIOD_MS  250U /**< 远程闪烁半周期，单位ms。 */
#define LED_TASK_PERIOD_MS           1U   /**< LED任务周期，单位ms。 */
#define LED_BRIGHTNESS               2U   /**< LED亮度，有效范围0~255。 */

/** @brief 按键任务通过Task Notification发送给LED任务的动作位。 */
#define LED_TASK_NOTIFY_SHORT_PRESS  (1UL << 0U)
#define LED_TASK_NOTIFY_LONG_PRESS   (1UL << 1U)
#define LED_TASK_NOTIFY_DOUBLE_CLICK (1UL << 2U)
#define LED_TASK_NOTIFY_ALL          (LED_TASK_NOTIFY_SHORT_PRESS | \
                                      LED_TASK_NOTIFY_LONG_PRESS | \
                                      LED_TASK_NOTIFY_DOUBLE_CLICK)

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

/** @brief PC端可下发的蜂鸣器模式，编号与LedCmd.msg保持一致。 */
typedef enum
{
    LED_TASK_BEEP_OFF   = 0, /**< 立即关闭蜂鸣器。 */
    LED_TASK_BEEP_SHORT = 1, /**< 短鸣一次。 */
    LED_TASK_BEEP_LONG  = 2, /**< 长鸣一次。 */
    LED_TASK_BEEPING    = 3  /**< 按参考工程执行两次短鸣。 */
} led_task_beep_mode_t;

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
 * @brief 提交一帧由PC下发的六灯和蜂鸣器模式。
 * @param led_mode 非空数组，按LED2、LED3、LED4、LED5、LED6、LED7排列。
 * @param beep_mode LED_TASK_BEEP_xxx蜂鸣器模式。
 * @retval TASK_OK cmd已写入单元素最新值邮箱。
 * @retval TASK_ERROR_RESOURCE 邮箱尚未创建。
 * @retval TASK_ERROR_PARAMETER 指针为空或存在非法模式。
 * @note 新cmd覆盖尚未执行的旧cmd，避免断线或拥塞后回放过期灯效。
 */
task_status_t led_task_submit_cmd(
    const uint8_t led_mode[LED_TASK_LED_COUNT],
    uint8_t beep_mode);

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
 * @note 本函数使用FreeRTOS Task Notification向LED任务设置动作位。
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
