/**
 * @file common.h
 * @brief Bootloader串口基础收发接口。
 */
#ifndef BOOT_COMMON_H
#define BOOT_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "stm32g4xx_hal.h"
#include "usart.h"

#define IAP_UART_HANDLE huart2
#define IAP_UART        USART2

#define TX_TIMEOUT_MS   (1000U)

HAL_StatusTypeDef Serial_PutByte(uint8_t value);
HAL_StatusTypeDef Serial_PutBytes(const uint8_t *data, uint16_t length);
void Serial_PutString(const char *text);
void Serial_PutStatus(uint16_t status);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_COMMON_H */
