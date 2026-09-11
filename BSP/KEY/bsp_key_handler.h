/**
 * @file bsp_key_handler.h
 * @brief PA11数据采集按键状态机接口。
 */
#ifndef BSP_KEY_HANDLER_H
#define BSP_KEY_HANDLER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    KEY_HANDLER_OK              = 0,
    KEY_HANDLER_ERROR           = 1,
    KEY_HANDLER_ERROR_TIMEOUT   = 2,
    KEY_HANDLER_ERROR_RESOURCE  = 3,
    KEY_HANDLER_ERROR_PARAMETER = 4,
    KEY_HANDLER_ERROR_NO_MEMORY = 5,
    KEY_HANDLER_ERROR_ISR       = 6,
    KEY_HANDLER_RESERVED        = 0xFF
} key_handler_status_t;

/** @brief 按键识别过程的内部状态。 */
typedef enum
{
    KEY_STATE_IDLE = 0,          /**< 空闲，等待按下。 */
    KEY_STATE_DEBOUNCE_PRESS,    /**< 检测到按下，正在执行按下消抖。 */
    KEY_STATE_HOLD,              /**< 已确认按下，监测长按或松开。 */
    KEY_STATE_DEBOUNCE_RELEASE,  /**< 检测到松开，正在执行松开消抖。 */
    KEY_STATE_WAIT_DOUBLE,       /**< 首次松开后等待第二次按下。 */
    KEY_STATE_DOUBLE_DONE        /**< 双击成立后等待第二次松开。 */
} key_state_t;

/** @brief 按键状态机识别完成后锁存的事件。 */
typedef enum
{
    KEY_EVENT_NONE = 0,          /**< 没有新按键事件。 */
    KEY_EVENT_LONG_PRESS,        /**< 持续按下达到长按门限。 */
    KEY_EVENT_SHORT_PRESS,       /**< 双击窗口超时且没有第二次按下。 */
    KEY_EVENT_DOUBLE_CLICK       /**< 双击窗口内完成第二次有效按下。 */
} key_event_t;

/**
 * @brief 初始化单路数据采集按键状态机。
 * @retval KEY_HANDLER_OK 初始化成功。
 * @note PA11输入上拉由CubeMX生成的GPIO代码完成。
 */
key_handler_status_t bsp_key_handler_init(void);

/**
 * @brief 读取PA11并向前推进一次按键状态机。
 * @retval KEY_HANDLER_OK 处理成功。
 * @retval KEY_HANDLER_ERROR_RESOURCE Handler尚未初始化。
 * @note 应由任务每1 ms调用一次。
 */
key_handler_status_t bsp_key_handler_process(void);

/**
 * @brief 读取当前已锁存的按键事件。
 * @param[out] event 非空输出指针。
 * @retval KEY_HANDLER_OK 读取成功。
 * @retval KEY_HANDLER_ERROR_RESOURCE Handler尚未初始化。
 * @retval KEY_HANDLER_ERROR_PARAMETER event为空指针。
 * @note 本函数不自动清除事件。
 */
key_handler_status_t bsp_key_handler_get_event(key_event_t *event);

/**
 * @brief 清除已处理的按键事件。
 * @retval KEY_HANDLER_OK 清除成功。
 * @retval KEY_HANDLER_ERROR_RESOURCE Handler尚未初始化。
 */
key_handler_status_t bsp_key_handler_clear_event(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_KEY_HANDLER_H */
