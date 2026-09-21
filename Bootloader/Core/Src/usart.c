/**
 * @file usart.c
 * @brief Bootloader USART2轮询通信配置。
 */
#include "usart.h"

UART_HandleTypeDef huart2;

/**
 * @brief 初始化CH340正式通信通道USART2，参数为115200-8-N-1。
 */
void MX_USART2_UART_Init(void){
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_OK != HAL_UART_Init(&huart2))
    {
        Error_Handler();
    }
    if (HAL_OK != HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8))
    {
        Error_Handler();
    }
    if (HAL_OK != HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8))
    {
        Error_Handler();
    }
    if (HAL_OK != HAL_UARTEx_DisableFifoMode(&huart2))
    {
        Error_Handler();
    }
}

/**
 * @brief 配置USART2的PA2 TX和PA3 RX引脚。
 * @param uart_handle UART句柄。
 */
void HAL_UART_MspInit(UART_HandleTypeDef *uart_handle){
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};

    if (uart_handle->Instance != USART2)
    {
        return;
    }

    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USART2;
    peripheral_clock.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
    if (HAL_OK != HAL_RCCEx_PeriphCLKConfig(&peripheral_clock))
    {
        Error_Handler();
    }

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/**
 * @brief 释放USART2和对应GPIO。
 * @param uart_handle UART句柄。
 */
void HAL_UART_MspDeInit(UART_HandleTypeDef *uart_handle){
    if (uart_handle->Instance != USART2)
    {
        return;
    }

    __HAL_RCC_USART2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_3);
}
