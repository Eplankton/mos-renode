// SPDX-License-Identifier: Apache-2.0
//
// RTOS-Benchmark on FreeRTOS (STM32F4), the reference side of the
// MOS-vs-FreeRTOS comparison.
//
// The kernel calls follow the upstream FreeRTOS port
// (external/rtos-benchmark/src/freertos/bench_porting_layer_freertos.c,
// written for an NXP K64 board); the target half is shared with the MOS port
// so that both kernels are timed with the same code:
//  - timing and the SysTick interrupt-latency hooks: bench_timer_cortex_m.hpp
//  - the malloc/free test's 128-byte freelist: common/block_pool.hpp
//
// Differences from upstream, all to run the same measurements as MOS:
//  - create-then-start semantics (RTOS_HAS_THREAD_CREATE_START): a created
//    task is suspended before it can run and started with vTaskResume();
//  - the message-queue test is enabled;
//  - bench_sem_give_from_isr() yields if it woke a higher-priority task, as
//    MOS's up_from_isr() does (upstream passes NULL and switches only at the
//    next tick).

#include "bench_porting_layer_freertos.h"

extern "C" {
#include "bench_api.h"
}

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "board.h"
#include "../common/block_pool.hpp"

#define BENCH_TICK_HZ configTICK_RATE_HZ
#include "../rtos_benchmark/bench_timer_cortex_m.hpp"

namespace
{
	constexpr int MAX_THREADS    = 10;
	constexpr int MAX_SEMAPHORES = 5;
	constexpr int MAX_MUTEXES    = 1;
	constexpr int MAX_QUEUES     = 1;
	constexpr int QUEUE_SIZE     = 1;

	// Same stack depth as the upstream port: configMINIMAL_STACK_SIZE + 200
	constexpr uint32_t STACK_SIZE = configMINIMAL_STACK_SIZE + 200;

	StaticTask_t task_buffer[MAX_THREADS];
	StackType_t stack_buffer[MAX_THREADS][STACK_SIZE];
	TaskHandle_t threads[MAX_THREADS];

	StaticTask_t init_task_buffer;
	StackType_t init_stack_buffer[STACK_SIZE];

	StaticSemaphore_t semaphore_buffer[MAX_SEMAPHORES];
	SemaphoreHandle_t semaphores[MAX_SEMAPHORES];

	StaticSemaphore_t mutex_buffer[MAX_MUTEXES];
	SemaphoreHandle_t mutexes[MAX_MUTEXES];

	StaticQueue_t queue_buffer[MAX_QUEUES];
	uint8_t queue_storage[MAX_QUEUES][QUEUE_SIZE];
	QueueHandle_t queues[MAX_QUEUES];

	// A task cannot reuse its static TCB until the idle task has reaped it
	// after vTaskDelete(NULL). As upstream, exiting tasks park on a
	// semaphore and bench_collect_resources() deletes them from outside.
	TaskHandle_t threads_to_remove[MAX_THREADS];
	int threads_to_remove_idx = 0;
	StaticSemaphore_t to_remove_sem_buffer;
	SemaphoreHandle_t to_remove_sem;

	MOS::Bench::BlockPool_t<128, 64> bench_heap;
	bool bench_heap_ready = false;

	// RTOS-Benchmark: smaller = higher, 0..31. FreeRTOS: larger = higher.
	UBaseType_t map_prio(int prio)
	{
		if (prio < 0) prio = 0;
		if (prio > configMAX_PRIORITIES - 1) prio = configMAX_PRIORITIES - 1;
		return static_cast<UBaseType_t>((configMAX_PRIORITIES - 1) - prio);
	}

	void (*bench_entry)(void*) = nullptr;

	void bench_task(void*)
	{
		if (bench_entry != nullptr) bench_entry(nullptr);

		PRINTF("\n*** RTOS-BENCHMARK DONE ***\n");
		vTaskDelete(nullptr);
	}
}

