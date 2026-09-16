/**
 * @file key_task.h
 * @brief 数据采集按键扫描任务入口。
 */
#ifndef KEY_TASK_H
#define KEY_TASK_H

/** @brief 按键状态机扫描周期，单位ms。 */
#define KEY_TASK_PERIOD_MS 1U

/**
 * @brief 每1 ms推进PA0按键状态机，并通知LED任务执行对应反馈。
 * @param argument FreeRTOS预留任务参数，当前固定传入NULL。
 */
void key_task_entry(void *argument);

#endif /* KEY_TASK_H */
