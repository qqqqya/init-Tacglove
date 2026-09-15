/**
 * @file bsp_led_driver.c
 * @brief STM32G474 通过 TIM3_CH3 PWM 和 DMA 向六颗 SK6805 发送数据。
 * @details
 * 每颗灯接收 24 bit，顺序为 G7..G0、R7..R0、B7..B0。
 * TIM3 固定码元周期，DMA 逐码元更新 CCR3 产生不同的高电平宽度，
 * 使传输时序不再依赖 CPU 主频和编译优化等级。
 */
#include "bsp_led_driver.h"

#include <stdbool.h>

#include "tim.h"

/** @brief 每颗 SK6805 的数据字节数，固定为 G、R、B 三字节。 */
#define SK6805_BYTES_PER_PIXEL 3U

/** @brief 一个字节内的数据位数。 */
#define SK6805_BITS_PER_BYTE 8U

/** @brief 六颗灯一帧的数据码元数。--bit数 6*3*8 --144*/
#define SK6805_DATA_SLOT_COUNT \
    (BSP_LED_PIXEL_COUNT * SK6805_BYTES_PER_PIXEL * SK6805_BITS_PER_BYTE)

/** @brief 1.2 us 码元周期中，逻辑 0 的 300 ns 高电平计数值。 */
#define SK6805_PWM_ZERO_TICKS 51U     //170M 时钟下对应时钟周期为5.88 ns   51 个时钟周期为 300 ns

/** @brief 1.2 us 码元周期中，逻辑 1 的 900 ns 高电平计数值。 */
#define SK6805_PWM_ONE_TICKS 153U

/** @brief 256 个零占空比周期为 DMA 预装留出裕量，确保复位低电平不小于 300 us。 */
#define SK6805_RESET_SLOT_COUNT 256U    //有些版本300us  有些版本80us--------256*1.2us

/** @brief DMA 帧包含前置复位、像素数据和后置复位。--656个时钟周期   2u原始  400-1u也是正常的 */
#define SK6805_DMA_SLOT_COUNT \
    ((1U * SK6805_RESET_SLOT_COUNT) + SK6805_DATA_SLOT_COUNT)//前一个的后置复位，实际上可以当做后一个的前置复位--1u的现象也是正常的 

/** @brief PWM DMA 传输完成等待超时，单位 ms。 */
#define SK6805_TRANSFER_TIMEOUT_MS 5U

/**
 * @brief 六颗灯的软件发送缓存。
 * @details 第一维是串行物理像素序号，第二维固定为 G、R、B。
 */
static uint8_t s_pixels[BSP_LED_PIXEL_COUNT][3];

/** @brief TIM3_CH3 的 DMA 比较值帧缓存。 */
static uint32_t s_pwm_dma_buffer[SK6805_DMA_SLOT_COUNT];

static volatile bool s_transfer_complete; /**< DMA 一帧传输完成标志。 */
static volatile bool s_transfer_error;    /**< DMA 或 TIM 发生错误的标志。 */
static bool s_led_initialized;            /**< Driver 是否已经成功初始化。 */

/**
 * @brief 将一个字节按最高位优先编码为八个 PWM 比较值。设置的是s_pwm_dma_buffer中的数据
 * @param value 待编码的字节。
 * @param slot_index DMA 缓存写入位置，调用后向后移动八个码元。
 */
static void sk6805_encode_byte(uint8_t value, uint16_t *slot_index)
{
    for (uint8_t bit = 0U; bit < SK6805_BITS_PER_BYTE; ++bit)
    {
        s_pwm_dma_buffer[*slot_index] =
            (0U != (value & 0x80U)) ? SK6805_PWM_ONE_TICKS      //bit=1？ 高电平900ns 153个时钟周期
                                    : SK6805_PWM_ZERO_TICKS;    //bit=0？ 低电平300ns 51个时钟周期
        ++(*slot_index);
        value <<= 1U;
    }
}

/**
 * @brief 根据当前六颗灯的 GRB 缓存构建完整 PWM DMA 帧。
 * @details 数据前后均保留 300 us 零占空比时间，用于帧边界复位和锁存。
 */
