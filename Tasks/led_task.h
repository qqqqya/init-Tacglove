/**
 * @file led_task.h
 * @brief LED状态任务及按键动作入口。
 */
#ifndef LED_TASK_H
#define LED_TASK_H

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
