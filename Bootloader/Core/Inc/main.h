/**
 * @file main.h
 * @brief Bootloader公共入口和板级引脚定义。
 */
#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"

#define key_cap_Pin       GPIO_PIN_11
#define key_cap_GPIO_Port GPIOA

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
