#ifndef __BUTTON_H
#define __BUTTON_H

#include "stm32g4xx_hal.h"
#include "stm32g4xx_hal_gpio.h"

/*
 * 本模块维护两个彼此独立的按键状态机。
 *
 * Button_State_t 是“识别过程的内部状态”，例如消抖、按住、等待第二击；
 * Button_Event_t 是“识别完成后给业务层的一次性结果”，例如长按、单击、双击。
 * app_micro_ros.c 发布的是 event，而不是每一毫秒的GPIO电平或内部state。
 */
#define BUTTON_NUM   2

#define BUTTON1_PIN          GPIO_PIN_11        //初期单按钮是PA0引脚
#define BUTTON2_PIN          GPIO_PIN_12
#define BUTTON_GPIO_PORT    GPIOA
#define BUTTON_GPIO_CLK()   __HAL_RCC_GPIOA_CLK_ENABLE()

#define BUTTON_DOWN     GPIO_PIN_RESET  //GPIO_PIN_SET
#define BUTTON_UP       GPIO_PIN_SET    //GPIO_PIN_RESET

#define BUTTON_DEBOUNCE_TIME     20    //20  // Button debounce time (ms)
#define BUTTON_LONG_PRESS_TIME   400   //400  // Long press determination threshold (ms)
#define BUTTON_DOUBLE_CLICK_TIME 350     // Double-click timeout period (ms)

typedef enum {
    BUTTON_1 = 0,
    BUTTON_2 = 1
} Button_Index_t;

typedef enum {
    BUTTON_STATE_IDLE,                   // 0. Idle (not pressed)
    BUTTON_STATE_DEBOUNCE_PRESS,         // 1. Pressing while in debounce mode: Detected a press, waiting for debounce confirmation
    BUTTON_STATE_HOLD,                   // 2. Press to keep: It has been confirmed as a press action. Monitoring for long press/release.
    BUTTON_STATE_DEBOUNCE_RELEASE,       // 3. Release de-icing in progress: Release detected, waiting for de-icing confirmation
    BUTTON_STATE_WAIT_DOUBLE,            // 4. Wait for double-click: After the first short press and release, wait for the second press.
    BUTTON_STATE_DOUBLE_DONE             // 5. Double-click complete: Trigger the double-click event and wait for the second key release.
} Button_State_t;

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_LONG_PRESS = 1,
    BUTTON_EVENT_SHORT_PRESS = 2,
    BUTTON_EVENT_DOUBLE_CLICK = 3
} Button_Event_t;

typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
    Button_State_t state;
    Button_Event_t event;
    uint16_t timer;
    uint16_t press_total_time;
    uint16_t double_click_timer;
    uint8_t has_triggered;
} Button_t;

extern Button_t g_buttons[BUTTON_NUM];

void button_init(void);                              // 配置PA11/PA12输入。
void button_process(Button_Index_t btn_index);       // 推进一步按键的消抖/手势状态机。
Button_Event_t button_get_event(Button_Index_t btn_index); // 读取已锁存事件，不自动清除。
void button_clear_event(Button_Index_t btn_index);   // ROS发布尝试后清除该次事件。
void button_state_machine(void);                     // TIM3每1ms调用，同时推进两个按键。
void button_demo_task(void *param);

#endif
