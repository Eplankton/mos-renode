// MOS porting layer for the Thread-Metric RTOS benchmark suite.
//
// Implements the RTOS-neutral API from thread-metric/include/tm_api.h on top
// of the MOS kernel. The benchmark sources stay untouched; each test is
// linked with this port and one source file from thread-metric/src.
//
// This header is included by main.cpp rather than compiled separately: the
// MOS scheduler header defines the exception entry points, so a second
// translation unit would emit duplicate SVC/PendSV/SysTick handlers.

#ifndef _MOS_THREAD_METRIC_PORT_
#define _MOS_THREAD_METRIC_PORT_

#include "kernel.hpp"
#include "stm32_ll.hpp" // From the board directory
#include "tm_api.h"
#include "../common/block_pool.hpp"

#include <cstdint>
#include <cstring>

namespace
{
	using namespace MOS;
	using namespace Kernel;

	constexpr int TM_MAX_THREADS    = 10;
	constexpr int TM_MAX_QUEUES     = 1;
	constexpr int TM_MAX_SEMAPHORES = 1;
	constexpr int TM_MAX_POOLS      = 1;

	constexpr uint32_t TM_QUEUE_DEPTH = 10;
	constexpr uint32_t TM_QUEUE_WORDS = 4;

	constexpr uint32_t TM_BLOCK_SIZE  = 128;
	constexpr uint32_t TM_BLOCK_COUNT = 2048 / TM_BLOCK_SIZE;

	// Extension IRQ used to benchmark the real exception entry/exit path.
	// It is never enabled by the board support code, so any pending bit comes
	// from tm_cause_interrupt().
	constexpr IRQn_Type TM_IRQn = EXTI0_IRQn;

	struct TmMessage
	{
		unsigned long word[TM_QUEUE_WORDS];
	};

	// A binary semaphore with count 0. `up()` records one resume token and
	// `down()` blocks until a token is available.
	struct TmThread
	{
		Sync::Sema_t resume {0};
		Task::TcbPtr_t tcb = nullptr;
		void (*entry)()    = nullptr;
	};

	struct TmSemaphore
	{
		Sync::Sema_t sema {1}; // Thread-Metric semaphores start available
	};

	TmThread threads[TM_MAX_THREADS];
	TmSemaphore semaphores[TM_MAX_SEMAPHORES];
	IPC::MsgQueue_t<TmMessage, TM_QUEUE_DEPTH> queue;

	Bench::BlockPool_t<TM_BLOCK_SIZE, TM_BLOCK_COUNT> pools[TM_MAX_POOLS];

	volatile bool tm_in_isr = false;

	const char* const thread_names[TM_MAX_THREADS] = {
	    "tm0", "tm1", "tm2", "tm3", "tm4",
	    "tm5", "tm6", "tm7", "tm8", "tm9",
	};

	bool is_isr()
	{
		return tm_in_isr || Arch::in_isr();
	}

	void thread_trampoline(TmThread& thread)
	{
		thread.resume.down();

		if (thread.entry != nullptr) {
			thread.entry();
		}

		Task::exit();
	}
}