static void sk6805_build_dma_frame(void)
{
    //清空DMA缓存
    for (uint16_t slot = 0U; slot < SK6805_DMA_SLOT_COUNT; ++slot)
    {
        s_pwm_dma_buffer[slot] = 0U;
    }

    //先跳过256cycle的复位周期 已经设置为0了
    uint16_t slot_index = SK6805_RESET_SLOT_COUNT;
    for (uint8_t pixel = 0U; pixel < BSP_LED_PIXEL_COUNT; ++pixel)
    {   //设置6led的GRB值 缓存信息数据
        sk6805_encode_byte(s_pixels[pixel][0], &slot_index);//设置G通道比较值 8位
        sk6805_encode_byte(s_pixels[pixel][1], &slot_index);
        sk6805_encode_byte(s_pixels[pixel][2], &slot_index);
    }
}

/**
 * @brief 将 TIM3_CH3 恢复为停止且低电平的安全状态。
 * @return HAL 停止 PWM DMA 的结果。
 */
static HAL_StatusTypeDef sk6805_stop_transfer(void)
{
    const HAL_StatusTypeDef status =
        HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_3);

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    htim3.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE | TIM_FLAG_CC3);
    return status;
}

led_driver_status_t bsp_led_driver_init(void)
{
    if ((TIM3 != htim3.Instance) ||
        (0U != htim3.Init.Prescaler) ||
        (203U != htim3.Init.Period) ||
        (NULL == htim3.hdma[TIM_DMA_ID_CC3]))
    {
        return LED_ERRORRESOURCE;
    }

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    htim3.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE | TIM_FLAG_CC3);
    bsp_led_driver_clear();
    s_transfer_complete = false;
    s_transfer_error = false;
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
    {//将所有灯  所有像素的GRB值设置为0 清除所有像素信息
        s_pixels[pixel][0] = 0U;
        s_pixels[pixel][1] = 0U;
        s_pixels[pixel][2] = 0U;
    }
}

led_driver_status_t bsp_led_driver_commit(void){
    //将完整的六像素缓存提交到 SK6805 灯链并触发锁存。
    if (!s_led_initialized)
    {
        return LED_ERRORRESOURCE;
    }

    if (0U != __get_IPSR())
    {
        return LED_ERRORISR;
    }

    if (0U != __get_PRIMASK())
    {
        return LED_ERRORRESOURCE;
    }

    sk6805_build_dma_frame();//设置reset + data数据缓存 buffer
    s_transfer_complete = false;
    s_transfer_error = false;

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    htim3.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE | TIM_FLAG_CC3);

    //根据build_dma_frame()设置的s_pwm_dma_buffer数据，启动PWM DMA传输
    const HAL_StatusTypeDef start_status =
        HAL_TIM_PWM_Start_DMA(&htim3,
                              TIM_CHANNEL_3,
                              s_pwm_dma_buffer,
                              SK6805_DMA_SLOT_COUNT);
    if (HAL_OK != start_status)
    {
        (void)sk6805_stop_transfer();
        return LED_ERRORRESOURCE;
    }

    //等待传输完成或超时
    const uint32_t start_tick = HAL_GetTick();
    while ((!s_transfer_complete) && (!s_transfer_error))
    {
        if (SK6805_TRANSFER_TIMEOUT_MS <= (HAL_GetTick() - start_tick))
        {
            (void)sk6805_stop_transfer();
            return LED_ERRORTIMEOUT;
        }
    }

    //停止传输
    const HAL_StatusTypeDef stop_status = sk6805_stop_transfer();
    if (s_transfer_error || (HAL_OK != stop_status))
    {
        return LED_ERROR;
    }

    return LED_OK;
}

/**
 * @brief TIM PWM DMA 一帧传输完成回调。
 * @param htim 触发回调的 TIM Handle。
 */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if ((TIM3 == htim->Instance) &&
        (HAL_TIM_ACTIVE_CHANNEL_3 == htim->Channel))
    {
        s_transfer_complete = true;
    }
}

/**
 * @brief TIM DMA 传输错误回调。
 * @param htim 发生错误的 TIM Handle。
 */
void HAL_TIM_ErrorCallback(TIM_HandleTypeDef *htim)
{
    if (TIM3 == htim->Instance)
    {
        s_transfer_error = true;
    }
}
