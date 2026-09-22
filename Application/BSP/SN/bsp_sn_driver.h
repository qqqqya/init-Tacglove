/**
 * @file bsp_sn_driver.h
 * @brief Application只读访问设备SN的板级驱动接口。
 */
#ifndef BSP_SN_DRIVER_H
#define BSP_SN_DRIVER_H

#include <stdint.h>

#include "firmware_layout.h"

#define BSP_SN_LENGTH      FIRMWARE_SN_LENGTH_BYTES
#define BSP_SN_BUFFER_SIZE (BSP_SN_LENGTH + 1U)

/** @brief SN只读驱动返回状态。 */
typedef enum
{
    SN_OK = 0,
    SN_ERROR,
    SN_ERRORRESOURCE,
    SN_ERRORPARAMETER,
    SN_RESERVED = 0xFF,
} sn_driver_status_t;

/**
 * @brief 从Bootloader约定的Flash页读取并校验设备SN。
 * @param[out] sn_buffer 接收SN字符串的缓冲区，成功时包含结尾NUL。
 * @param buffer_size 缓冲区容量，必须不小于BSP_SN_BUFFER_SIZE。
 * @retval SN_OK SN有效且已经复制到输出缓冲区。
 * @retval SN_ERRORRESOURCE Flash中没有有效SN标记。
 * @retval SN_ERROR SN内容不符合18字节SN兼容格式。
 * @retval SN_ERRORPARAMETER 输出指针为空或容量不足。
 */
sn_driver_status_t bsp_sn_driver_read(char *sn_buffer,
                                      uint32_t buffer_size);

#endif /* BSP_SN_DRIVER_H */
