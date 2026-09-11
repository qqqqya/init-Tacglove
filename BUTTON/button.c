#include "button.h"

Button_t g_buttons[BUTTON_NUM] = {
    {BUTTON_GPIO_PORT, BUTTON1_PIN, BUTTON_STATE_IDLE, BUTTON_EVENT_NONE, 0, 0, 0, 0},
    {BUTTON_GPIO_PORT, BUTTON2_PIN, BUTTON_STATE_IDLE, BUTTON_EVENT_NONE, 0, 0, 0, 0}
};

/**
 * @brief  初始化两个实体按键输入。
 * @note   代码配置为PA11/PA12下拉输入，同时BUTTON_DOWN定义为低电平。
 *         最终有效电平必须结合板上外部上拉/下拉与按键接法验证。
 */
void button_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    BUTTON_GPIO_CLK();
    
    GPIO_InitStruct.Pin = BUTTON1_PIN | BUTTON2_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(BUTTON_GPIO_PORT, &GPIO_InitStruct);
}

/**
 * @brief  根据当前GPIO电平推进指定按键的消抖、长按和双击状态机。
 * @details 本函数由TIM3每1ms调用一次，因此timer字段的计数单位按当前配置约为1ms。
 *          识别到动作后只写btn->event；ROS发布不在定时器中断里执行。
 * @param  btn_index BUTTON_1或BUTTON_2。
 */
void button_process(Button_Index_t btn_index)
{
    Button_t* btn = &g_buttons[btn_index];
    uint8_t button_level = HAL_GPIO_ReadPin(btn->port, btn->pin);
    
    switch(btn->state)
    {
        case BUTTON_STATE_IDLE:
            btn->has_triggered = 0;
            btn->double_click_timer = 0;
            if(button_level == BUTTON_DOWN)
            {
                btn->state = BUTTON_STATE_DEBOUNCE_PRESS;
                btn->timer = 0;
            }
            break;
            
        case BUTTON_STATE_DEBOUNCE_PRESS:
            btn->timer++;
            if(btn->timer >= BUTTON_DEBOUNCE_TIME)
            {
                if(button_level == BUTTON_DOWN)
                {
                    btn->state = BUTTON_STATE_HOLD;
                    btn->timer = 0;
                    btn->press_total_time = 0;
                }
                else
                {
                    btn->state = BUTTON_STATE_IDLE;
                }
            }
            break;
            
        case BUTTON_STATE_HOLD:
            if(button_level == BUTTON_DOWN)
            {
                btn->press_total_time++;
                if(btn->press_total_time >= BUTTON_LONG_PRESS_TIME && !btn->has_triggered)
                {
                    /* 锁存一次“长按事件”，等待app_micro_ros_task读取并发布。 */
                    btn->event = BUTTON_EVENT_LONG_PRESS;
                    btn->has_triggered = 1;
                }
            }
            else
            {
                btn->state = BUTTON_STATE_DEBOUNCE_RELEASE;
                btn->timer = 0;
            }
            break;
            
        case BUTTON_STATE_DEBOUNCE_RELEASE:
            btn->timer++;
            if(btn->timer >= BUTTON_DEBOUNCE_TIME)
            {
                if(button_level == BUTTON_UP)
                {
                    if(btn->press_total_time >= BUTTON_LONG_PRESS_TIME || btn->has_triggered)
                    {
                        btn->state = BUTTON_STATE_IDLE;
                    }
                    else
                    {
                        btn->state = BUTTON_STATE_WAIT_DOUBLE;
                        btn->double_click_timer = 0;
                    }
                }
                else
                {
                    btn->state = BUTTON_STATE_HOLD;
                }
            }
            break;
        
        case BUTTON_STATE_WAIT_DOUBLE:
            btn->double_click_timer++;
            if(button_level == BUTTON_DOWN)
            {
                btn->timer++;
                if(btn->timer >= BUTTON_DEBOUNCE_TIME && !btn->has_triggered)
                {
                    /* 第二次按下在窗口内成立，锁存“双击事件”。 */
                    btn->event = BUTTON_EVENT_DOUBLE_CLICK;
                    btn->has_triggered = 1;
                    btn->state = BUTTON_STATE_DOUBLE_DONE;
                    btn->timer = 0;
                }
            }
            else if(btn->double_click_timer >= BUTTON_DOUBLE_CLICK_TIME && !btn->has_triggered)
            {
                /* 等待窗口结束仍没有第二击，第一击才被确认为“单击事件”。 */
                btn->event = BUTTON_EVENT_SHORT_PRESS;
                btn->state = BUTTON_STATE_IDLE;
            }
            break;
            
        case BUTTON_STATE_DOUBLE_DONE:
            if(button_level == BUTTON_UP)
            {
                btn->timer++;
                if(btn->timer >= BUTTON_DEBOUNCE_TIME)
                {
                    btn->state = BUTTON_STATE_IDLE;
                    btn->timer = 0;
                }
            }
            break;
            
        default:
            btn->state = BUTTON_STATE_IDLE;
            break;
    }
}

/**
 * @brief  读取指定按键已经识别并锁存的事件。
 * @note   返回值不是实时GPIO state；调用后事件仍保留，必须显式clear。
 */
Button_Event_t button_get_event(Button_Index_t btn_index)
{
    if(btn_index >= BUTTON_NUM)
        return BUTTON_EVENT_NONE;
    
    return g_buttons[btn_index].event;
}

/**
 * @brief  清除一次已处理事件，允许后续动作再次被上报。
 */
void button_clear_event(Button_Index_t btn_index)
{
    if(btn_index < BUTTON_NUM)
    {
        g_buttons[btn_index].event = BUTTON_EVENT_NONE;
    }
}

/**
 * @brief  同时推进两路按键状态机。
 * @note   main.c的TIM3周期回调每1ms调用本函数；这里只识别事件，不调用ROS。
 */
void button_state_machine(void)
{
    button_process(BUTTON_1);
    button_process(BUTTON_2);
}

// void button_demo_task(void *param)
// {
//     printf("BUTTON Task!\r\n");
    
//     while(1)
//     {
//         Button_Event_t event1 = button_get_event(BUTTON_1);
//         if(event1 != BUTTON_EVENT_NONE)
//         {
//             switch(event1)
//             {
//                 case BUTTON_EVENT_SHORT_PRESS:
//                     printf("Button1: short press\r\n");
//                     break;
//                 case BUTTON_EVENT_LONG_PRESS:
//                     printf("Button1: long press\r\n");
//                     break;
//                 case BUTTON_EVENT_DOUBLE_CLICK:
//                     printf("Button1: double click\r\n");
//                     break;
//                 default:
//                     break;
//             }
//             button_clear_event(BUTTON_1);
//         }
        
//         Button_Event_t event2 = button_get_event(BUTTON_2);
//         if(event2 != BUTTON_EVENT_NONE)
//         {
//             switch(event2)
//             {
//                 case BUTTON_EVENT_SHORT_PRESS:
//                     printf("Button2: short press\r\n");
//                     break;
//                 case BUTTON_EVENT_LONG_PRESS:
//                     printf("Button2: long press\r\n");
//                     break;
//                 case BUTTON_EVENT_DOUBLE_CLICK:
//                     printf("Button2: double click\r\n");
//                     break;
//                 default:
//                     break;
//             }
//             button_clear_event(BUTTON_2);
//         }
        
//         vTaskDelay(10);
//     }
// }
