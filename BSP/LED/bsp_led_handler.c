/**
 * @file bsp_led_handler.c
 * @brief 相机1~5指示灯和LED7系统状态灯的逻辑映射实现。
 */
#include "bsp_led_handler.h"

/**
 * @brief 业务逻辑灯号到串行物理像素号的映射。
 * @details 数据从 PB0 首先到达 LED7，随后依次经过 LED6、LED5、LED4、LED3、LED2，
 *          因此 LED7 对应像素 0，LED2 对应像素 5。
 */
static const uint8_t s_led_to_pixel[BSP_LED_ID_COUNT] = {
    [BSP_LED_CAMERA_1] = 5U,
    [BSP_LED_CAMERA_2] = 4U,
    [BSP_LED_CAMERA_3] = 3U,
    [BSP_LED_CAMERA_4] = 2U,
    [BSP_LED_CAMERA_5] = 1U,
    [BSP_LED_SYSTEM] = 0U,
};
#if 0
/**
 * @brief 将 LED Driver 层状态转换为 LED Handler 层状态。
 * @param driver_status LED Driver 层返回的状态。
 * @return 与底层错误原因对应的 LED Handler 层状态。
 */
static led_handler_status_t led_handler_convert_driver_status(
    led_driver_status_t driver_status)
{
    led_handler_status_t handler_status = HANDLER_ERROR;

    switch (driver_status)
    {
        case LED_OK:
            handler_status = HANDLER_OK;
            break;

        case LED_ERRORTIMEOUT:
            handler_status = HANDLER_ERRORTIMEOUT;
            break;

        case LED_ERRORRESOURCE:
            handler_status = HANDLER_ERRORRESOURCE;
            break;

        case LED_ERRORPARAMETER:
            handler_status = HANDLER_ERRORPARAMETER;
            break;

        case LED_ERRORNOMEMORY:
            handler_status = HANDLER_ERRORNOMEMORY;
            break;

        case LED_ERRORISR:
            handler_status = HANDLER_ERRORISR;
            break;

        case LED_RESERVED:
            handler_status = HANDLER_RESERVED;
            break;

        case LED_ERROR:
        default:
            handler_status = HANDLER_ERROR;
            break;
    }

    return handler_status;
}
#endif



led_handler_status_t bsp_led_handler_init(void)
{
    led_driver_status_t driver_status = bsp_led_driver_init();
    if (LED_OK != driver_status)
    {
        // return led_handler_convert_driver_status(driver_status);
        return driver_status;
    }

    bsp_led_driver_clear();
    driver_status = bsp_led_driver_commit();
    return driver_status;
    // return led_handler_convert_driver_status(driver_status);
}

led_handler_status_t bsp_led_handler_set(bsp_led_id_t led,
                                          bsp_led_color_t color)
{
    if ((uint32_t)BSP_LED_ID_COUNT <= (uint32_t)led)
    {
        return HANDLER_ERRORPARAMETER;
    }

    const led_driver_status_t driver_status =
        bsp_led_driver_set_pixel(s_led_to_pixel[led], color);//亮度如何设置
    return driver_status;
    // return led_handler_convert_driver_status(driver_status);
}

led_handler_status_t bsp_led_handler_set_all_cameras(bsp_led_color_t color)
{
    // 设置所有相机指示灯--五个指示灯 为指定颜色
    for (bsp_led_id_t led = BSP_LED_CAMERA_1;
         led <= BSP_LED_CAMERA_5;
         led = (bsp_led_id_t)((uint32_t)led + 1U))
    {
        const led_handler_status_t status = bsp_led_handler_set(led, color);
        if (HANDLER_OK != status)
        {
            /* 日志预留：记录指示灯逻辑编号和 status。 */
            return status;
        }
    }

    return HANDLER_OK;
}

void bsp_led_handler_clear(void)
{//清空六颗业务逻辑灯的软件缓存。
// @note 本函数不发送数据，需随后调用 bsp_led_handler_commit()。
    bsp_led_driver_clear();
}

led_handler_status_t bsp_led_handler_commit(void)
{
    const led_driver_status_t driver_status = bsp_led_driver_commit();

    return driver_status;
    // return led_handler_convert_driver_status(driver_status);
}
