/* Thread-Metric on FreeRTOS (STM32F4), the reference side of the
 * MOS-vs-FreeRTOS comparison.
 *
 * The kernel calls are the upstream FreeRTOS port, compiled unmodified
 * (external/thread-metric-benchmark/ports/freertos/tm_port.c). This file
 * supplies what that port
 * leaves to the target, mirroring the MOS port (thread_metric/tm_port.hpp):
 * the entry point, console output, the end-of-run marker the runner waits
 * for, and the benchmark interrupt on EXTI0. */

#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "tm_api.h"

void tm_main(void);

void tm_putchar(int c)
{
    bench_board_putchar((char) c);
}

void tm_semihosting_exit(int code)
{
    tm_printf("\n*** THREAD-METRIC DONE (code=%d) ***\n", code);
    for (;;) {
        __WFI();
    }
}

int main(void)
{
    bench_board_init();

    tm_report_init();
    tm_printf("Thread-Metric: reporting interval = %d s\n", tm_test_duration);

    tm_main();
    return 0;
}

/* Benchmark interrupt, as upstream's cortex-m/tm_isr_dispatch.c but on the
 * STM32 EXTI0 line the MOS port uses. FreeRTOS only allows its FromISR API
 * at or below configMAX_SYSCALL_INTERRUPT_PRIORITY, so EXTI0 runs at the
 * kernel (lowest) priority, as in the upstream dispatcher. */

__attribute__((weak)) void tm_interrupt_handler(void) {}
__attribute__((weak)) void tm_interrupt_preemption_handler(void) {}

static volatile bool tm_benchmark_interrupt_active;

void EXTI0_IRQHandler(void)
{
    tm_interrupt_handler();
    tm_interrupt_preemption_handler();
}

bool tm_benchmark_interrupt_context_active(void)
{
    return tm_benchmark_interrupt_active;
}

void tm_isr_dispatch_init(void)
{
    NVIC_SetPriority(EXTI0_IRQn, configLIBRARY_LOWEST_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

void tm_cause_interrupt(void)
{
    NVIC_SetPendingIRQ(EXTI0_IRQn);
    __DSB();
    __ISB();
}

void tm_cause_interrupt_sync(void)
{
    __disable_irq();
    tm_benchmark_interrupt_active = true;
    tm_interrupt_handler();
    tm_benchmark_interrupt_active = false;
    __enable_irq();
}
