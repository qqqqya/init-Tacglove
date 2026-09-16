/**
 * @file led_task.c
 * @brief LED上电自检、采集状态和按键识别反馈任务。
 */
#include "led_task.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "bsp_beep_driver.h"
#include "bsp_led_handler.h"

/**
 * @brief LED颜色定义。
 */
static const bsp_led_color_t COLOR_OFF = {0U, 0U, 0U};
static const bsp_led_color_t COLOR_GREEN = {0U, LED_BRIGHTNESS, 0U};
static const bsp_led_color_t COLOR_RED = {LED_BRIGHTNESS, 0U, 0U};
static const bsp_led_color_t COLOR_BLUE = {LED_BRIGHTNESS, 0U, LED_BRIGHTNESS};

// static const bsp_led_color_t COLOR_PURPLE = {LED_BRIGHTNESS, 0U, LED_BRIGHTNESS};

typedef struct
{
    bool enabled;
    uint8_t led_mode[LED_TASK_LED_COUNT]; //led_mode 非空数组，按LED2、LED3、LED4、LED5、LED6、LED7排列。
    uint8_t beep_mode;
} led_task_cmd_t;

static volatile bool s_led_ready;
static volatile uint8_t s_system_state = LED_TASK_SYSTEM_SELF_TEST;// 系统状态
static QueueHandle_t s_led_cmd_queue;

/**
 * @brief 将ROS灯效模式转换为当前闪烁相位对应的颜色。
 * @param mode LED_TASK_MODE_xxx模式。
 * @param blink_on true表示闪烁灯当前处于亮相位。
 * @param[out] color 非空颜色输出指针。
 * @retval TASK_OK 转换成功。
 * @retval TASK_ERROR_PARAMETER mode非法或color为空。
 */
static task_status_t led_task_mode_to_color(uint8_t mode,
                                            bool blink_on,
                                            bsp_led_color_t *color){
    if (NULL == color)
    {
        return TASK_ERROR_PARAMETER;
    }

    switch (mode)//根据数组里的  mode序号进行相应的led动作
    {
        case LED_TASK_MODE_OFF:
            *color = COLOR_OFF;
            break;

        case LED_TASK_MODE_GREEN_SOLID:
            *color = COLOR_GREEN;
            break;

        case LED_TASK_MODE_GREEN_BLINK:
            *color = blink_on ? COLOR_GREEN : COLOR_OFF;
            break;

        case LED_TASK_MODE_RED_SOLID:
            *color = COLOR_RED;
            break;

        case LED_TASK_MODE_BLUE_BLINK:
            *color = blink_on ? COLOR_BLUE : COLOR_OFF;
            break;

        case LED_TASK_MODE_BLUE_SOLID:
            *color = COLOR_BLUE;
            break;

        default:
            return TASK_ERROR_PARAMETER;
    }

    return TASK_OK;
}

/**
 * @brief 判断远程cmd中是否至少包含一个闪烁模式。
 * @param cmd 非空远程cmd指针。
 * @return true表示需要周期切换闪烁相位。
 */
