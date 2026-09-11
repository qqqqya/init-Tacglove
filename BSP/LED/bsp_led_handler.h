/**
 * @file bsp_led_handler.h
 * @brief 五指数采板指示灯的逻辑映射接口。
 * @details
 * 本层将 LED2~LED7 的业务含义与 SK6805 串联方向、物理像素序号隔离。
 * LED2~LED6分别对应相机1~5，鱼眼相机不设置指示灯，LED7为系统状态灯。
 */
#ifndef BSP_LED_HANDLER_H
#define BSP_LED_HANDLER_H

#include <stdint.h>

#include "bsp_led_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 当前板上六颗指示灯的业务逻辑编号。 */
typedef enum
{
    BSP_LED_CAMERA_1 = 0,
    BSP_LED_CAMERA_2,
    BSP_LED_CAMERA_3,
    BSP_LED_CAMERA_4,
    BSP_LED_CAMERA_5,
    BSP_LED_SYSTEM,
    BSP_LED_ID_COUNT
} bsp_led_id_t;

/** @brief LED Handler 层函数返回状态。 */
typedef enum
{
    HANDLER_OK             = 0,    /**< Operation completed successfully. */
    HANDLER_ERROR          = 1,    /**< General runtime error. */
    HANDLER_ERRORTIMEOUT   = 2,    /**< Operation timed out. */
    HANDLER_ERRORRESOURCE  = 3,    /**< Required resource is unavailable. */
    HANDLER_ERRORPARAMETER = 4,    /**< Invalid parameter. */
    HANDLER_ERRORNOMEMORY  = 5,    /**< Memory allocation failed. */
    HANDLER_ERRORISR       = 6,    /**< Operation is not allowed in ISR context. */
    HANDLER_RESERVED       = 0xFF  /**< Reserved status. */
} led_handler_status_t;

/**
 * @brief 初始化 LED Driver，并向灯链提交一帧全灭数据。
 * @retval HANDLER_OK 初始化及首帧提交成功。
 * @retval HANDLER_ERRORRESOURCE TIM3_CH3 PWM DMA 或底层资源不可用。
 * @retval HANDLER_ERROR 其他底层错误。
 */
led_handler_status_t bsp_led_handler_init(void);

/**
 * @brief 修改一个业务逻辑灯的软件缓存颜色。
 * @param led 相机1~5或系统状态灯的逻辑编号。
 * @param color 目标 RGB 颜色，各分量范围为 0~255。
 * @retval HANDLER_OK 缓存写入成功。
 * @retval HANDLER_ERRORPARAMETER led 超出有效范围。
 * @note 本函数不立即刷新物理灯珠，需随后调用 bsp_led_handler_commit()。
 */
led_handler_status_t bsp_led_handler_set(bsp_led_id_t led,
                                          bsp_led_color_t color);

/**
 * @brief 将相机1~5对应的五颗指示灯设置为同一缓存颜色。
 * @param color 目标 RGB 颜色，各分量范围为 0~255。
 * @retval HANDLER_OK 五颗灯的缓存均设置成功。
 * @retval HANDLER_ERRORPARAMETER 逻辑映射或底层参数无效。
 * @retval HANDLER_ERROR 其他底层错误。
 * @note LED7系统状态灯不在本函数的修改范围内。
 */
led_handler_status_t bsp_led_handler_set_all_cameras(bsp_led_color_t color);

/**
 * @brief 清空六颗业务逻辑灯的软件缓存。
 * @note 本函数不发送数据，需随后调用 bsp_led_handler_commit()。
 */
void bsp_led_handler_clear(void);

/**
 * @brief 将 Handler 层当前缓存颜色提交到物理 SK6805 灯链。
 * @retval HANDLER_OK 一帧数据提交成功。
 * @retval HANDLER_ERRORRESOURCE LED Driver 尚未初始化或底层资源不可用。
 * @retval HANDLER_ERROR 其他底层错误。
 */
led_handler_status_t bsp_led_handler_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_HANDLER_H */
