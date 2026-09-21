/**
 * @file common.c
 * @brief Bootloader串口基础收发实现。
 */
#include "common.h"

#include <stddef.h>
#include <string.h>

/**
 * @brief 通过IAP串口发送一个字节。
 * @param value 待发送字节。
 * @return HAL串口发送结果。
 */
HAL_StatusTypeDef Serial_PutByte(uint8_t value){
    return HAL_UART_Transmit(&IAP_UART_HANDLE, &value, 1U, TX_TIMEOUT_MS);
}

/**
 * @brief 通过IAP串口发送指定长度的原始数据。
 * @param data 数据缓冲区。
 * @param length 数据长度，单位为字节。
 * @return HAL串口发送结果。
 */
HAL_StatusTypeDef Serial_PutBytes(const uint8_t *data, uint16_t length){
    if ((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }
    return HAL_UART_Transmit(&IAP_UART_HANDLE, (uint8_t *)data, length, TX_TIMEOUT_MS);
}

/**
 * @brief 通过IAP串口发送以NUL结尾的文本。
 * @param text 待发送字符串。
 */
void Serial_PutString(const char *text){
    if (text == NULL)
    {
        return;
    }
    (void)Serial_PutBytes((const uint8_t *)text, (uint16_t)strlen(text));
}

/**
 * @brief 以大端顺序发送参考工程兼容的16位Bootloader状态码。
 * @param status 状态码，例如0xF016表示SN写入成功。
 */
void Serial_PutStatus(uint16_t status){
    const uint8_t bytes[2] = {
        (uint8_t)(status >> 8U),
        (uint8_t)(status & 0xFFU),
    };
    (void)Serial_PutBytes(bytes, (uint16_t)sizeof(bytes));
}
