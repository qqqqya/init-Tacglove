/**
 * @file flash_if.c
 * @brief APP向量检查和设备SN的单次写入实现。
 */
#include "flash_if.h"

#include <string.h>

#include "stm32g4xx_hal.h"

#define SRAM_START_ADDRESS (0x20000000UL)
#define SRAM_END_ADDRESS   (0x20020000UL)
#define SN_RECORD_SIZE     (24U)

/**
 * @brief 检查APP1向量表中的MSP和Reset Handler是否落在合法范围。
 * @param app_address APP分区起始地址。
 * @return FLASHIF_OK表示可跳转，否则表示无有效APP。
 */
FLASHIF_StatusTypeDef FLASH_If_Check(uint32_t app_address){
    uint32_t stack_pointer;
    uint32_t reset_handler;
    uint32_t reset_address;

    if (app_address != APP1_START_ADDR)
    {
        return FLASHIF_INVALID_ADDRESS;
    }

    stack_pointer = *(const volatile uint32_t *)app_address;
    reset_handler = *(const volatile uint32_t *)(app_address + sizeof(uint32_t));
    reset_address = reset_handler & ~1UL;

    if ((stack_pointer < SRAM_START_ADDRESS) ||
        (stack_pointer > SRAM_END_ADDRESS) ||
        ((stack_pointer & 0x7UL) != 0UL))                   //栈顶指针（SP）最低 3 位必须为 0（8字节对齐）
    {
        return FLASHIF_EMPTY;
    }

    if (((reset_handler & 1UL) == 0UL) ||                   //复位入口地址  最低 1 位必须为 1
        (reset_address < APP1_START_ADDR) ||
        (reset_address >= (APP1_START_ADDR + APP1_SIZE)))
    {
        return FLASHIF_EMPTY;
    }

    return FLASHIF_OK;
}

/**
 * @brief 保留参考工程函数名，静默检查APP是否有效。
 * @param app_address APP分区起始地址。
 * @return APP检查结果。
 */
FLASHIF_StatusTypeDef CheckAppSilent(uint32_t app_address){
    return FLASH_If_Check(app_address);
}

/**
 * @brief 校验固定格式SN-TacGlove-000000。
 * @param sn 固定18字节SN，不要求以NUL结尾。
 * @return true表示格式有效。
 */
bool Boot_IsSnFormatValid(const uint8_t sn[SN_LENGTH]){
    uint32_t index;

    if (sn == NULL)
    {
        return false;
    }
    if (memcmp(sn, SN_PREFIX, SN_PREFIX_LENGTH) != 0)
    {
        return false;
    }
    for (index = SN_PREFIX_LENGTH; index < SN_LENGTH; ++index)
    {
        if ((sn[index] < (uint8_t)'0') || (sn[index] > (uint8_t)'9'))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief 把设备SN一次性写入Flash最后一页。
 * @param sn 固定18字节SN。
 * @return 写入结果；有效SN已存在时不会擦除或覆盖。
 * @note 存储布局兼容参考工程：4字节有效标记后紧跟18字节SN。
 */
BootSnResult Boot_ProgramSN(const uint8_t sn[SN_LENGTH]){
    FLASH_EraseInitTypeDef erase = {0};
    uint8_t record[SN_RECORD_SIZE];
    uint32_t page_error = 0U;
    uint32_t offset;
    HAL_StatusTypeDef status = HAL_OK;

    if (*(const volatile uint32_t *)SN_FLASH_ADDR == SN_VALID_FLAG)
    {
        return BOOT_SN_RESULT_EXISTS;
    }
    if (!Boot_IsSnFormatValid(sn))
    {
        return BOOT_SN_RESULT_INVALID;
    }

    memset(record, 0xFF, sizeof(record));
    memcpy(&record[0], &(const uint32_t){SN_VALID_FLAG}, sizeof(uint32_t));
    memcpy(&record[sizeof(uint32_t)], sn, SN_LENGTH);

    if (HAL_OK != HAL_FLASH_Unlock())
    {
        return BOOT_SN_RESULT_FLASH_ERROR;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_2;
    erase.Page = (SN_FLASH_ADDR - FIRMWARE_FLASH_BANK2_START_ADDRESS) /
                 FIRMWARE_FLASH_PAGE_SIZE_BYTES;
    erase.NbPages = 1U;
    if (HAL_OK != HAL_FLASHEx_Erase(&erase, &page_error))
    {
        (void)HAL_FLASH_Lock();
        return BOOT_SN_RESULT_FLASH_ERROR;
    }

    /* 先写后16字节，最后提交含有效标记的首个Double Word。 */
    for (offset = 8U; offset < SN_RECORD_SIZE; offset += 8U)
    {
        uint64_t double_word;
        memcpy(&double_word, &record[offset], sizeof(double_word));
        if (HAL_OK != HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                                        SN_FLASH_ADDR + offset,
                                        double_word))
        {
            status = HAL_ERROR;
            break;
        }
    }

    if (HAL_OK == status)
    {
        uint64_t first_double_word;
        memcpy(&first_double_word, &record[0], sizeof(first_double_word));
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                                   SN_FLASH_ADDR,
                                   first_double_word);
    }

    (void)HAL_FLASH_Lock();
    if (HAL_OK != status)
    {
        return BOOT_SN_RESULT_FLASH_ERROR;
    }

    if ((*(const volatile uint32_t *)SN_FLASH_ADDR != SN_VALID_FLAG) ||
        (memcmp((const void *)(SN_FLASH_ADDR + sizeof(uint32_t)), sn, SN_LENGTH) != 0))
    {
        return BOOT_SN_RESULT_FLASH_ERROR;
    }
    return BOOT_SN_RESULT_OK;
}

/**
 * @brief 从Flash读取并校验设备SN。
 * @param sn_out 接收固定18字节SN的缓冲区。
 * @return BOOT_SN_RESULT_OK表示记录标记和SN格式都有效。
 */
BootSnResult Boot_ReadSN(uint8_t sn_out[SN_LENGTH]){
    const uint8_t *stored_sn =
        (const uint8_t *)(SN_FLASH_ADDR + sizeof(uint32_t));

    if ((sn_out == NULL) ||
        (*(const volatile uint32_t *)SN_FLASH_ADDR != SN_VALID_FLAG) ||
        !Boot_IsSnFormatValid(stored_sn))
    {
        return BOOT_SN_RESULT_INVALID;
    }

    memcpy(sn_out, stored_sn, SN_LENGTH);
    return BOOT_SN_RESULT_OK;
}
