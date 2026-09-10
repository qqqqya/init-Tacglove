/**
 * @file led_task.h
 * @brief LED自检和按键反馈任务入口。
 */
#ifndef LED_TASK_H
#define LED_TASK_H

/**
 * @brief 直接执行开始采集时的LED7和蜂鸣器动作。
 * @note LED上电自检完成前调用时不执行动作。
 */
void led_task_start_collection(void);

/**
 * @brief 执行上电灯光自检，完成后进入待机状态。
 * @param argument FreeRTOS预留任务参数，当前固定传入NULL。
 */
void led_task_entry(void *argument);

#endif /* LED_TASK_H */
