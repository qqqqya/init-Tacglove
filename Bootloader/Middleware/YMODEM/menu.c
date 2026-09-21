/**
 * @file menu.c
 * @brief Bootloader串口菜单、SN命令和APP跳转实现。
 */
#include "menu.h"

#include <string.h>

#include "common.h"
#include "flash_if.h"
#include "main.h"

#define MENU_RX_TIMEOUT_MS       (100U)
#define COMMAND_RX_TIMEOUT_MS    (1000U)
#define SN_RX_TIMEOUT_MS         (3000U)
#define WAIT_STATUS_PERIOD_MS    (3000U)

static void CheckAppPartition(void);
static void HandlePrefixedCommand(uint8_t prefix);
static void Boot_StartApplication(uint32_t app_stack_pointer,
                                  uint32_t app_reset_address)
    __attribute__((noreturn));

/**
 * @brief 设置APP主栈后直接分支到APP复位入口。
 * @param app_stack_pointer APP向量表首项保存的主栈初值。
 * @param app_reset_address APP向量表第二项保存的复位入口地址。
 * @note 修改MSP后不能再执行依赖当前C栈帧的语句，因此把设置MSP和分支放在同一段汇编中。
 */
static void Boot_StartApplication(uint32_t app_stack_pointer,
                                  uint32_t app_reset_address){
    __asm volatile(
        "msr msp, %0\n"
        "bx %1\n"
        :
        : "r" (app_stack_pointer), "r" (app_reset_address)
        : "memory");
    __builtin_unreachable();
}

/**
 * @brief 返回APP1分区有效性状态码F009或F008。
 */
static void CheckAppPartition(void){
    if (FLASHIF_OK == FLASH_If_Check(APP1_START_ADDR))
    {
        Serial_PutStatus(BOOT_DNL_IMG_VALID);
    }
    else
    {
        Serial_PutStatus(BOOT_DNL_IMG_INVALID);
    }
}

/**
 * @brief 校验APP1并完成Bootloader到Application的裸机跳转。
 */
void Boot_JumpToApp(void){
    uint32_t app_stack_pointer;
    uint32_t app_reset_address;
    uint32_t index;

    if (FLASHIF_OK != CheckAppSilent(APP1_START_ADDR))
    {
        Serial_PutStatus(BOOT_DNL_IMG_INVALID);
        return;
    }

    app_stack_pointer = *(const volatile uint32_t *)APP1_START_ADDR;
    app_reset_address = *(const volatile uint32_t *)(APP1_START_ADDR + 4U);

    Serial_PutStatus(BOOT_SYS_JUMP_APP);
    (void)HAL_UART_DeInit(&IAP_UART_HANDLE);
    (void)HAL_RCC_DeInit();
    (void)HAL_DeInit();

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    for (index = 0U; index < 8U; ++index)
    {
        NVIC->ICER[index] = 0xFFFFFFFFUL;
        NVIC->ICPR[index] = 0xFFFFFFFFUL;
    }
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;

    SCB->VTOR = APP1_START_ADDR;
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    __set_CONTROL(0U);
    __DSB();
    __ISB();
    Boot_StartApplication(app_stack_pointer, app_reset_address);
}

/**
 * @brief 接收并处理参考工程的#F_SN_W和*F_SN_R命令。
 * @param prefix 已接收的命令前缀字符。
 */
static void HandlePrefixedCommand(uint8_t prefix){
    uint8_t command[6] = {0};

    if (HAL_OK != HAL_UART_Receive(&IAP_UART_HANDLE,
                                   command,
                                   (uint16_t)sizeof(command),
                                   COMMAND_RX_TIMEOUT_MS))
    {
        Serial_PutStatus((prefix == (uint8_t)'#') ?
                         BOOT_SN_RECV_ERR : BOOT_USER_CMD_ERR);
        return;
    }

    if ((prefix == (uint8_t)'#') &&
        (memcmp(command, "F_SN_W", sizeof(command)) == 0))
    {
        Serial_ProgramSN();
        return;
    }

    if ((prefix == (uint8_t)'*') &&
        (memcmp(command, "F_SN_R", sizeof(command)) == 0))
    {
        uint8_t sn[SN_LENGTH];
        if (BOOT_SN_RESULT_OK == Boot_ReadSN(sn))
        {
            (void)Serial_PutBytes(sn, SN_LENGTH);
        }
        else
        {
            Serial_PutStatus(BOOT_SN_WRITE_ERR);
        }
        return;
    }

    Serial_PutStatus(BOOT_USER_CMD_ERR);
}

/**
 * @brief 进入参考工程风格的串口菜单并持续处理命令。
 * @note 命令1跳转APP，2预留YMODEM，3检查APP；SN使用独立前缀命令。
 */
void Main_Menu(void){
    uint32_t last_wait_status_tick = 0U;

    Serial_PutStatus(BOOT_MENU_ENTERED);

    while (1)
    {
        uint8_t key = 0U;
        HAL_StatusTypeDef receive_status;
        uint32_t now = HAL_GetTick();

        if ((last_wait_status_tick == 0U) ||
            ((now - last_wait_status_tick) >= WAIT_STATUS_PERIOD_MS))
        {
            Serial_PutStatus(BOOT_WAIT_USER_CMD);
            last_wait_status_tick = now;
        }

        //接受用户输入 py转端口命令到c端
        receive_status = HAL_UART_Receive(&IAP_UART_HANDLE,
                                          &key,
                                          1U,
                                          MENU_RX_TIMEOUT_MS);
        if (HAL_TIMEOUT == receive_status)
        {
            continue;
        }
        if (HAL_OK != receive_status)
        {
            __HAL_UART_CLEAR_FLAG(&IAP_UART_HANDLE,
                                  UART_CLEAR_PEF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF | UART_CLEAR_OREF);
            continue;
        }

        last_wait_status_tick = HAL_GetTick();
        if ((key == (uint8_t)'#') || (key == (uint8_t)'*'))
        {
            HandlePrefixedCommand(key);
            continue;
        }

        switch (key)
        {
            case (uint8_t)'1':
                Boot_JumpToApp();
                break;

            case (uint8_t)'2':
                /* 小阶段5.2只保留协议入口，不擦除、不接收固件。 */
                Serial_PutStatus(BOOT_USER_CMD_ERR);
                break;

            case (uint8_t)'3':
                CheckAppPartition();
                break;

            default:
                Serial_PutStatus(BOOT_USER_CMD_ERR);
                break;
        }
    }
}

/**
 * @brief 接收固定18字节SN，校验后执行一次性Flash写入。
 */
void Serial_ProgramSN(void){
    uint8_t sn[SN_LENGTH] = {0};
    BootSnResult result;

    Serial_PutStatus(BOOT_SN_WAIT_DATA);
    if (HAL_OK != HAL_UART_Receive(&IAP_UART_HANDLE,
                                   sn,
                                   SN_LENGTH,
                                   SN_RX_TIMEOUT_MS))
    {
        Serial_PutStatus(BOOT_SN_RECV_ERR);
        return;
    }

    result = Boot_ProgramSN(sn);
    switch (result)
    {
        case BOOT_SN_RESULT_OK:
            Serial_PutStatus(BOOT_SN_WRITE_OK);
            break;

        case BOOT_SN_RESULT_EXISTS:
            Serial_PutStatus(BOOT_SN_EXIST);
            break;

        case BOOT_SN_RESULT_INVALID:
        case BOOT_SN_RESULT_FLASH_ERROR:
        default:
            Serial_PutStatus(BOOT_SN_WRITE_ERR);
            break;
    }
}
