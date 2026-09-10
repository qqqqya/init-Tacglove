/**
 * @file bsp_key_handler.h
 * @brief PA11 数据采集按键的板级读取接口。
 * @details 本层封装按键低电平有效特性、软件消抖和单次按下事件。
 */
#ifndef BSP_KEY_HANDLER_H
#define BSP_KEY_HANDLER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 按键 Handler 层函数返回状态。 */
typedef enum
{
    KEY_HANDLER_OK              = 0,    /**< Operation completed successfully. */
    KEY_HANDLER_ERROR           = 1,    /**< General runtime error. */
    KEY_HANDLER_ERROR_TIMEOUT   = 2,    /**< Operation timed out. */
    KEY_HANDLER_ERROR_RESOURCE  = 3,    /**< Required resource is unavailable. */
    KEY_HANDLER_ERROR_PARAMETER = 4,    /**< Invalid parameter. */
    KEY_HANDLER_ERROR_NO_MEMORY = 5,    /**< Memory allocation failed. */
    KEY_HANDLER_ERROR_ISR       = 6,    /**< Operation is not allowed in ISR context. */
    KEY_HANDLER_RESERVED        = 0xFF  /**< Reserved status. */
} key_handler_status_t;

/**
 * @brief 初始化数据采集按键 Handler。
 * @retval KEY_HANDLER_OK 初始化成功。
 * @note PA11 的输入和上拉配置由 CubeMX 生成的 GPIO 初始化完成。
 */
key_handler_status_t bsp_key_handler_init(void);

/**
 * @brief 检查数据采集按键是否产生一次有效按下。
 * @param[out] pressed 非空输出指针；true 仅在消抖后的新按下事件中返回一次。
 * @retval KEY_HANDLER_OK 读取成功。
 * @retval KEY_HANDLER_ERROR_RESOURCE Handler 尚未初始化。
 * @retval KEY_HANDLER_ERROR_PARAMETER pressed 为空指针。
 * @note 按键持续按住不会重复触发，松开并完成消抖后才允许下一次触发。
 */
key_handler_status_t bsp_key_handler_is_pressed(bool *pressed);

#ifdef __cplusplus
}
#endif

#endif /* BSP_KEY_HANDLER_H */
