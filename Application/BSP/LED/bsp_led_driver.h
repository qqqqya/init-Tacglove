/**
 * @file bsp_led_driver.h
 * @brief SK6805 灯链底层驱动接口。
 * @details
 * 本层只负责六颗物理灯珠的软件帧缓存、GRB 编码和 TIM3_CH3 PWM DMA 发送，
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

/** @brief SK6805协议与TIM3 PWM DMA帧参数。 */
/** @brief 每颗灯固定为G、R、B三个数据字节。 */
#define SK6805_BYTES_PER_PIXEL       3U
/** @brief 一个字节包含八个数据位。 */
#define SK6805_BITS_PER_BYTE         8U
/** @brief 六颗灯一帧的数据码元数：6 * 3 * 8 = 144。 */
#define SK6805_DATA_SLOT_COUNT       \
    (BSP_LED_PIXEL_COUNT * SK6805_BYTES_PER_PIXEL * SK6805_BITS_PER_BYTE)
/** @brief 170 MHz下逻辑0的300 ns高电平计数值。 */
#define SK6805_PWM_ZERO_TICKS        51U
/** @brief 170 MHz下逻辑1的900 ns高电平计数值。 */
#define SK6805_PWM_ONE_TICKS         153U
/** @brief 256个零占空比周期提供不小于300 us的复位低电平。 */
#define SK6805_RESET_SLOT_COUNT      256U
/** @brief DMA帧由复位低电平和144个数据码元组成。 */
#define SK6805_DMA_SLOT_COUNT        \
    (SK6805_RESET_SLOT_COUNT + SK6805_DATA_SLOT_COUNT)
/** @brief PWM DMA传输完成等待超时，单位ms。 */
#define SK6805_TRANSFER_TIMEOUT_MS   5U

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
 * @details 检查 TIM3_CH3 PWM DMA 资源，将比较值置零并清空六颗灯的缓存；
 *          本函数不会发送灯珠数据。
 * @retval LED_OK 初始化成功。
 * @retval LED_ERRORRESOURCE TIM3_CH3 配置或 DMA 资源不可用。
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
 * @details 按 G-R-B、最高位优先发送 144 bit；TIM3 产生 1.2 us 码元，
 *          DMA 逐码元更新高电平宽度，数据前后均保持低电平 300 us。
 * @retval LED_OK 一帧数据发送并锁存完成。
 * @retval LED_ERRORTIMEOUT DMA 未在规定时间内完成传输。
 * @retval LED_ERRORRESOURCE Driver 未初始化、中断已关闭或 TIM/DMA 无法启动。
 * @retval LED_ERRORISR 在中断上下文中调用。
 * @retval LED_ERROR TIM 或 DMA 传输失败。
 * @warning 修改 TIM3 时钟、PSC、ARR 或 CCR 编码值后，必须用逻辑分析仪重新测量波形。
 */
led_driver_status_t bsp_led_driver_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_DRIVER_H */