static bool led_task_cmd_has_blink(const led_task_cmd_t *cmd){
    for (uint8_t index = 0U; index < LED_TASK_LED_COUNT; ++index)
    {
        if ((LED_TASK_MODE_GREEN_BLINK == cmd->led_mode[index]) ||
            (LED_TASK_MODE_BLUE_BLINK == cmd->led_mode[index]))
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief 显示远程cmd在指定闪烁相位下的完整六灯状态。
 * @param cmd 非空远程cmd指针。
 * @param blink_on true表示闪烁灯当前处于亮相位。
 * @return LED Handler层状态。
 */
static led_handler_status_t led_task_show_remote_cmd(
    const led_task_cmd_t *cmd,
    bool blink_on){
    bsp_led_handler_clear();// 清空LED缓存数据层

    for (uint8_t index = 0U; index < LED_TASK_LED_COUNT; ++index)
    {
        bsp_led_color_t color = COLOR_OFF;
        const task_status_t convert_status = led_task_mode_to_color(
            cmd->led_mode[index], blink_on, &color);// 转换ROS灯效模式为颜色 根据cmd中的ledmode
        if (TASK_OK != convert_status)
        {
            return HANDLER_ERRORPARAMETER;
        }

        const led_handler_status_t status = bsp_led_handler_set(
            (bsp_led_id_t)index, color);//color会被led_task_mode_to_color修改 然后修改一个业务逻辑灯的软件缓存颜色。
        if (HANDLER_OK != status)
        {
            return status;
        }
    }// 遍历所有LED 循环六次

    return bsp_led_handler_commit();// 提交所有LED缓存数据层的颜色到硬件层
}

/**
 * @brief 同时设置五颗相机灯和LED7系统状态灯。
 * @param camera_color LED2~LED6的目标颜色。
 * @param system_color LED7的目标颜色。
 * @return LED Handler层状态。
 */
static led_handler_status_t led_task_show_state(
    bsp_led_color_t camera_color,
    bsp_led_color_t system_color){
    bsp_led_handler_clear();

    led_handler_status_t status =
        bsp_led_handler_set_all_cameras(camera_color);
    if (HANDLER_OK != status)
    {
        return status;
    }

    status = bsp_led_handler_set(BSP_LED_SYSTEM, system_color);
    if (HANDLER_OK != status)
    {
        return status;
    }

    return bsp_led_handler_commit();
}

/**
 * @brief 执行LED2~LED6绿色闪烁的上电自检灯效。
 * @return LED Handler层状态。
 * @note LED7在整个上电自检过程中保持熄灭。
 */
static led_handler_status_t led_task_run_self_test(void){
    for (uint8_t blink = 0U; blink < SELF_TEST_BLINK_COUNT; ++blink)
    {
        led_handler_status_t status =
            led_task_show_state(COLOR_GREEN, COLOR_OFF);//这个green不对 亮度在哪里调？？--好像里面也包含 但是时序好像确实不对
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(SELF_TEST_HALF_PERIOD_MS));

        status = led_task_show_state(COLOR_OFF, COLOR_OFF);
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(SELF_TEST_HALF_PERIOD_MS));
    }

    return HANDLER_OK;
}

/**
 * @brief 让LED7蓝色闪烁表示进入数据采集准备状态。
 * @return LED Handler层状态。
 */
static led_handler_status_t led_task_run_prepare(void){
    for (uint8_t blink = 0U; blink < PREPARE_BLINK_COUNT; ++blink)
    {
        led_handler_status_t status =
            led_task_show_state(COLOR_GREEN, COLOR_BLUE);
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(PREPARE_HALF_PERIOD_MS));

        status = led_task_show_state(COLOR_GREEN, COLOR_OFF);
        if (HANDLER_OK != status)
        {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(PREPARE_HALF_PERIOD_MS));
    }

    return HANDLER_OK;
}

/**
 * @brief 阻塞LED任务完成一次蜂鸣，按键任务仍可继续扫描。
 * @param duration_ms 蜂鸣持续时间，单位ms，必须大于0。
 * @return 蜂鸣器Driver层状态。
 */
static beep_driver_status_t led_task_beep(uint32_t duration_ms){
    beep_driver_status_t status = bsp_beep_driver_set(true);
    if (BEEP_DRIVER_OK != status)
    {
        return status;
    }

    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return bsp_beep_driver_set(false);
}

/**
 * @brief 执行PC下发的蜂鸣器模式。
 * @param beep_mode LED_TASK_BEEP_xxx蜂鸣器模式。
 * @return 蜂鸣器Driver层状态。
 * @note MODE_BEEPING沿用参考工程语义：150 ms鸣叫两次，中间静音250 ms。
 */
