/**
 * @file main.c
 * @brief TacGlove Bootloader程序入口。
 */
#include "main.h"

#include "gpio.h"
#include "menu.h"
#include "usart.h"

static void SystemClock_Config(void);

/**
 * @brief 初始化最小板级资源并进入串口Bootloader菜单。
 * @return 正常情况下不会返回。
 */
int main(void){
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART2_UART_Init();

#if (BOOT_POWER_ON_JUMP_APP != 0U)
    Boot_JumpToApp();
#endif
    Main_Menu();

    while (1)
    {
    }
}

/**
 * @brief 使用12 MHz HSE配置系统时钟为170 MHz。
 */
static void SystemClock_Config(void){
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clock = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = RCC_PLLM_DIV3;
    oscillator.PLL.PLLN = 85;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2;
    oscillator.PLL.PLLQ = RCC_PLLQ_DIV2;
    oscillator.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_OK != HAL_RCC_OscConfig(&oscillator))
    {
        Error_Handler();
    }

    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV1;
    clock.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_OK != HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_4))
    {
        Error_Handler();
    }
}

/**
 * @brief 不可恢复错误处理。
 */
void Error_Handler(void){
    __disable_irq();
    while (1)
    {
    }
}
