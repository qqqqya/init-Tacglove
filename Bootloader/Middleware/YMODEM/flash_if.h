/**
 * @file flash_if.h
 * @brief APP分区检查和设备SN Flash访问接口。
 */
#ifndef FLASH_IF_H
#define FLASH_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "firmware_layout.h"

#define APP1_START_ADDR FIRMWARE_APP1_START_ADDRESS
#define APP1_SIZE       FIRMWARE_APP1_SIZE_BYTES
#define APP2_START_ADDR FIRMWARE_APP2_START_ADDRESS
#define APP2_SIZE       FIRMWARE_APP2_SIZE_BYTES
#define SN_FLASH_ADDR   FIRMWARE_SN_START_ADDRESS

#define SN_LENGTH       (18U)
#define SN_PREFIX       "SN-TacGlove-"
#define SN_PREFIX_LENGTH (12U)
#define SN_VALID_FLAG   (0xA55A5AA5UL)

typedef enum
{
    FLASHIF_OK = 0,
    FLASHIF_EMPTY,
    FLASHIF_INVALID_ADDRESS,
    FLASHIF_ERASE_ERROR,
    FLASHIF_WRITE_ERROR,
    FLASHIF_VERIFY_ERROR,
} FLASHIF_StatusTypeDef;

typedef enum
{
    BOOT_SN_RESULT_OK = 0,
    BOOT_SN_RESULT_EXISTS,
    BOOT_SN_RESULT_INVALID,
    BOOT_SN_RESULT_FLASH_ERROR,
} BootSnResult;

FLASHIF_StatusTypeDef CheckAppSilent(uint32_t app_address);
FLASHIF_StatusTypeDef FLASH_If_Check(uint32_t app_address);
bool Boot_IsSnFormatValid(const uint8_t sn[SN_LENGTH]);
BootSnResult Boot_ProgramSN(const uint8_t sn[SN_LENGTH]);
BootSnResult Boot_ReadSN(uint8_t sn_out[SN_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_IF_H */
