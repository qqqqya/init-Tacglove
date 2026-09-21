/**
 * @file stm32g4xx_it.h
 * @brief Cortex-M4异常处理函数声明。
 */
#ifndef STM32G4XX_IT_H
#define STM32G4XX_IT_H

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

#endif /* STM32G4XX_IT_H */
