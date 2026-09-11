/**
 * @file dma_transport.h
 * @brief micro-ROS基于STM32 HAL UART DMA的自定义传输接口。
 */
#ifndef DMA_TRANSPORT_H
#define DMA_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uxr/client/transport.h"

bool cubemx_transport_open(struct uxrCustomTransport *transport);
bool cubemx_transport_close(struct uxrCustomTransport *transport);
size_t cubemx_transport_write(struct uxrCustomTransport *transport,
                              const uint8_t *buffer,
                              size_t length,
                              uint8_t *error);
size_t cubemx_transport_read(struct uxrCustomTransport *transport,
                             uint8_t *buffer,
                             size_t length,
                             int timeout_ms,
                             uint8_t *error);

#endif /* DMA_TRANSPORT_H */
