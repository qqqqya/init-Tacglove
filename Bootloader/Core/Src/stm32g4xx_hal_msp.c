/**
 * @file stm32g4xx_hal_msp.c
 * @brief HAL全局底层初始化。
 */
#include "main.h"

/**
 * @brief 使能系统配置和电源控制时钟。
 */
void HAL_MspInit(void){
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_DisableUCPDDeadBattery();
}
