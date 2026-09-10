/**
 * @file bsp_beep_driver.h
 * @brief 五指数采板有源蜂鸣器 GPIO 驱动接口。
 * @details
 * 本模块直接封装 PB9 及低电平有效的硬件特性，不创建 Handler 层，
 * 也不包含鸣叫时长、节奏或 FreeRTOS 延时。
 */
#ifndef BSP_BEEP_DRIVER_H
#define BSP_BEEP_DRIVER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 蜂鸣器 Driver 层函数返回状态。 */
typedef enum
{
    BEEP_DRIVER_OK              = 0,    /**< Operation completed successfully. */
    BEEP_DRIVER_ERROR           = 1,    /**< General runtime error. */
    BEEP_DRIVER_ERROR_TIMEOUT   = 2,    /**< Operation timed out. */
    BEEP_DRIVER_ERROR_RESOURCE  = 3,    /**< Required resource is unavailable. */
    BEEP_DRIVER_ERROR_PARAMETER = 4,    /**< Invalid parameter. */
    BEEP_DRIVER_ERROR_NO_MEMORY = 5,    /**< Memory allocation failed. */
    BEEP_DRIVER_ERROR_ISR       = 6,    /**< Operation is not allowed in ISR context. */
    BEEP_DRIVER_RESERVED        = 0xFF  /**< Reserved status. */
} beep_driver_status_t;

/**
 * @brief 初始化蜂鸣器 GPIO 状态并确保上电后保持静音。
 * @retval BEEP_DRIVER_OK 初始化成功，PB9 已输出高电平。
 * @note GPIO 模式由 CubeMX 初始化；本函数只建立 Driver 状态并设置安全电平。
 */
beep_driver_status_t bsp_beep_driver_init(void);

/**
 * @brief 设置蜂鸣器的开关状态。
 * @param active true 表示鸣叫，false 表示停止。
 * @retval BEEP_DRIVER_OK 输出状态设置成功。
 * @retval BEEP_DRIVER_ERROR_RESOURCE 尚未调用 bsp_beep_driver_init()。
 * @note 原理图使用 S8550 驱动有源蜂鸣器，因此 PB9 输出低电平时鸣叫。
 */
beep_driver_status_t bsp_beep_driver_set(bool active);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BEEP_DRIVER_H */
