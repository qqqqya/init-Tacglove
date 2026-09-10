/**
 * @file bsp_led_driver.h
 * @brief SK6805 灯链底层驱动接口。
 * @details
 * 本层只负责六颗物理灯珠的软件帧缓存、GRB 编码和 PB0 单总线发送，
 * 不包含相机编号、采集状态等业务含义，也不依赖 FreeRTOS。
 */
#ifndef BSP_LED_DRIVER_H
#define BSP_LED_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 原理图中串联的 SK6805 灯珠总数。 */
#define BSP_LED_PIXEL_COUNT 6U

/** @brief RGB 颜色；三个亮度分量的有效范围均为 0~255。 */
typedef struct
{
    uint8_t red;   /**< 红色亮度。 */
    uint8_t green; /**< 绿色亮度。 */
    uint8_t blue;  /**< 蓝色亮度。 */
} bsp_led_color_t;

/** @brief LED Driver 层函数返回状态。 */
typedef enum
{
    LED_OK             = 0,    /**< Operation completed successfully. */
    LED_ERROR          = 1,    /**< General runtime error. */
    LED_ERRORTIMEOUT   = 2,    /**< Operation timed out. */
    LED_ERRORRESOURCE  = 3,    /**< Required resource is unavailable. */
    LED_ERRORPARAMETER = 4,    /**< Invalid parameter. */
    LED_ERRORNOMEMORY  = 5,    /**< Memory allocation failed. */
    LED_ERRORISR       = 6,    /**< Operation is not allowed in ISR context. */
    LED_RESERVED       = 0xFF  /**< Reserved status. */
} led_driver_status_t;

/**
 * @brief 初始化 SK6805 底层驱动和软件帧缓存。
 * @details 启用 Cortex-M4 DWT 周期计数器、计算当前主频对应的协议周期数，
 *          将 PB0 拉低并清空六颗灯的缓存；本函数不会发送灯珠数据。
 * @retval LED_OK 初始化成功。
 * @retval LED_ERRORRESOURCE 系统主频无效或 DWT 周期计数器不可用。
 */
led_driver_status_t bsp_led_driver_init(void);

/**
 * @brief 修改一个物理灯珠的软件缓存颜色。
 * @param pixel_index 串行数据流中的物理序号，范围为 0~5；0 表示最先接收数据的 LED7。
 * @param color 要写入的 RGB 颜色，各分量范围为 0~255。
 * @retval LED_OK 缓存写入成功。
 * @retval LED_ERRORPARAMETER pixel_index 超出有效范围。
 * @note 调用后必须再调用 bsp_led_driver_commit()，物理灯珠才会更新。
 */
led_driver_status_t bsp_led_driver_set_pixel(uint8_t pixel_index,
                                              bsp_led_color_t color);

/**
 * @brief 将六颗物理灯珠的软件缓存全部清零。
 * @note 本函数不发送数据，需随后调用 bsp_led_driver_commit()。
 */
void bsp_led_driver_clear(void);

/**
 * @brief 将完整的六像素缓存提交到 SK6805 灯链并触发锁存。
 * @details 按 G-R-B、最高位优先发送 144 bit；发送数据期间短暂关闭中断，
 *          完成后恢复原中断状态，并保持 PB0 低电平至少 300 us。
 * @retval LED_OK 一帧数据发送并锁存完成。
 * @retval LED_ERRORRESOURCE 尚未成功初始化 Driver。
 * @warning 修改系统主频、编译器或优化等级后，必须重新用逻辑分析仪校准波形。
 */
led_driver_status_t bsp_led_driver_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_DRIVER_H */
