// SPDX-License-Identifier: Apache-2.0
//
// MOS porting layer for the RTOS-Benchmark suite from
// https://github.com/zephyrproject-rtos/rtos-benchmark.
//
// The common benchmark sources are compiled unmodified. This header implements
// the bench_api.h primitives with MOS tasks, semaphores, mutexes, message
// queues; the timing half (DWT/TIM2, SysTick) is bench_timer_cortex_m.hpp,
// shared with the FreeRTOS reference port.
//
// It is included by main.cpp rather than compiled separately: the MOS
// scheduler header defines the exception entry points, so a second translation
// unit would emit duplicate SVC/PendSV/SysTick handlers.

#ifndef BENCH_PORTING_LAYER_MOS_IMPL_HPP
#define BENCH_PORTING_LAYER_MOS_IMPL_HPP

#include "bench_porting_layer_mos.h"

// bench_api.h has no extern "C" wrapper and is called from C sources.
extern "C" {
#include "bench_api.h"
}

#include "kernel.hpp"
#include "../common/block_pool.hpp"

#define BENCH_TICK_HZ MOS_CONF_SYSTICK
#include "bench_timer_cortex_m.hpp"

#include <cstring>
#include <new>

namespace
{
	using namespace MOS;
	using namespace Kernel;

	constexpr int MAX_THREADS    = 10;
	constexpr int MAX_SEMAPHORES = 5;
	constexpr int MAX_MUTEXES    = 1;
	constexpr int MAX_QUEUES     = 1;

	struct BenchThread
	{
		// The entry trampoline waits for a start token, then for suspend
		// tokens. This mirrors the create-suspended/start semantics the
		// benchmark expects from Zephyr and FreeRTOS.
		Sync::Sema_t start {0};
		Sync::Sema_t suspend {0};

		Task::TcbPtr_t tcb = nullptr;
		void (*entry)(void*) = nullptr;
		void* args = nullptr;
		volatile bool suspended = false;
	};

	struct BenchSemaphore
	{
		Sync::Sema_t sema {0};
	};

	BenchThread threads[MAX_THREADS];
	BenchSemaphore semaphores[MAX_SEMAPHORES];

	alignas(Sync::MutexImpl_t) uint8_t mutex_storage[MAX_MUTEXES]
	    [sizeof(Sync::MutexImpl_t)];
	Sync::MutexImpl_t* mutex_ptr = nullptr;

	using BenchQueue = IPC::MsgQueue_t<char, 1>;
	alignas(BenchQueue) uint8_t queue_storage[MAX_QUEUES][sizeof(BenchQueue)];
	BenchQueue* queue_ptr = nullptr;

	// Fixed-size heap used by the malloc/free benchmark. MOS has no general
	// purpose heap, so this measures an O(1) 128-byte block allocator.
	Bench::BlockPool_t<128, 64> bench_heap;
	bool bench_heap_ready = false;

	void clear_thread(int id)
	{
		threads[id].tcb       = nullptr;
		threads[id].suspended = false;
		threads[id].entry     = nullptr;
		threads[id].args      = nullptr;
	}

	void thread_trampoline(BenchThread& thread)
	{
		thread.start.down();

		while (thread.suspended) {
			thread.suspend.down();
		}

		if (thread.entry != nullptr) {
			thread.entry(thread.args);
		}

		// Entry functions normally call bench_thread_exit(). Handle a plain
		// return as well so helper threads can never fall through.
		bench_thread_exit();
	}

	void (*bench_entry)(void*) = nullptr;

	void bench_task()
	{
		if (bench_entry != nullptr) bench_entry(nullptr);

		PRINTF("\n*** RTOS-BENCHMARK DONE ***\n");
		Task::exit();
	}
}

