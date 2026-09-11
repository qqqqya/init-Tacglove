/**
 * @file dma_transport.c
 * @brief USART2 RX循环DMA和TX普通DMA形式的micro-ROS传输实现。
 */
#include "dma_transport.h"

#include <limits.h>

#include "FreeRTOS.h"
#include "task.h"
#include "usart.h"

#include "rmw_microxrcedds_c/config.h"

#ifdef RMW_UXRCE_TRANSPORT_CUSTOM

#define UART_DMA_RX_BUFFER_SIZE 2048U
#define UART_BITS_PER_BYTE      10U
#define UART_TX_MARGIN_MS       20U

static uint8_t s_dma_rx_buffer[UART_DMA_RX_BUFFER_SIZE];
static size_t s_dma_rx_head;

/**
 * @brief 读取循环DMA当前写入位置。
 * @param uart 非空且已关联RX DMA的UART句柄。
 * @return DMA写入位置，范围为0~UART_DMA_RX_BUFFER_SIZE-1。
 */
static size_t dma_transport_rx_tail(const UART_HandleTypeDef *uart)
{
    const uint32_t remaining = __HAL_DMA_GET_COUNTER(uart->hdmarx);
    return (UART_DMA_RX_BUFFER_SIZE - remaining) % UART_DMA_RX_BUFFER_SIZE;
}

bool cubemx_transport_open(struct uxrCustomTransport *transport)
{
    if ((NULL == transport) || (NULL == transport->args))
    {
        return false;
    }

    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)transport->args;
    if ((NULL == uart->hdmarx) || (NULL == uart->hdmatx))
    {
        return false;
    }

    (void)HAL_UART_DMAStop(uart);
    s_dma_rx_head = 0U;

    return HAL_OK == HAL_UART_Receive_DMA(uart,
                                          s_dma_rx_buffer,
                                          UART_DMA_RX_BUFFER_SIZE);
}

bool cubemx_transport_close(struct uxrCustomTransport *transport)
{
    if ((NULL == transport) || (NULL == transport->args))
    {
        return false;
    }

    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)transport->args;
    return HAL_OK == HAL_UART_DMAStop(uart);
}

size_t cubemx_transport_write(struct uxrCustomTransport *transport,
                              const uint8_t *buffer,
                              size_t length,
                              uint8_t *error)
{
    if (NULL != error)
    {
        *error = 1U;
    }

    if ((NULL == transport) ||
        (NULL == transport->args) ||
        (NULL == buffer) ||
        (0U == length) ||
        (UINT16_MAX < length))
    {
        return 0U;
    }

    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)transport->args;
    if (HAL_UART_STATE_READY != uart->gState)
    {
        return 0U;
    }

    if (HAL_OK != HAL_UART_Transmit_DMA(uart,
                                        buffer,
                                        (uint16_t)length))
    {
        return 0U;
    }

    const uint32_t baud_rate = uart->Init.BaudRate;
    const uint32_t transfer_time_ms =
        (uint32_t)(((uint64_t)length * UART_BITS_PER_BYTE * 1000U +
                    baud_rate - 1U) /
                   baud_rate);
    const TickType_t timeout_ticks = pdMS_TO_TICKS(
        transfer_time_ms + UART_TX_MARGIN_MS);
    const TickType_t start_tick = xTaskGetTickCount();

    while (HAL_UART_STATE_READY != uart->gState)
    {
        if (timeout_ticks <= (xTaskGetTickCount() - start_tick))
        {
            (void)HAL_UART_AbortTransmit(uart);
            return 0U;
        }

        vTaskDelay(pdMS_TO_TICKS(1U));
    }

    if (NULL != error)
    {
        *error = 0U;
    }

    return length;
}

size_t cubemx_transport_read(struct uxrCustomTransport *transport,
                             uint8_t *buffer,
                             size_t length,
                             int timeout_ms,
                             uint8_t *error)
{
    if (NULL != error)
    {
        *error = 1U;
    }

    if ((NULL == transport) ||
        (NULL == transport->args) ||
        (NULL == buffer) ||
        (0U == length))
    {
        return 0U;
    }

    UART_HandleTypeDef *uart = (UART_HandleTypeDef *)transport->args;
    if (NULL == uart->hdmarx)
    {
        return 0U;
    }

    const TickType_t start_tick = xTaskGetTickCount();
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS((0 < timeout_ms) ? (uint32_t)timeout_ms : 0U);
    size_t dma_tail = dma_transport_rx_tail(uart);

    while ((s_dma_rx_head == dma_tail) &&
           (0 < timeout_ms) &&
           ((xTaskGetTickCount() - start_tick) < timeout_ticks))
    {
        vTaskDelay(pdMS_TO_TICKS(1U));
        dma_tail = dma_transport_rx_tail(uart);
    }

    size_t read_length = 0U;
    while ((s_dma_rx_head != dma_tail) && (read_length < length))
    {
        buffer[read_length] = s_dma_rx_buffer[s_dma_rx_head];
        s_dma_rx_head = (s_dma_rx_head + 1U) % UART_DMA_RX_BUFFER_SIZE;
        ++read_length;
    }

    if (NULL != error)
    {
        *error = 0U;
    }

    return read_length;
}

#endif /* RMW_UXRCE_TRANSPORT_CUSTOM */
