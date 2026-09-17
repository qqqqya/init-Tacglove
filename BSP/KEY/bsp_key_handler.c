/**
 * @file bsp_key_handler.c
 * @brief PA0低电平有效数据采集按键状态机实现。
 */
#include "bsp_key_handler.h"

#include <stddef.h>
#include <stdint.h>

#include "main.h"

typedef struct
{
    key_state_t state;              //当前按键状态
    key_event_t event;              //当前按键事件
    uint32_t state_start_tick;      //当前状态开始时间
    uint32_t press_start_tick;      //按键按下开始时间
    uint32_t secondary_start_tick;  //双击或松开消抖开始时间
    bool secondary_timing;          //是否正在进行辅助计时
    bool has_triggered;             //是否触发事件
} key_control_t;

static bool s_key_initialized;
static key_control_t s_key;

/**
 * @brief 读取PA0按键是否处于低电平按下状态。
 * @return true表示按下，false表示松开。
 */
static bool key_read_level(void){
    return GPIO_PIN_RESET ==
           HAL_GPIO_ReadPin(key_cap_GPIO_Port, key_cap_Pin);
}

/**
 * @brief 判断从指定时刻起是否已经达到目标时间。
 * @param current_tick 当前HAL Tick，单位ms。
 * @param start_tick 开始HAL Tick，单位ms。
 * @param duration_ms 目标时长，单位ms。
 * @return true表示已到期，false表示尚未到期。
 */
static bool key_time_reached(uint32_t current_tick,
                             uint32_t start_tick,
                             uint32_t duration_ms){
    return duration_ms <= (current_tick - start_tick);
}

key_handler_status_t bsp_key_handler_init(void){
    /** @brief 初始化按键状态机
    状态空闲
    事件无
    定时器0
    */
    s_key.state = KEY_STATE_IDLE;
    s_key.event = KEY_EVENT_NONE;
    s_key.state_start_tick = 0U;
    s_key.press_start_tick = 0U;       //按键按下开始时间
    s_key.secondary_start_tick = 0U;   //双击或松开消抖开始时间
    s_key.secondary_timing = false;    //是否正在进行辅助计时
    s_key.has_triggered = false;       //是否触发事件
    s_key_initialized = true;          //是否初始化
    return KEY_HANDLER_OK;
}

key_handler_status_t bsp_key_handler_process(void){
    if (!s_key_initialized)
    {
        return KEY_HANDLER_ERROR_RESOURCE;
    }

    const bool is_pressed = key_read_level();//读取按键状态
    const uint32_t current_tick = HAL_GetTick();

    switch (s_key.state)
    {//根据当前状态更新按键事件---空闲  去抖-按下消抖 松开消抖---  长按  等待双击
        case KEY_STATE_IDLE:
            s_key.has_triggered = false;
            s_key.secondary_timing = false;
            if (is_pressed)
            {
                s_key.state = KEY_STATE_DEBOUNCE_PRESS;
                s_key.state_start_tick = current_tick;
            }
            break;

        case KEY_STATE_DEBOUNCE_PRESS://按下消抖
            if (!is_pressed)
            {
                s_key.state = KEY_STATE_IDLE;
            }
            else if (key_time_reached(current_tick,
                                      s_key.state_start_tick,
                                      KEY_DEBOUNCE_TIME_MS))
            {
                s_key.state = KEY_STATE_HOLD;
                s_key.press_start_tick = current_tick;

                /* 此时只确认按下；单击必须等松开及双击窗口结束后才能确定。 */
            }
            break;

        case KEY_STATE_HOLD://已确认按下，监测长按或松开
            if (is_pressed)
            {
                if (key_time_reached(current_tick,
                                     s_key.press_start_tick,
                                     KEY_LONG_PRESS_TIME_MS) &&
                    !s_key.has_triggered)
                {
                    /* 持续按下达到门限后立即上报一次长按事件。 */
                    s_key.event = KEY_EVENT_LONG_PRESS;                         //事件长按发生
                    s_key.has_triggered = true;
                }
            }
            else
            {
                s_key.state = KEY_STATE_DEBOUNCE_RELEASE;
                s_key.state_start_tick = current_tick;
            }
            break;

        case KEY_STATE_DEBOUNCE_RELEASE://松开消抖
            if (is_pressed)
            {
                s_key.state = KEY_STATE_HOLD;
            }
            else if (key_time_reached(current_tick,
                                      s_key.state_start_tick,
                                      KEY_DEBOUNCE_TIME_MS))
            {
                if (s_key.has_triggered)
                {
                    s_key.state = KEY_STATE_IDLE;
                }
                else
                {
                    /* 第一次短按结束，进入双击判定窗口。 */
                    s_key.state = KEY_STATE_WAIT_DOUBLE;
                    s_key.state_start_tick = current_tick;
                    s_key.secondary_timing = false;
                }
            }
            break;

        case KEY_STATE_WAIT_DOUBLE: //等待双击 判断单击or双击
            if (is_pressed)
            {
                if (!s_key.secondary_timing)
                {
                    s_key.secondary_timing = true;
                    s_key.secondary_start_tick = current_tick;
                }
                else if (key_time_reached(current_tick,
                                          s_key.secondary_start_tick,
                                          KEY_DEBOUNCE_TIME_MS) &&
                         !s_key.has_triggered)
                {
                    /* 第二次按下消抖成功，双击成立。 */
                    s_key.event = KEY_EVENT_DOUBLE_CLICK;
                    s_key.has_triggered = true;
                    s_key.state = KEY_STATE_DOUBLE_DONE;
                    s_key.secondary_timing = false;         //双击窗口结束，重置辅助计时
                }
            }
            else
            {//单击窗口超时且没有第二次按下，单击成立
                s_key.secondary_timing = false;
                if (key_time_reached(current_tick,s_key.state_start_tick,KEY_DOUBLE_CLICK_TIME_MS) 
                        &&
                    !s_key.has_triggered)
                {
                    /* 双击窗口超时且没有第二次按下，单击成立。 */
                    s_key.event = KEY_EVENT_SHORT_PRESS;
                    s_key.state = KEY_STATE_IDLE;
                }
            }
            break;

        case KEY_STATE_DOUBLE_DONE://双击成立后等待第二次松开。
            if (is_pressed)
            {
                s_key.secondary_timing = false;
            }
            else if (!s_key.secondary_timing)
            {
                s_key.secondary_timing = true;
                s_key.secondary_start_tick = current_tick;
            }
            else if (key_time_reached(current_tick,
                                      s_key.secondary_start_tick,
                                      KEY_DEBOUNCE_TIME_MS))//按键松开去抖时间20ms
            {
                s_key.state = KEY_STATE_IDLE;
                s_key.secondary_timing = false;
            }
            break;

        default:
            s_key.state = KEY_STATE_IDLE;
            s_key.secondary_timing = false;
            break;
    }

    return KEY_HANDLER_OK;
}

key_handler_status_t bsp_key_handler_get_event(key_event_t *event){
    if (NULL == event)
    {
        return KEY_HANDLER_ERROR_PARAMETER;
    }

    if (!s_key_initialized)
    {
        return KEY_HANDLER_ERROR_RESOURCE;
    }

    *event = s_key.event;   //获取按键事件 in struct key_handler_t
    return KEY_HANDLER_OK;
}

key_handler_status_t bsp_key_handler_clear_event(void){
    if (!s_key_initialized)
    {
        return KEY_HANDLER_ERROR_RESOURCE;
    }

    s_key.event = KEY_EVENT_NONE;
    return KEY_HANDLER_OK;
}
