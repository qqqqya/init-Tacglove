/**
 * @file key_task.h
 * @brief 数据采集按键扫描任务入口。
 */
#ifndef KEY_TASK_H
#define KEY_TASK_H

/**
 * @brief 扫描PA11按键，确认有效按下后直接执行系统指示动作。
 * @param argument FreeRTOS预留任务参数，当前固定传入NULL。
 */
void key_task_entry(void *argument);

#endif /* KEY_TASK_H */