extern "C" {

void bench_all(void* arg);

void _putchar(char ch) { bench_board_putchar(ch); }

int main()
{
	bench_board_init();
	bench_test_init(bench_all);
	return 0;
}

void bench_test_init(void (*test_init_function)(void*))
{
	bench_entry   = test_init_function;
	to_remove_sem = xSemaphoreCreateCountingStatic(MAX_THREADS, 0, &to_remove_sem_buffer);

	xTaskCreateStatic(
	    bench_task, "benchmark", STACK_SIZE, nullptr,
	    map_prio(BENCH_LAST_PRIORITY - 1), init_stack_buffer, &init_task_buffer
	);

	vTaskStartScheduler();
	for (;;) {}
}

void bench_thread_set_priority(int priority)
{
	vTaskPrioritySet(nullptr, map_prio(priority));
}

int bench_thread_create(
    int thread_id, const char* thread_name, int priority,
    void (*entry_function)(void*), void* args
)
{
	if (thread_id < 0 || thread_id >= MAX_THREADS) return BENCH_ERROR;

	// Created suspended: nothing may run between the two calls.
	vTaskSuspendAll();
	threads[thread_id] = xTaskCreateStatic(
	    entry_function, thread_name, STACK_SIZE, args, map_prio(priority),
	    stack_buffer[thread_id], &task_buffer[thread_id]
	);
	vTaskSuspend(threads[thread_id]);
	xTaskResumeAll();

	return threads[thread_id] != nullptr ? BENCH_SUCCESS : BENCH_ERROR;
}

int bench_thread_spawn(
    int thread_id, const char* thread_name, int priority,
    void (*entry_function)(void*), void* args
)
{
	if (thread_id < 0 || thread_id >= MAX_THREADS) return BENCH_ERROR;

	threads[thread_id] = xTaskCreateStatic(
	    entry_function, thread_name, STACK_SIZE, args, map_prio(priority),
	    stack_buffer[thread_id], &task_buffer[thread_id]
	);

	return threads[thread_id] != nullptr ? BENCH_SUCCESS : BENCH_ERROR;
}

void bench_thread_start(int thread_id) { vTaskResume(threads[thread_id]); }

void bench_thread_resume(int thread_id) { vTaskResume(threads[thread_id]); }

void bench_thread_suspend(int thread_id) { vTaskSuspend(threads[thread_id]); }

void bench_thread_abort(int thread_id) { vTaskDelete(threads[thread_id]); }

void bench_thread_exit(void)
{
	taskENTER_CRITICAL();
	threads_to_remove[threads_to_remove_idx++] = xTaskGetCurrentTaskHandle();
	taskEXIT_CRITICAL();

	xSemaphoreTake(to_remove_sem, portMAX_DELAY);
}

void bench_collect_resources(void)
{
	while (threads_to_remove_idx > 0) {
		vTaskDelete(threads_to_remove[--threads_to_remove_idx]);
	}
}

void bench_yield(void) { taskYIELD(); }

void bench_sync_ticks(void) { vTaskDelay(1); }

int bench_sem_create(int sem_id, int initial_count, int maximum_count)
{
	if (sem_id < 0 || sem_id >= MAX_SEMAPHORES) return BENCH_ERROR;

	semaphores[sem_id] = xSemaphoreCreateCountingStatic(
	    maximum_count, initial_count, &semaphore_buffer[sem_id]
	);
	return semaphores[sem_id] != nullptr ? BENCH_SUCCESS : BENCH_ERROR;
}

void bench_sem_give(int sem_id) { xSemaphoreGive(semaphores[sem_id]); }

void bench_sem_give_from_isr(int sem_id)
{
	BaseType_t woken = pdFALSE;
	xSemaphoreGiveFromISR(semaphores[sem_id], &woken);
	portYIELD_FROM_ISR(woken);
}

int bench_sem_take(int sem_id)
{
	return xSemaphoreTake(semaphores[sem_id], portMAX_DELAY) == pdTRUE
	           ? BENCH_SUCCESS
	           : BENCH_ERROR;
}

int bench_mutex_create(int mutex_id)
{
	if (mutex_id < 0 || mutex_id >= MAX_MUTEXES) return BENCH_ERROR;

	mutexes[mutex_id] = xSemaphoreCreateRecursiveMutexStatic(&mutex_buffer[mutex_id]);
	return mutexes[mutex_id] != nullptr ? BENCH_SUCCESS : BENCH_ERROR;
}

int bench_mutex_lock(int mutex_id)
{
	return xSemaphoreTakeRecursive(mutexes[mutex_id], portMAX_DELAY) == pdTRUE
	           ? BENCH_SUCCESS
	           : BENCH_ERROR;
}

int bench_mutex_unlock(int mutex_id)
{
	return xSemaphoreGiveRecursive(mutexes[mutex_id]) == pdTRUE
	           ? BENCH_SUCCESS
	           : BENCH_ERROR;
}

void* bench_malloc(size_t size)
{
	if (size > bench_heap.block_size()) return nullptr;

	if (!bench_heap_ready) {
		bench_heap.init();
		bench_heap_ready = true;
	}

	return bench_heap.allocate();
}

void bench_free(void* ptr)
{
	if (ptr != nullptr) bench_heap.release(ptr);
}

int bench_message_queue_create(
    int mq_id, const char* mq_name, size_t msg_max_num, size_t msg_max_len
)
{
	(void) mq_name;

	if (mq_id < 0 || mq_id >= MAX_QUEUES || msg_max_num == 0 ||
	    msg_max_len == 0 || msg_max_num * msg_max_len > QUEUE_SIZE) {
		return BENCH_ERROR;
	}

	queues[mq_id] = xQueueCreateStatic(
	    msg_max_num, msg_max_len, queue_storage[mq_id], &queue_buffer[mq_id]
	);
	return queues[mq_id] != nullptr ? BENCH_SUCCESS : BENCH_ERROR;
}

int bench_message_queue_send(int mq_id, char* msg_ptr, size_t msg_len)
{
	(void) msg_len;
	return xQueueSend(queues[mq_id], msg_ptr, portMAX_DELAY) == pdPASS
	           ? BENCH_SUCCESS
	           : BENCH_ERROR;
}

int bench_message_queue_receive(int mq_id, char* msg_ptr, size_t msg_len)
{
	(void) msg_len;
	return xQueueReceive(queues[mq_id], msg_ptr, portMAX_DELAY) == pdPASS
	           ? BENCH_SUCCESS
	           : BENCH_ERROR;
}

int bench_message_queue_delete(int mq_id, const char* mq_name)
{
	(void) mq_name;
	vQueueDelete(queues[mq_id]);
	return BENCH_SUCCESS;
}
}
