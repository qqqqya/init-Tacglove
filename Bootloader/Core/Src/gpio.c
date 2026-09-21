/**
 * @file gpio.c
 * @brief Bootloader使用的GPIO初始化。
 */
#include "gpio.h"

#include "main.h"

/**
 * @brief 初始化PA11按键输入；当前阶段仅保持与硬件和IOC一致。
 */
void MX_GPIO_Init(void){
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = key_cap_Pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(key_cap_GPIO_Port, &gpio);
}