static beep_driver_status_t led_task_apply_remote_beep(uint8_t beep_mode){
    switch (beep_mode)
    {
        case LED_TASK_BEEP_OFF:
            return bsp_beep_driver_set(false);

        case LED_TASK_BEEP_SHORT:
            return led_task_beep(REMOTE_SHORT_BEEP_TIME_MS);

        case LED_TASK_BEEP_LONG:
            return led_task_beep(REMOTE_LONG_BEEP_TIME_MS);

        case LED_TASK_BEEPING:
            for (uint8_t count = 0U; count < 2U; ++count)
            {
                const beep_driver_status_t status =
                    led_task_beep(REMOTE_BEEPING_TIME_MS);
                if (BEEP_DRIVER_OK != status)
                {
                    return status;
                }

                if (0U == count)
                {
                    vTaskDelay(pdMS_TO_TICKS(
                        REMOTE_BEEPING_INTERVAL_MS));
                }
            }
            return BEEP_DRIVER_OK;

        default:
            return BEEP_DRIVER_ERROR_PARAMETER;
    }
}

/**
 * @brief 临时反转LED7，用于观察长按或双击是否被正确识别。
 * @param collecting true表示当前处于采集中状态。
 * @param hold_ms 反转状态保持时间，单位ms。
 * @return LED Handler层状态。
 */
static led_handler_status_t led_task_show_gesture_feedback(
    bool collecting,
    uint32_t hold_ms){
    const bsp_led_color_t feedback_color = collecting ? COLOR_OFF : COLOR_BLUE;
    led_handler_status_t status =
        led_task_show_state(COLOR_GREEN, feedback_color);
    if (HANDLER_OK != status)
    {
        return status;
    }

    vTaskDelay(pdMS_TO_TICKS(hold_ms));
    return led_task_show_state(COLOR_GREEN,
                               collecting ? COLOR_BLUE : COLOR_OFF);
}

/**
 * @brief 取出并清空按键任务发送给当前LED任务的通知动作位。
 * @return 待处理动作位掩码。
 */
static uint32_t led_task_take_notified_actions(void){
    uint32_t actions = 0U;
    (void)xTaskNotifyWait(0U,
                          LED_TASK_NOTIFY_ALL,
                          &actions,
                          0U);
    return actions;
}

/**
 * @brief 使用FreeRTOS Task Notification向LED任务设置一个按键动作位。
 * @param action LED_TASK_NOTIFY_xxx动作位。
 */
static void led_task_notify_action(uint32_t action){
    if ((!s_led_ready) || (NULL == g_led_task_handle))
    {
        return;
    }

    (void)xTaskNotify(g_led_task_handle, action, eSetBits);
}

/**
 * @brief 显示六灯红色常亮故障状态并停止正常流程。
 */
