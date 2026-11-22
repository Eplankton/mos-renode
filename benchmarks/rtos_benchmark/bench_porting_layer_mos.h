/* SPDX-License-Identifier: Apache-2.0 */

/*
 * MOS porting layer definitions for the RTOS-Benchmark suite.
 *
 * This header is force-included into every RTOS-Benchmark C source so the
 * upstream bench_api.h does not try to pull in a Zephyr/FreeRTOS header.
 */

#ifndef BENCH_PORTING_LAYER_MOS_H
#define BENCH_PORTING_LAYER_MOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t bench_time_t;
typedef void* bench_work;

/* Implemented by the kernel's printf.c and wired to the board UART. */
int printf_(const char* format, ...);

#define PRINTF(...) printf_(__VA_ARGS__)

/*
 * RTOS-Benchmark treats a smaller numeric priority as higher priority, which
 * matches MOS. Keep all benchmark threads above the MOS idle task (PRI_MIN).
 */
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

#endif /* BENCH_PORTING_LAYER_MOS_H */
