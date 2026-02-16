#ifndef _MOS_USER_TEST_
#define _MOS_USER_TEST_

#include "../core/kernel.hpp"
#include <cstdint>

namespace MOS::User::Test
{
	using namespace Kernel;
	using namespace Utils;

	void MutexTest(size_t scale)
	{
		// ==========================================
		// 1. PIP Test (L/M/H Inversion Scenario)
		// ==========================================
		static auto pip_test = [] {
			static Sync::Mutex_t mutex;

			static const auto PRI_H = 1;
			static const auto PRI_M = 2;
			static const auto PRI_L = 3;

			kprintf("\n=== [Test 1] PIP Inversion Test (L/M/H) ===\n");
			kprintf("Expect: L(P%d) locks -> H(P%d) runs & boosts L -> M(P%d) cannot preempt L\n", PRI_L, PRI_H, PRI_M);

			// --- Low Priority Task ---
			auto task_l = [] {
				mutex.hold([] {
					kprintf("[L] Locked. Pri=%d. Busy Waiting 500ms for H/M...\n", Task::current()->get_pri());

					// Busy waiting: hog CPU to test if H can preempt
					auto start = Kernel::Global::os_ticks;
					while (Kernel::Global::os_ticks < start + 500_ms) {
						MOS_NOP();
					}

					kprintf("[L] Resumed. Checking PIP status...\n");
					auto cur = Task::current();

					// Check Point: H should have run and boosted priority
					if (cur->get_pri() <= PRI_H) {
						kprintf("[L] PASS: Priority Boosted to %d (Matched H)\n", cur->get_pri());
					}
					else {
						kprintf("[L] FAIL: Priority is %d. (H didn't run)\n", cur->get_pri());
					}

					kprintf("[L] Working (M should NOT preempt)...\n");
					for (int i = 0; i < 3; i++) {
						Utils::delay(100_ms);
					}
					kprintf("[L] Unlock\n");
				});
				kprintf("[L] Finished. Pri restored to %d\n", Task::current()->get_pri());
			};

			// --- High Priority Task ---
			auto task_h = [] {
				Task::delay(100_ms); // Let L run first

				// If this line is not printed, preemption failed
				kprintf("[H] Started! Trying to lock (Should boost L)...\n");

				mutex.hold([] {
					kprintf("[H] Get Lock!\n");
				});

				kprintf("[H] Finished\n");
			};

			// --- Medium Priority Task ---
			auto task_m = [] {
				Task::delay(200_ms);
				kprintf("[M] Started.\n"); // If this line prints before L Unlock, PIP failed
				kprintf("[M] Done! (Should be LAST)\n");
			};

			Task::create(task_h, nullptr, PRI_H, "mtx/H");
			Task::create(task_m, nullptr, PRI_M, "mtx/M");
			Task::create(task_l, nullptr, PRI_L, "mtx/L");
		};

		Task::create(pip_test, 0, Macro::PRI_MIN, "pip_root");

		// // ==========================================
		// // 2. Stress Test
		// // ==========================================
		// static auto stress_test = [] {
		// 	kprintf("\n=== [Test 2] Concurrency Stress Test ===\n");

		// 	static Sync::Mutex_t stress_mtx;
		// 	static volatile int cnt = 0;
		// 	const int TASKS_NUM     = 4;
		// 	const int INC_PER_TASK  = 1000;

		// 	static auto worker = [](int count) {
		// 		for (int i = 0; i < count; i++) {
		// 			stress_mtx.hold([&] {
		// 				volatile int temp = cnt;
		// 				cnt               = temp + 1;
		// 			});
		// 			if (i % 100 == 0) Task::yield(); // Rapid Shuffle
		// 		}
		// 	};

		// 	for (int i = 0; i < TASKS_NUM; i++) {
		// 		Task::create(worker, INC_PER_TASK, 2, "worker");
		// 	}

		// 	// Wait until all done
		// 	Task::delay(1000_ms);

		// 	kprintf("Expect: %d\n", TASKS_NUM * INC_PER_TASK);
		// 	kprintf("Actual: %d\n", cnt);

		// 	if (cnt == TASKS_NUM * INC_PER_TASK) {
		// 		kprintf("[PASS] Done.\n");
		// 	}
		// 	else {
		// 		kprintf("[FAIL] Race Condition Detected!\n");
		// 	}
		// };

		// Task::create(stress_test, scale, Macro::PRI_MAX, "mtx/test");
	}

	template <size_t NUM>
	void MsgQueueTest()
	{
		using MsgQue_t = IPC::MsgQueue_t<int, NUM>;

		static auto producer = [](MsgQue_t& msg_q) {
			uint32_t i = Task::current()->get_pri();
			while (true) {
				msg_q.send(i++);
				Task::delay(50_ms);
			}
		};

		static auto consumer = [](MsgQue_t& msg_q) {
			while (true) {
				auto [status, msg] = msg_q.recv(200_ms);
				IrqGuard_t guard;
				kprintf(status ? "" : "MsgQ Timeout!\n");
				// kprintf(status ? "%d, " : "MsgQ Timeout!\n", msg);
			}
		};

		static auto launch = [] {
			static MsgQue_t channel;                    // Create a static MsgQueue
			constexpr Task::Prior_t base_pri = NUM + 1; // Set base priority

			Task::create(consumer, &channel, base_pri, "msg_q/recv"); // Create a Consumer

			for (auto p = base_pri; p <= base_pri * 2; p += 1) {
				Task::create(producer, &channel, p, "msg_q/send"); // Create multiple Producers
			}
		};

		Task::create(launch, nullptr, Macro::PRI_MAX, "msg_q/test");
	}

	template <size_t NUM>
	void AsyncTest()
	{
		using Async::Future_t;
		static size_t cnt = NUM;

		auto sum = [](const size_t n) -> Future_t<> {
			f32 result   = 0;
			uint32_t rng = n * 63641362238ULL + 1; // RNG Seed

			// Simualte a large calculation
			for (size_t i = 0; i < n; i++) {
				result += i;
				rng = rng * 63641365ULL + os_ticks; // RNG Generator
				if ((rng % n) == 0) {
					co_await Async::delay(1000_ms + 10 * (rng % 13));
				}
			}

			LOG("sum(%d) => %.3f", n, result * 1.234f);

			if (--cnt <= 0) {
				LOG("All Async Sum Jobs Are Done!");
			}
		};

		// Launch all coroutines
		for (size_t k = NUM; k > 0; k -= 1) {
			auto fut = sum(k); // lazy calculation
		}
	}
}

#endif
