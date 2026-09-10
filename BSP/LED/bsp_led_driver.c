/**
 * @file bsp_led_driver.c
 * @brief STM32G474 通过 PB0 向六颗 SK6805 发送单总线数据。
 * @details
 * 每颗灯接收 24 bit，顺序为 G7..G0、R7..R0、B7..B0。
 * 位宽等待使用 DWT->CYCCNT，以 SystemCoreClock 计算目标周期数，不使用
 * 函数级 optimize 属性。逻辑分析仪实测波形始终是最终校准依据。
 */
#include "bsp_led_driver.h"

#include <stdbool.h>

#include "main.h"

/** @brief 逻辑 0 的目标高电平时间，单位 ns。 */
#define SK6805_T0H_NS   300UL

/** @brief 逻辑 1 的目标高电平时间，单位 ns。 */
#define SK6805_T1H_NS   900UL

/** @brief 单个码元的目标总周期，单位 ns。 */
#define SK6805_BIT_NS  1200UL

/** @brief 一帧结束后的目标复位低电平时间，单位 us。 */
#define SK6805_RESET_US 300UL

/**
 * @brief 六颗灯的软件发送缓存。
 * @details 第一维是串行物理像素序号，第二维固定为 G、R、B。
 */
static uint8_t s_pixels[BSP_LED_PIXEL_COUNT][3];

static uint32_t s_t0h_cycles;   /**< 逻辑 0 高电平对应的 CPU 周期数。 */
static uint32_t s_t1h_cycles;   /**< 逻辑 1 高电平对应的 CPU 周期数。 */
static uint32_t s_bit_cycles;   /**< 单个码元对应的 CPU 周期数。 */
static uint32_t s_reset_cycles; /**< 帧锁存低电平对应的 CPU 周期数。 */
static bool s_led_initialized;  /**< Driver 是否已经成功初始化。 */

/**
 * @brief 将纳秒时间向上换算为当前主频的 CPU 周期数。
 * @param nanoseconds 目标时间，单位 ns。
 * @return 不小于目标时间的 CPU 周期数。
 */
static uint32_t sk6805_ns_to_cycles(uint32_t nanoseconds)
{
    return (uint32_t)((((uint64_t)SystemCoreClock * nanoseconds) +
                       999999999ULL) /
                      1000000000ULL);
}

/**
 * @brief 将微秒时间向上换算为当前主频的 CPU 周期数。
 * @param microseconds 目标时间，单位 us。
 * @return 不小于目标时间的 CPU 周期数。
 */
static uint32_t sk6805_us_to_cycles(uint32_t microseconds)
{
    return (uint32_t)((((uint64_t)SystemCoreClock * microseconds) +
                       999999ULL) /
                      1000000ULL);
}

/**
 * @brief 从指定起始计数开始等待给定数量的 CPU 周期。
 * @param start DWT 周期计数器的起始值。
 * @param cycles 至少需要经过的 CPU 周期数。
 */
static inline void sk6805_wait_cycles(uint32_t start, uint32_t cycles)
{
    while ((uint32_t)(DWT->CYCCNT - start) < cycles)
    {
        __NOP();
    }
}

/**
 * @brief 发送一个 SK6805 数据位。
 * @param one true 发送逻辑 1，false 发送逻辑 0。
 * @warning 仅允许在已经关闭抢占中断的完整帧发送区间内调用。
 */
static inline void sk6805_write_bit(bool one)
{
    rgb_ctrl_GPIO_Port->BSRR = rgb_ctrl_Pin;
    const uint32_t high_start = DWT->CYCCNT;

    sk6805_wait_cycles(high_start, one ? s_t1h_cycles : s_t0h_cycles);
    rgb_ctrl_GPIO_Port->BSRR = (uint32_t)rgb_ctrl_Pin << 16U;
    sk6805_wait_cycles(high_start, s_bit_cycles);
}

/**
 * @brief 按最高位优先顺序发送一个字节。
 * @param value 要发送的 8 bit 数据。
 */
static void sk6805_write_byte(uint8_t value)
{
    for (uint8_t bit = 0U; bit < 8U; ++bit)
    {
        sk6805_write_bit((value & 0x80U) != 0U);
        value <<= 1U;
    }
}

led_driver_status_t bsp_led_driver_init(void)
{
    if (0U == SystemCoreClock)
    {
        return LED_ERRORRESOURCE;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if (0U == (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk))
    {
        return LED_ERRORRESOURCE;
    }

    s_t0h_cycles = sk6805_ns_to_cycles(SK6805_T0H_NS);
    s_t1h_cycles = sk6805_ns_to_cycles(SK6805_T1H_NS);
    s_bit_cycles = sk6805_ns_to_cycles(SK6805_BIT_NS);
    s_reset_cycles = sk6805_us_to_cycles(SK6805_RESET_US);

    HAL_GPIO_WritePin(rgb_ctrl_GPIO_Port, rgb_ctrl_Pin, GPIO_PIN_RESET);
    bsp_led_driver_clear();
    s_led_initialized = true;
    return LED_OK;
}

led_driver_status_t bsp_led_driver_set_pixel(uint8_t pixel_index,
                                              bsp_led_color_t color)
{
    if (BSP_LED_PIXEL_COUNT <= pixel_index)
    {
        return LED_ERRORPARAMETER;
    }

    /* 协议线序不是 RGB，缓存时必须转换为 G-R-B。 */
    s_pixels[pixel_index][0] = color.green;
    s_pixels[pixel_index][1] = color.red;
    s_pixels[pixel_index][2] = color.blue;
    return LED_OK;
}

void bsp_led_driver_clear(void)
{
    for (uint8_t pixel = 0U; pixel < BSP_LED_PIXEL_COUNT; ++pixel)
    {
        s_pixels[pixel][0] = 0U;
        s_pixels[pixel][1] = 0U;
        s_pixels[pixel][2] = 0U;
    }
}

led_driver_status_t bsp_led_driver_commit(void)
{
    if (!s_led_initialized)
    {
        return LED_ERRORRESOURCE;
    }

    /*
     * 保存调用前的中断状态。六颗灯共 144 bit，短暂关中断可避免 FreeRTOS
     * tick 或其他 ISR 拉长数据脉冲；一帧数据发完后立即恢复原中断状态。
     */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    for (uint8_t pixel = 0U; pixel < BSP_LED_PIXEL_COUNT; ++pixel)
    {
        sk6805_write_byte(s_pixels[pixel][0]);
        sk6805_write_byte(s_pixels[pixel][1]);
        sk6805_write_byte(s_pixels[pixel][2]);
    }

    rgb_ctrl_GPIO_Port->BSRR = (uint32_t)rgb_ctrl_Pin << 16U;
    __set_PRIMASK(primask);

    /* 复位阶段只要求保持低电平，中断造成的额外延长不会破坏锁存。 */
    const uint32_t reset_start = DWT->CYCCNT;
    sk6805_wait_cycles(reset_start, s_reset_cycles);
    return LED_OK;
}
