/**
 * @file key_task.c
 * @brief 数据采集按键扫描及系统指示动作。
 */
#include "key_task.h"

#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"

#include "bsp_key_handler.h"
#include "led_task.h"


#include "bsp_beep_driver.h"
#define KEY_SCAN_PERIOD_MS 20U

void key_task_entry(void *argument)
{
    const key_handler_status_t init_status = bsp_key_handler_init();
    if (KEY_HANDLER_OK != init_status)
    {
        vTaskSuspend(NULL);
    }

    for (;;)
    {
        bool is_pressed = false;
        // const key_handler_status_t status =
        //     bsp_key_handler_is_pressed(&is_pressed);
        bsp_key_handler_is_pressed(&is_pressed);
        if (true == is_pressed)
        {
            beep_driver_status_t beep_status = bsp_beep_driver_set(true);{
            // if (BEEP_DRIVER_OK != beep_status)
            // {
            //     led_task_show_fault();
            // }
            vTaskDelay(pdMS_TO_TICKS(120u));

            beep_status = bsp_beep_driver_set(false);}//调试没问题但是 直接运行时按键按下没有用  只要改了之后 按键就一直是 一直在120ms的beep
            //好像是返回值的问题
        }   



        // if ((KEY_HANDLER_OK == status) && is_pressed)
        // {
        //     led_task_start_collection();
        // }

        vTaskDelay(pdMS_TO_TICKS(KEY_SCAN_PERIOD_MS));
    }
}
