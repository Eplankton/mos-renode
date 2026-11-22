/* Minimal STM32F4 Discovery bring-up for the FreeRTOS benchmark builds.
 *
 * Same clock as the MOS builds (16 MHz HSI out of reset, no PLL) and the
 * same console (USART2 on PD5, polled). Unlike the MOS board support it does
 * not call HAL_Init() or start SysTick: FreeRTOS configures SysTick in
 * vTaskStartScheduler(). */

#ifndef MOS_BENCH_FREERTOS_BOARD_H
#define MOS_BENCH_FREERTOS_BOARD_H

#include "stm32f4xx.h"

static inline void bench_board_init(void)
{
    /* All priority bits pre-emptive, as HAL_Init() does for the MOS build. */
    NVIC_SetPriorityGrouping(3);

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void) RCC->APB1ENR;

    /* PD5 = USART2_TX, alternate function 7 */
    GPIOD->MODER = (GPIOD->MODER & ~GPIO_MODER_MODER5) | GPIO_MODER_MODER5_1;
    GPIOD->AFR[0] = (GPIOD->AFR[0] & ~GPIO_AFRL_AFSEL5) | (7U << GPIO_AFRL_AFSEL5_Pos);

    USART2->BRR = (SystemCoreClock + 115200 / 2) / 115200; /* APB1 = HCLK */
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE;
}

static inline void bench_board_putchar(char ch)
{
    while ((USART2->SR & USART_SR_TXE) == 0) {}
    USART2->DR = (uint8_t) ch;
}

#endif