extern "C" {

void bench_test_init(void (*test_init_function)(void*))
{
	bench_entry = test_init_function;

	Task::create(
	    bench_task,
	    static_cast<Task::Prior_t>(BENCH_LAST_PRIORITY - 1), "benchmark"
	);

	Scheduler::launch();
}

void bench_thread_set_priority(int priority)
{
	if (priority < Macro::PRI_MAX) priority = Macro::PRI_MAX;
	if (priority > Macro::PRI_MIN) priority = Macro::PRI_MIN;

	Task::change_pri(
	    Task::current(), static_cast<Task::Prior_t>(priority)
	);
}

int bench_thread_create(
    int thread_id, const char* thread_name, int priority,
    void (*entry_function)(void*), void* args
)
{
	if (thread_id < 0 || thread_id >= MAX_THREADS ||
	    priority < 0 || priority > Macro::PRI_MIN ||
	    entry_function == nullptr) {
		return BENCH_ERROR;
	}

	auto& thread = threads[thread_id];
	if (thread.tcb != nullptr) return BENCH_ERROR;

	thread.start.cnt    = 0;
	thread.suspend.cnt  = 0;
	thread.suspended    = false;
	thread.entry        = entry_function;
	thread.args         = args;

	thread.tcb = Task::create(
	    thread_trampoline, &thread, static_cast<Task::Prior_t>(priority),
	    thread_name != nullptr ? thread_name : "bench"
	);

	return thread.tcb != nullptr ? BENCH_SUCCESS : BENCH_ERROR;
}

int bench_thread_spawn(
    int thread_id, const char* thread_name, int priority,
    void (*entry_function)(void*), void* args
)
{
	const auto status = bench_thread_create(
	    thread_id, thread_name, priority, entry_function, args
	);
	if (status != BENCH_SUCCESS) return status;

	bench_thread_start(thread_id);
	return BENCH_SUCCESS;
}

void bench_thread_start(int thread_id)
{
	if (thread_id < 0 || thread_id >= MAX_THREADS) return;
	if (threads[thread_id].tcb == nullptr) return;

	threads[thread_id].start.up();
}

void bench_thread_resume(int thread_id)
{
	if (thread_id < 0 || thread_id >= MAX_THREADS) return;

	auto& thread = threads[thread_id];
	if (thread.tcb == nullptr || !thread.suspended) return;

	thread.suspended = false;
	thread.suspend.up();
}

void bench_thread_suspend(int thread_id)
{
	if (thread_id < 0 || thread_id >= MAX_THREADS) return;

	auto& thread = threads[thread_id];
	if (thread.tcb == nullptr) return;

	thread.suspended = true;

	if (Task::current() == thread.tcb) {
		thread.suspend.down();
		thread.suspended = false;
	}
}

void bench_thread_abort(int thread_id)
{
	if (thread_id < 0 || thread_id >= MAX_THREADS) return;

	auto& thread = threads[thread_id];
	auto tcb     = thread.tcb;
	if (tcb == nullptr) return;

	clear_thread(thread_id);
	Task::terminate(tcb);
}

void bench_thread_exit(void)
{
	auto current = Task::current();

	for (int i = 0; i < MAX_THREADS; ++i) {
		if (threads[i].tcb == current) {
			clear_thread(i);
			break;
		}
	}

	Task::exit();
}

void bench_yield(void) { Task::yield(); }

void bench_sync_ticks(void) { Task::delay(1); }

int bench_sem_create(int sem_id, int initial_count, int maximum_count)
{
	(void) maximum_count;

	if (sem_id < 0 || sem_id >= MAX_SEMAPHORES) return BENCH_ERROR;

	Utils::IrqGuard_t guard;
	semaphores[sem_id].sema.cnt = initial_count;
	return BENCH_SUCCESS;
}

void bench_sem_give(int sem_id)
{
	if (sem_id < 0 || sem_id >= MAX_SEMAPHORES) return;
	semaphores[sem_id].sema.up();
}

void bench_sem_give_from_isr(int sem_id)
{
	if (sem_id < 0 || sem_id >= MAX_SEMAPHORES) return;
	semaphores[sem_id].sema.up_from_isr();
}

int bench_sem_take(int sem_id)
{
	if (sem_id < 0 || sem_id >= MAX_SEMAPHORES) return BENCH_ERROR;

	semaphores[sem_id].sema.down();
	return BENCH_SUCCESS;
}

int bench_mutex_create(int mutex_id)
{
	if (mutex_id < 0 || mutex_id >= MAX_MUTEXES) return BENCH_ERROR;

	if (mutex_ptr != nullptr) mutex_ptr->~MutexImpl_t();
	mutex_ptr = new (mutex_storage[mutex_id]) Sync::MutexImpl_t();
	return BENCH_SUCCESS;
}

int bench_mutex_lock(int mutex_id)
{
	if (mutex_id < 0 || mutex_id >= MAX_MUTEXES ||
	    mutex_ptr == nullptr) {
		return BENCH_ERROR;
	}

	mutex_ptr->lock();
	return BENCH_SUCCESS;
}

int bench_mutex_unlock(int mutex_id)
{
	if (mutex_id < 0 || mutex_id >= MAX_MUTEXES ||
	    mutex_ptr == nullptr) {
		return BENCH_ERROR;
	}

	mutex_ptr->unlock();
	return BENCH_SUCCESS;
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

	if (mq_id < 0 || mq_id >= MAX_QUEUES ||
	    msg_max_num == 0 || msg_max_len == 0) {
		return BENCH_ERROR;
	}

	if (queue_ptr != nullptr) queue_ptr->~BenchQueue();
	queue_ptr = new (queue_storage[mq_id]) BenchQueue();
	return BENCH_SUCCESS;
}

int bench_message_queue_send(int mq_id, char* msg_ptr, size_t msg_len)
{
	if (mq_id < 0 || mq_id >= MAX_QUEUES || queue_ptr == nullptr ||
	    msg_ptr == nullptr || msg_len == 0) {
		return BENCH_ERROR;
	}

	return queue_ptr->send(msg_ptr[0], IPC::FOREVER) == BenchQueue::Ok
	           ? BENCH_SUCCESS
	           : BENCH_ERROR;
}

int bench_message_queue_receive(int mq_id, char* msg_ptr, size_t msg_len)
{
	if (mq_id < 0 || mq_id >= MAX_QUEUES || queue_ptr == nullptr ||
	    msg_ptr == nullptr || msg_len == 0) {
		return BENCH_ERROR;
	}

	auto result = queue_ptr->recv(IPC::FOREVER);
	if (result.status != BenchQueue::Ok) return BENCH_ERROR;

	msg_ptr[0] = result.msg;
	return BENCH_SUCCESS;
}

int bench_message_queue_delete(int mq_id, const char* mq_name)
{
	(void) mq_name;

	if (mq_id < 0 || mq_id >= MAX_QUEUES) return BENCH_ERROR;

	if (queue_ptr != nullptr) {
		queue_ptr->~BenchQueue();
		queue_ptr = nullptr;
	}

	return BENCH_SUCCESS;
}
}

#endif // BENCH_PORTING_LAYER_MOS_IMPL_HPP
