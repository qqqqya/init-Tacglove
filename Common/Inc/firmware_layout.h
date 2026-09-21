/**
 * @file firmware_layout.h
 * @brief STM32G474 bootloader、应用程序和配置区的统一Flash分区定义。
 */
#ifndef FIRMWARE_LAYOUT_H
#define FIRMWARE_LAYOUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief STM32G474RET6内部Flash总容量：512 KiB。 */
#define FIRMWARE_FLASH_START_ADDRESS       (0x08000000UL)
#define FIRMWARE_FLASH_END_ADDRESS         (0x08080000UL)
#define FIRMWARE_FLASH_BANK2_START_ADDRESS (0x08040000UL)

/** @brief Bootloader分区：64 KiB。 */
#define FIRMWARE_BOOT_START_ADDRESS        (0x08000000UL)
#define FIRMWARE_BOOT_SIZE_BYTES           (0x00010000UL)

/** @brief 主应用APP1分区：192 KiB。 */
#define FIRMWARE_APP1_START_ADDRESS        (0x08010000UL)
#define FIRMWARE_APP1_SIZE_BYTES           (0x00030000UL)
#define FIRMWARE_APP1_END_ADDRESS          \
    (FIRMWARE_APP1_START_ADDRESS + FIRMWARE_APP1_SIZE_BYTES)

/** @brief 备份应用APP2分区：192 KiB。 */
#define FIRMWARE_APP2_START_ADDRESS        (0x08040000UL)
#define FIRMWARE_APP2_SIZE_BYTES           (0x00030000UL)
#define FIRMWARE_APP2_END_ADDRESS          \
    (FIRMWARE_APP2_START_ADDRESS + FIRMWARE_APP2_SIZE_BYTES)

/** @brief 配置区：最后64 KiB；SN暂存于Flash最后一页。 */
#define FIRMWARE_CONFIG_START_ADDRESS      (0x08070000UL)
#define FIRMWARE_CONFIG_SIZE_BYTES         (0x00010000UL)
#define FIRMWARE_FLASH_PAGE_SIZE_BYTES     (0x00000800UL)
#define FIRMWARE_SN_START_ADDRESS          (0x0807F800UL)

/** @brief Cortex-M4向量表要求的最小对齐。 */
#define FIRMWARE_VECTOR_ALIGNMENT_BYTES    (0x00000200UL)

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_LAYOUT_H */
