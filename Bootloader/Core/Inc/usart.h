/**
 * @file usart.h
 * @brief Bootloader USART2初始化接口。
 */
#ifndef USART_H
#define USART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern UART_HandleTypeDef huart2;

void MX_USART2_UART_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* USART_H */
