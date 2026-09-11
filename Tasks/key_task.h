/**
 * @file key_task.h
 * @brief 数据采集按键扫描任务入口。
 */
#ifndef KEY_TASK_H
#define KEY_TASK_H

/**
 * @brief 每1 ms推进PA11按键状态机，并直接调用对应LED任务动作接口。
 * @param argument FreeRTOS预留任务参数，当前固定传入NULL。
 */
void key_task_entry(void *argument);

#endif /* KEY_TASK_H */