static void led_task_show_fault(void){
    s_system_state = LED_TASK_SYSTEM_ERROR;
    (void)bsp_beep_driver_set(false);
    (void)led_task_show_state(COLOR_RED, COLOR_RED);

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

task_status_t led_task_resources_init(void){
    if (NULL != s_led_cmd_queue)
    {
        return TASK_OK;
    }

    //创建邮箱 邮箱大小为1 用于存储LED任务的命令
    s_led_cmd_queue = xQueueCreate(1U, sizeof(led_task_cmd_t));
    if (NULL == s_led_cmd_queue)
    {
        return TASK_ERROR_NO_MEMORY;
    }

    return TASK_OK;
}

task_status_t led_task_submit_cmd(
    const uint8_t led_mode[LED_TASK_LED_COUNT],
    uint8_t beep_mode){
    if (NULL == led_mode)
    {
        return TASK_ERROR_PARAMETER;
    }

    if (NULL == s_led_cmd_queue)
    {
        return TASK_ERROR_RESOURCE;
    }

    if (LED_TASK_BEEPING < beep_mode)
    {
        return TASK_ERROR_PARAMETER;
    }

    led_task_cmd_t cmd = {.enabled = true};
    for (uint8_t index = 0U; index < LED_TASK_LED_COUNT; ++index)
    {
        if (LED_TASK_MODE_BLUE_SOLID < led_mode[index])
        {
            return TASK_ERROR_PARAMETER;
        }

        cmd.led_mode[index] = led_mode[index];
    }
    cmd.beep_mode = beep_mode;

    if (pdPASS != xQueueOverwrite(s_led_cmd_queue, &cmd))
    {
        return TASK_ERROR;
    }

    return TASK_OK;
}

task_status_t led_task_release_remote_control(void){
    if (NULL == s_led_cmd_queue)
    {
        return TASK_ERROR_RESOURCE;
    }

    const led_task_cmd_t cmd = {.enabled = false};
    if (pdPASS != xQueueOverwrite(s_led_cmd_queue, &cmd))
    {
        return TASK_ERROR;
    }

    return TASK_OK;
}

uint8_t led_task_get_system_state(void){
    return s_system_state;
}


void led_task_on_short_press(void){
    led_task_notify_action(LED_TASK_NOTIFY_SHORT_PRESS);
}/**这三个函数  分别对应短按 长按 双击按键
led申请 */

void led_task_on_long_press(void){
    led_task_notify_action(LED_TASK_NOTIFY_LONG_PRESS);
}

void led_task_on_double_click(void){
    led_task_notify_action(LED_TASK_NOTIFY_DOUBLE_CLICK);
}

void led_task_entry(void *argument){
    s_system_state = LED_TASK_SYSTEM_SELF_TEST;

    //初始化LED Handler层 内部包含driver层init
    led_handler_status_t led_status = bsp_led_handler_init();
    if (HANDLER_OK != led_status)
    {
        led_task_show_fault();
    }

    //初始化蜂鸣器
       beep_driver_status_t beep_status = bsp_beep_driver_init();
    if (BEEP_DRIVER_OK != beep_status)
    {
        led_task_show_fault();
    }
    //上电自检LED 五个绿灯闪烁三次
    led_status = led_task_run_self_test();
    if (HANDLER_OK != led_status)
    {
        led_task_show_fault();
    }
    //自检之后常亮绿色
    led_status = led_task_show_state(COLOR_GREEN, COLOR_OFF);
    if (HANDLER_OK != led_status)
    {
        led_task_show_fault();
    }

    bool collecting = false;        //是否正在采集LED颜色
    bool remote_active = false;     //是否正在接收远程cmd
    bool remote_blink_on = true;
    led_task_cmd_t remote_cmd = {0};
    TickType_t next_remote_blink = xTaskGetTickCount();

    s_system_state = LED_TASK_SYSTEM_IDLE;
    s_led_ready = true;

    for (;;)
    {
        led_task_cmd_t received_cmd = {0};
        if (pdPASS == xQueueReceive(s_led_cmd_queue, &received_cmd, 0U))
        { //邮箱中存的led cmd 有数据
            remote_active = received_cmd.enabled;
            if (remote_active)
            {
                remote_cmd = received_cmd;
                remote_blink_on = true;
                next_remote_blink = xTaskGetTickCount() +
                                    pdMS_TO_TICKS(
                                        REMOTE_BLINK_HALF_PERIOD_MS);
                led_status = led_task_show_remote_cmd(&remote_cmd,
                                                      remote_blink_on);// 显示远程cmd设置的LED颜色
                beep_status = led_task_apply_remote_beep(
                    remote_cmd.beep_mode);
            }
            else
            {
                led_status = led_task_show_state(
                    COLOR_GREEN,
                    collecting ? COLOR_BLUE : COLOR_OFF);// 显示本地cmd设置的LED颜色  绿色 没采集 blueoff
                beep_status = bsp_beep_driver_set(false);
            }

            if (HANDLER_OK != led_status)
            {
                led_task_show_fault();
            }
            if (BEEP_DRIVER_OK != beep_status)
            {
                led_task_show_fault();
            }
        }

        const uint32_t actions = led_task_take_notified_actions();

        if (0U != (actions & LED_TASK_NOTIFY_SHORT_PRESS))// 短按按键
        {
            if (!collecting)
            {// 空闲状态 切换到采集状态
                s_system_state = LED_TASK_SYSTEM_PREPARING;

                if (!remote_active)
                {//没有远程cmd 正常的采集LED颜色 即led7闪烁
                    led_status = led_task_run_prepare();
                    if (HANDLER_OK != led_status)
                    {
                        led_task_show_fault();
                    }
                }

                beep_status = led_task_beep(SHORT_BEEP_TIME_MS);
                if (BEEP_DRIVER_OK != beep_status)
                {
                    led_task_show_fault();
                }

                if (!remote_active)
                {//led7常亮蓝色 其他led常亮绿色
                    led_status = led_task_show_state(COLOR_GREEN,
                                                     COLOR_BLUE);
                }
                collecting = true;
                //  切换到采集状态
                s_system_state = LED_TASK_SYSTEM_COLLECTING;
            }
            else
            {// 已采集 从采集状态切换到空闲状态
                beep_status = led_task_beep(SHORT_BEEP_TIME_MS);
                if (BEEP_DRIVER_OK != beep_status)
                {
                    led_task_show_fault();
                }

                if (!remote_active)
                {//led7 off 其他led常亮蓝色
                    led_status = led_task_show_state(COLOR_GREEN, COLOR_OFF);
                }
                collecting = false;
                s_system_state = LED_TASK_SYSTEM_IDLE;// 采集状态 切换到空闲状态
            }

            if ((!remote_active) && (HANDLER_OK != led_status))
            {
                led_task_show_fault();
            }
        }

        if (0U != (actions & LED_TASK_NOTIFY_LONG_PRESS))
        {// 长按按键
            if (!remote_active)
            {
                led_status = led_task_show_gesture_feedback(
                    collecting,
                    LONG_FEEDBACK_TIME_MS);
                if (HANDLER_OK != led_status)
                {
                    led_task_show_fault();
                }
            }

            //beep 长鸣
            beep_status = led_task_beep(LONG_BEEP_TIME_MS);
            if (BEEP_DRIVER_OK != beep_status)
            {
                led_task_show_fault();
            }
        }

        if (0U != (actions & LED_TASK_NOTIFY_DOUBLE_CLICK))
        {// 双击按键
            for (uint8_t count = 0U; count < 2U; ++count)
            {
                if (!remote_active)
                {
                    led_status = led_task_show_gesture_feedback(
                        collecting,
                        DOUBLE_FEEDBACK_TIME_MS);
                    if (HANDLER_OK != led_status)
                    {
                        led_task_show_fault();
                    }
                }

                beep_status = led_task_beep(DOUBLE_BEEP_TIME_MS);
                if (BEEP_DRIVER_OK != beep_status)
                {
                    led_task_show_fault();
                }

                if (0U == count)
                {
                    vTaskDelay(pdMS_TO_TICKS(DOUBLE_BEEP_INTERVAL_MS));
                }
            }
        }

        const TickType_t current_tick = xTaskGetTickCount();
        if (remote_active &&                                         // 远程命令有效
            led_task_cmd_has_blink(&remote_cmd) &&              // 远程命令有闪烁模式
            ((int32_t)(current_tick - next_remote_blink) >= 0))     // 如果当前时间大于等于下一个闪烁时间
        {
            remote_blink_on = !remote_blink_on;
            next_remote_blink = current_tick +
                                pdMS_TO_TICKS(
                                    REMOTE_BLINK_HALF_PERIOD_MS);

            //远程cmd在指定闪烁相位下的完整六灯状态
            led_status = led_task_show_remote_cmd(&remote_cmd,
                                                  remote_blink_on);
            if (HANDLER_OK != led_status)
            {
                led_task_show_fault();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LED_TASK_PERIOD_MS));
    }
}
