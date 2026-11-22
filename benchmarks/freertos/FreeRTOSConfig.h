/* FreeRTOS configuration for the MOS-vs-FreeRTOS benchmark builds
 * (STM32F407, Cortex-M4F, ARM_CM4F port).
 *
 * Chosen to match the MOS benchmark builds: 16 MHz HSI core clock, 1 kHz
 * tick, preemptive priority scheduling with time slicing (MOS PreemptPri),
 * mutexes with priority inheritance, recursive mutexes, counting semaphores.
 * Assertions follow MOS (MOS_CONF_ASSERT, see MOS_BENCH_ASSERT). Features
 * MOS does not have or the benchmarks do not use (software timers, trace,
 * stack-overflow checks, run-time stats) are off. */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t SystemCoreClock;
#ifdef __cplusplus
}
#endif

#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      ((TickType_t) 1000)
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configMAX_PRIORITIES                    32
#define configMINIMAL_STACK_SIZE                ((uint16_t) 128)
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0

/* Memory: the RTOS-Benchmark port allocates statically, the upstream
 * Thread-Metric port dynamically (heap_4); neither allocates while timing. */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t) (48 * 1024))
#define configKERNEL_PROVIDED_STATIC_MEMORY     1

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configUSE_CO_ROUTINES                   0
#define configUSE_TIMERS                        0

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskResumeFromISR              1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* STM32F4: 4 NVIC priority bits. FreeRTOS API calls are allowed from
 * interrupts of priority 5..15. */
#define configPRIO_BITS                              4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* -DMOS_BENCH_ASSERT=OFF turns assertions off in both kernels. */
#if MOS_BENCH_NO_ASSERT
#define configASSERT(x)
#else
#define configASSERT(x)                \
    do {                               \
        if ((x) == 0) {                \
            __asm volatile("cpsid i"); \
            for (;;) {}                \
        }                              \
    } while (0)
#endif

/* Map the port's handlers onto the CMSIS vector names. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
