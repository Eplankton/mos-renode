/* SPDX-License-Identifier: Apache-2.0 */

/*
 * FreeRTOS porting layer definitions for RTOS-Benchmark on STM32F4.
 *
 * Force-included into every RTOS-Benchmark source, like the MOS one
 * (rtos_benchmark/bench_porting_layer_mos.h). The feature flags and the
 * priority range are the same as for MOS so that both kernels run the same
 * set of measurements.
 */

#ifndef BENCH_PORTING_LAYER_FREERTOS_F4_H
#define BENCH_PORTING_LAYER_FREERTOS_F4_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t bench_time_t;
typedef void* bench_work;

/* core/libc/printf.c, as in the MOS build */
int printf_(const char* format, ...);

#define PRINTF(...) printf_(__VA_ARGS__)

/* Smaller number = higher priority, as for MOS; mapped onto FreeRTOS
 * priorities in the port. */
#define BENCH_LAST_PRIORITY 20
#define BENCH_IDLE_TIME     0

#define RTOS_HAS_THREAD_SPAWN        1
#define RTOS_HAS_THREAD_CREATE_START 1
#define RTOS_HAS_SUSPEND_RESUME      1
#define RTOS_HAS_MESSAGE_QUEUE       1
#define RTOS_HAS_MAIN_ENTRY_POINT    0

#define ARG_UNUSED(x) (void) (x)
#define __weak        __attribute__((__weak__))
#define TICK_SYNCH()  bench_sync_ticks()

#ifdef __cplusplus
}
#endif

#endif /* BENCH_PORTING_LAYER_FREERTOS_F4_H */
