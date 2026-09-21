/**
 * @file microros_allocators.c
 * @brief 将rcutils内存接口绑定到micro-ROS专用FreeRTOS heap。
 */
#include "microros_allocators.h"

#include "FreeRTOS.h"

void *pvPortMallocMicroROS(size_t wanted_size);
void vPortFreeMicroROS(void *pointer);
void *pvPortReallocMicroROS(void *pointer, size_t wanted_size);
void *pvPortCallocMicroROS(size_t number_of_elements,
                           size_t size_of_element);

void *microros_allocate(size_t size, void *state)
{
    (void)state;
    return pvPortMallocMicroROS(size);
}

void microros_deallocate(void *pointer, void *state)
{
    (void)state;

    if (NULL != pointer)
    {
        vPortFreeMicroROS(pointer);
    }
}

void *microros_reallocate(void *pointer, size_t size, void *state)
{
    (void)state;
    return pvPortReallocMicroROS(pointer, size);
}

void *microros_zero_allocate(size_t number_of_elements,
                             size_t size_of_element,
                             void *state)
{
    (void)state;
    return pvPortCallocMicroROS(number_of_elements, size_of_element);
}
