/**
 * @file bsp_sn_driver.c
 * @brief Application设备SN只读访问实现。
 */
#include "bsp_sn_driver.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief 检查Flash中的SN字符是否符合18字节SN兼容格式。
 * @param sn_data 指向固定BSP_SN_LENGTH字节的Flash数据。
 * @retval SN_OK 格式有效。
 * @retval SN_ERROR 格式无效。
 */
static sn_driver_status_t bsp_sn_driver_validate(const uint8_t *sn_data){
    uint32_t index;

    if (((uint8_t)'S' != sn_data[0]) ||
        ((uint8_t)'N' != sn_data[1]) ||
        ((uint8_t)'-' != sn_data[2]))
    {
        return SN_ERROR;
    }

    for (index = 3U; index < BSP_SN_LENGTH; ++index)
    {
        const bool is_digit = ((uint8_t)'0' <= sn_data[index]) &&
                              ((uint8_t)'9' >= sn_data[index]);
        const bool is_upper = ((uint8_t)'A' <= sn_data[index]) &&
                              ((uint8_t)'Z' >= sn_data[index]);
        const bool is_lower = ((uint8_t)'a' <= sn_data[index]) &&
                              ((uint8_t)'z' >= sn_data[index]);
        if (!is_digit && !is_upper && !is_lower &&
            ((uint8_t)'-' != sn_data[index]))
        {
            return SN_ERROR;
        }
    }

    return SN_OK;
}

sn_driver_status_t bsp_sn_driver_read(char *sn_buffer,
                                      uint32_t buffer_size){
    const uint8_t *stored_sn =
        (const uint8_t *)(FIRMWARE_SN_START_ADDRESS + sizeof(uint32_t));
    sn_driver_status_t status;

    if ((NULL == sn_buffer) || (BSP_SN_BUFFER_SIZE > buffer_size))
    {
        return SN_ERRORPARAMETER;
    }

    sn_buffer[0] = '\0';
    if (FIRMWARE_SN_VALID_FLAG !=
        *(const volatile uint32_t *)FIRMWARE_SN_START_ADDRESS)
    {
        return SN_ERRORRESOURCE;
    }

    status = bsp_sn_driver_validate(stored_sn);
    if (SN_OK != status)
    {
        return status;
    }

    memcpy(sn_buffer, stored_sn, BSP_SN_LENGTH);
    sn_buffer[BSP_SN_LENGTH] = '\0';
    return SN_OK;
}
