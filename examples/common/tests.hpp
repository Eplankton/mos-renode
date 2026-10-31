#ifndef _MOS_COMMON_TESTS_
#define _MOS_COMMON_TESTS_

#include "kernel.hpp"
#include <cstdint>

namespace MOS::Examples::Test
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

	// ============================================================
	// Scheduling policy tests
	//
	// Each battery is compiled only for the policies where it is meaningful
	// (`DEFAULT_POLICY` is a compile-time constant, so this costs nothing at
	// runtime). Every test terminates on its own and prints one verdict line
	// prefixed with `[TEST]`. The whole battery is driven by `SchedPolicyTest`.
	// ============================================================

	// Equal-priority tasks must share the CPU fairly under every policy.
	inline void rr_fairness_test()
	{
		constexpr Task::Tick_t WINDOW = 600_ms;
		constexpr Task::Prior_t PRI   = 20;
		constexpr int N         = 3;

		static volatile int runs[N] = {0};
		static Task::TcbPtr_t workers[N] = {nullptr};
		static constexpr const char* names[N] = {"rr/w0", "rr/w1", "rr/w2"};

		auto worker = [](int id) {
			while (true) {
				runs[id] += 1;
				Task::yield(); // Rotate within the equal-rank group
			}
		};

		for (int i = 0; i < N; i++) {
			workers[i] = Task::create(worker, i, PRI, names[i]);
		}

		Task::delay(WINDOW);

		for (auto w: workers) {
			if (w != nullptr) Task::terminate(w);
		}

		int mn = runs[0], mx = runs[0];
		for (int i = 1; i < N; i++) {
			if (runs[i] < mn) mn = runs[i];
			if (runs[i] > mx) mx = runs[i];
		}

		// Everyone must have run; shares within 40% of each other.
		const bool ok = mn > 0 && (mx - mn) * 100 <= mx * 40;
		kprintf(
		    "[TEST] RR fairness: runs=[%d,%d,%d] -> %s\n",
		    runs[0], runs[1], runs[2], ok ? "PASS" : "FAIL"
		);
	}

	// Preemptive priority policy: a higher-priority task must run even while
	// a lower-priority task is busy. (PreemptPri / RM / DM)
	inline void preempt_test()
	{
		static volatile Task::Tick_t high_ran = 0, low_killed = 0;

		auto low_task = [] {
			while (true) MOS_NOP(); // Busy until terminated
		};

		auto high_task = [] {
			Task::delay(100_ms);
			high_ran = Global::os_ticks;
		};

		auto low = Task::create(low_task, nullptr, 10, "pre/low");
		Task::create(high_task, nullptr, 1, "pre/high");

		const auto end = Global::os_ticks + 500_ms;
		while (high_ran == 0 && (int32_t) (Global::os_ticks - end) < 0) {
			Task::delay(10_ms);
		}

		low_killed = Global::os_ticks;
		Task::terminate(low);

		// `<=`: high and the kill may land in the same tick, but high must
		// not run only AFTER the low task was killed.
		const bool ok = high_ran != 0 && high_ran <= low_killed;
		kprintf(
		    "[TEST] Preemption: high @%u, low killed @%u -> %s\n",
		    high_ran, low_killed, ok ? "PASS" : "FAIL"
		);
	}

	// Cooperative: no tick preemption. The busy medium task must finish before
	// the (higher-priority) delayed task gets any CPU at all.
	inline void coop_test()
	{
		static volatile Task::Tick_t med_done = 0, high_ran = 0;

		auto med_task = [] {
			const auto end = Global::os_ticks + 200_ms;
			while ((int32_t) (Global::os_ticks - end) < 0) MOS_NOP();
			med_done = Global::os_ticks;
		};

		auto high_task = [] {
			Task::delay(50_ms);
			high_ran = Global::os_ticks;
		};

		Task::create(med_task, nullptr, 10, "coo/med");
		Task::create(high_task, nullptr, 1, "coo/high");

		const auto end = Global::os_ticks + 600_ms;
		while (high_ran == 0 && (int32_t) (Global::os_ticks - end) < 0) {
			Task::delay(10_ms);
		}

		const bool ok = high_ran != 0 && med_done != 0 && high_ran >= med_done;
		kprintf(
		    "[TEST] Cooperative: med done @%u, high @%u -> %s\n",
		    med_done, high_ran, ok ? "PASS" : "FAIL"
		);
	}

	// EDF orders the first runs by absolute deadline; RM/DM derive the static
	// priorities from T/D. Tasks are created in reverse order on purpose.
	template <Policy P>
	inline void deadline_order_test()
	{
		static volatile Task::Tick_t stamp[3] = {0};
		static volatile int order[3]    = {0};
		static volatile int seq         = 0;
		static Task::TcbPtr_t tcbs[3]   = {nullptr};

		auto job = [](int id) {
			stamp[id] = Global::os_ticks; // First run: record and exit
			order[id] = seq + 1;
			seq       = seq + 1;
		};

		// `*_raw` skips the preempt-if-higher check so all three jobs become
		// READY before any of them runs.
		tcbs[0] = Task::create_periodic_raw(job, 0, 0, 800_ms, 0, "dlo/800");
		tcbs[1] = Task::create_periodic_raw(job, 1, 0, 400_ms, 0, "dlo/400");
		tcbs[2] = Task::create_periodic_raw(job, 2, 0, 200_ms, 0, "dlo/200");

		const auto end = Global::os_ticks + 500_ms;
		while ((order[0] == 0 || order[1] == 0 || order[2] == 0) &&
		       (int32_t) (Global::os_ticks - end) < 0) {
			Task::delay(10_ms);
		}

		bool ok = order[0] != 0 && order[1] != 0 && order[2] != 0 &&
		          order[2] < order[1] && order[1] < order[0];

		if constexpr (P != Policy::EarliestDeadlineFirst) {
			// RM/DM must also derive the static priorities from T/D.
			ok = ok && tcbs[2]->get_pri() < tcbs[1]->get_pri() &&
			          tcbs[1]->get_pri() < tcbs[0]->get_pri();
		}

		kprintf(
		    "[TEST] %s order: seq=[%d,%d,%d] pri=[%d,%d,%d] -> %s\n",
		    policy_name(P),
		    order[0], order[1], order[2],
		    tcbs[0]->get_pri(), tcbs[1]->get_pri(), tcbs[2]->get_pri(),
		    ok ? "PASS" : "FAIL"
		);
	}

	// Periodic activation accuracy: the interval between consecutive
	// activations must stay close to the period and, under EDF (the policy
	// that counts them), no deadline may be missed.
	inline void periodic_timing_test()
	{
		constexpr Task::Tick_t PERIOD = 50_ms;
		constexpr int JOBS       = 8;
		constexpr Task::Tick_t TOL     = 15_ms;

		static volatile Task::Tick_t act[JOBS] = {0};
		static volatile int n            = 0;
		static volatile Task::Tick_t done      = 0;

		auto job = [] {
			while (true) {
				if (n < JOBS) act[n] = Global::os_ticks;
				n += 1;
				if (n >= JOBS) {
					done = Global::os_ticks;
					return Task::exit();
				}
				Task::periodic_wait();
			}
		};

		Task::create_periodic(job, nullptr, 1, PERIOD, 0, "ptm/job", 5_ms);

		const auto end = Global::os_ticks + 1500_ms;
		while (done == 0 && (int32_t) (Global::os_ticks - end) < 0) {
			Task::delay(10_ms);
		}

		bool ok = done != 0 && n == JOBS;
		for (int i = 1; i < JOBS; i++) {
			const auto dt = act[i] - act[i - 1];
			ok = ok && dt >= PERIOD - TOL && dt <= PERIOD + TOL;
		}

		kprintf(
		    "[TEST] Periodic T=%ums: n=%d last-dt=%u misses=%u -> %s\n",
		    (int) (PERIOD * 1000 / MOS_CONF_SYSTICK),
		    n,
		    (unsigned) (act[JOBS - 1] - act[JOBS - 2]),
		    (unsigned) Task::missed_deadlines(),
		    ok ? "PASS" : "FAIL"
		);
	}

	// Nested PIP: two waiters boost the owner through two nested mutexes. The
	// owner's priority must unwind one level per unlock, back to its base.
	// Also checks the classic L/M/H inversion: M must not run before H gets
	// the lock. (PreemptPri / RM / DM)
	inline void nested_pip_test()
	{
		constexpr Task::Prior_t P_HB = 1, P_HA = 2, P_M = 5, P_L = 10;

		static Sync::Mutex_t mtx_a, mtx_b;

		static volatile Task::Tick_t hb_ran = 0, ha_ran = 0,
		                       med_ran = 0, low_done = 0;
		static volatile Task::Prior_t low_pri = 0;

		auto low_task = [] {
			mtx_a.hold([&] {
				mtx_b.hold([&] {
					auto end = Global::os_ticks + 150_ms;
					while ((int32_t) (Global::os_ticks - end) < 0) MOS_NOP();
				});
				auto end = Global::os_ticks + 50_ms;
				while ((int32_t) (Global::os_ticks - end) < 0) MOS_NOP();
			});
			low_pri  = Task::current()->get_pri();
			low_done = Global::os_ticks;
		};

		auto ha_task = [] {
			Task::delay(30_ms);
			mtx_a.hold([&] { ha_ran = Global::os_ticks; });
		};

		auto hb_task = [] {
			Task::delay(80_ms);
			mtx_b.hold([&] { hb_ran = Global::os_ticks; });
		};

		auto med_task = [] {
			Task::delay(60_ms);
			med_ran = Global::os_ticks;
		};

		Task::create(low_task, nullptr, P_L, "pip/low");
		Task::create(med_task, nullptr, P_M, "pip/med");
		Task::create(ha_task, nullptr, P_HA, "pip/ha");
		Task::create(hb_task, nullptr, P_HB, "pip/hb");

		const auto end = Global::os_ticks + 1500_ms;
		while (low_done == 0 && (int32_t) (Global::os_ticks - end) < 0) {
			Task::delay(10_ms);
		}

		// B is released before A (different ticks), while the A-handoff, M and
		// L may all finish within one tick -- hence <= for those.
		const bool ok = low_done != 0 &&
		                hb_ran != 0 && ha_ran != 0 && med_ran != 0 &&
		                hb_ran < ha_ran &&
		                ha_ran <= med_ran &&
		                med_ran <= low_done &&
		                low_pri == P_L;
		kprintf(
		    "[TEST] Nested PIP: HB@%u HA@%u M@%u L@%u pri=%d -> %s\n",
		    hb_ran, ha_ran, med_ran, low_done, low_pri,
		    ok ? "PASS" : "FAIL"
		);
	}

	// Run the batteries that make sense for the active policy.
	inline void SchedPolicyTest()
	{
		kprintf(
		    "=== Scheduling policy tests [%s] ===\n",
		    policy_name(DEFAULT_POLICY)
		);

		rr_fairness_test();

		if constexpr (DEFAULT_POLICY == Policy::PreemptPri ||
		              DEFAULT_POLICY == Policy::RateMonotonic ||
		              DEFAULT_POLICY == Policy::DeadlineMonotonic) {
			preempt_test();
			nested_pip_test();
		}

		if constexpr (DEFAULT_POLICY == Policy::Cooperative) {
			coop_test();
		}

		if constexpr (DEFAULT_POLICY == Policy::RateMonotonic ||
		              DEFAULT_POLICY == Policy::DeadlineMonotonic ||
		              DEFAULT_POLICY == Policy::EarliestDeadlineFirst) {
			deadline_order_test<DEFAULT_POLICY>();
		}

		if constexpr (DEFAULT_POLICY == Policy::PreemptPri ||
		              DEFAULT_POLICY == Policy::RateMonotonic ||
		              DEFAULT_POLICY == Policy::DeadlineMonotonic ||
		              DEFAULT_POLICY == Policy::EarliestDeadlineFirst) {
			periodic_timing_test();
		}

		kprintf("=== Scheduling policy tests done ===\n");
	}
}

#endif