extern "C" {
// The two interrupt hooks are supplied by the individual tests. Declaring
// them weak (rather than defining no-ops here) keeps the strong definitions
// visible to the linker even with LTO enabled.
void tm_interrupt_handler(void) __attribute__((weak));
void tm_interrupt_preemption_handler(void) __attribute__((weak));

void EXTI0_IRQHandler(void)
{
	tm_in_isr = true;
	if (tm_interrupt_handler != nullptr) tm_interrupt_handler();
	if (tm_interrupt_preemption_handler != nullptr) {
		tm_interrupt_preemption_handler();
	}
	tm_in_isr = false;
	// No explicit PendSV here: `up_from_isr()` requests the switch itself.
}

void tm_initialize(void (*test_initialization_function)(void))
{
	NVIC_SetPriority(TM_IRQn, 0);
	NVIC_EnableIRQ(TM_IRQn);

	// Run the test's thread setup before scheduling starts. Scheduler::launch
	// only treats its argument as the idle hook, and creating tasks from the
	// idle task would let the first resumed worker preempt the rest of setup.
	test_initialization_function();
	Scheduler::launch();
}

int tm_thread_create(int thread_id, int priority, void (*entry_function)(void))
{
	if (thread_id < 0 || thread_id >= TM_MAX_THREADS ||
	    priority < 1 || priority > 31 || entry_function == nullptr) {
		return TM_ERROR;
	}

	auto& thread = threads[thread_id];
	if (thread.tcb != nullptr) return TM_ERROR;

	thread.entry = entry_function;
	thread.tcb   = Task::create(
	    thread_trampoline, &thread, static_cast<Task::Prior_t>(priority - 1),
	    thread_names[thread_id]
	);

	return thread.tcb != nullptr ? TM_SUCCESS : TM_ERROR;
}

int tm_thread_resume(int thread_id)
{
	if (thread_id < 0 || thread_id >= TM_MAX_THREADS ||
	    threads[thread_id].tcb == nullptr) {
		return TM_ERROR;
	}

	auto& resume = threads[thread_id].resume;

	if (is_isr()) {
		resume.up_from_isr();
	}
	else {
		resume.up();
	}

	return TM_SUCCESS;
}

int tm_thread_suspend(int thread_id)
{
	if (thread_id < 0 || thread_id >= TM_MAX_THREADS ||
	    threads[thread_id].tcb == nullptr) {
		return TM_ERROR;
	}

	// The Thread-Metric tests only suspend the calling thread.
	if (Task::current() != threads[thread_id].tcb) return TM_ERROR;

	threads[thread_id].resume.down();
	return TM_SUCCESS;
}

void tm_thread_relinquish(void) { Task::yield(); }

void tm_thread_sleep(int seconds)
{
	Task::delay(static_cast<Task::Tick_t>(seconds) * MOS_CONF_SYSTICK);
}

int tm_queue_create(int queue_id)
{
	return (queue_id >= 0 && queue_id < TM_MAX_QUEUES) ? TM_SUCCESS : TM_ERROR;
}

int tm_queue_send(int queue_id, unsigned long* message_ptr)
{
	if (queue_id < 0 || queue_id >= TM_MAX_QUEUES || message_ptr == nullptr) {
		return TM_ERROR;
	}

	TmMessage message;
	std::memcpy(message.word, message_ptr, sizeof(message.word));

	return queue.send(message, 0) == IPC::MsgQueue_t<TmMessage, TM_QUEUE_DEPTH>::Ok
	           ? TM_SUCCESS
	           : TM_ERROR;
}

int tm_queue_receive(int queue_id, unsigned long* message_ptr)
{
	if (queue_id < 0 || queue_id >= TM_MAX_QUEUES || message_ptr == nullptr) {
		return TM_ERROR;
	}

	auto result = queue.recv(0);
	if (result.status != IPC::MsgQueue_t<TmMessage, TM_QUEUE_DEPTH>::Ok) {
		return TM_ERROR;
	}

	std::memcpy(message_ptr, result.msg.word, sizeof(result.msg.word));
	return TM_SUCCESS;
}

int tm_semaphore_create(int semaphore_id)
{
	if (semaphore_id < 0 || semaphore_id >= TM_MAX_SEMAPHORES) {
		return TM_ERROR;
	}

	Utils::IrqGuard_t guard;
	semaphores[semaphore_id].sema.cnt = 1;
	return TM_SUCCESS;
}

int tm_semaphore_get(int semaphore_id)
{
	if (semaphore_id < 0 || semaphore_id >= TM_MAX_SEMAPHORES) {
		return TM_ERROR;
	}

	return semaphores[semaphore_id].sema.try_down() ? TM_SUCCESS : TM_ERROR;
}

int tm_semaphore_put(int semaphore_id)
{
	if (semaphore_id < 0 || semaphore_id >= TM_MAX_SEMAPHORES) {
		return TM_ERROR;
	}

	auto& sema = semaphores[semaphore_id].sema;

	if (is_isr()) {
		sema.up_from_isr();
	}
	else {
		sema.up();
	}

	return TM_SUCCESS;
}

int tm_memory_pool_create(int pool_id)
{
	if (pool_id < 0 || pool_id >= TM_MAX_POOLS) return TM_ERROR;

	pools[pool_id].init();
	return TM_SUCCESS;
}

int tm_memory_pool_allocate(int pool_id, unsigned char** memory_ptr)
{
	if (pool_id < 0 || pool_id >= TM_MAX_POOLS || memory_ptr == nullptr) {
		return TM_ERROR;
	}

	auto block = pools[pool_id].allocate();
	if (block == nullptr) return TM_ERROR;

	*memory_ptr = static_cast<unsigned char*>(block);
	return TM_SUCCESS;
}

int tm_memory_pool_deallocate(int pool_id, unsigned char* memory_ptr)
{
	if (pool_id < 0 || pool_id >= TM_MAX_POOLS || memory_ptr == nullptr) {
		return TM_ERROR;
	}

	pools[pool_id].release(memory_ptr);
	return TM_SUCCESS;
}

void tm_cause_interrupt(void)
{
	NVIC_SetPendingIRQ(TM_IRQn);
	__DSB();
	__ISB();
}

void tm_cause_interrupt_sync(void)
{
	// Run the ISR body on the caller's stack, with interrupts masked so no
	// real IRQ or context switch can race the benchmark-only ISR flag.
	const bool irq_was_enabled = (__get_PRIMASK() == 0);

	__disable_irq();
	tm_in_isr = true;

	if (tm_interrupt_handler != nullptr) tm_interrupt_handler();
	if (tm_interrupt_preemption_handler != nullptr) {
		tm_interrupt_preemption_handler();
	}

	tm_in_isr = false;

	// Any switch requested by the handler runs once IRQs are unmasked.
	if (irq_was_enabled) __enable_irq();
}
}

#endif // _MOS_THREAD_METRIC_PORT_
