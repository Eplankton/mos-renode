#ifndef _MOS_TESTS_KERNEL_
#define _MOS_TESTS_KERNEL_

#include "kernel.hpp"
#include <cstdint>

namespace MOS::Test
{
	using namespace Kernel;
	using namespace Utils;

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

		Task::create(launch, Macro::PRI_MAX, "msg_q/test");
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
	// a lower-priority task is busy. (PP / RM / DM)
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

		auto low = Task::create(low_task, 10, "pre/low");
		Task::create(high_task, 1, "pre/high");

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

	// COOP: no tick preemption. The busy medium task must finish before
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

		Task::create(med_task, 10, "coo/med");
		Task::create(high_task, 1, "coo/high");

		const auto end = Global::os_ticks + 600_ms;
		while (high_ran == 0 && (int32_t) (Global::os_ticks - end) < 0) {
			Task::delay(10_ms);
		}

		const bool ok = high_ran != 0 && med_done != 0 && high_ran >= med_done;
		kprintf(
		    "[TEST] COOP: med done @%u, high @%u -> %s\n",
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

		if constexpr (P != Policy::EDF) {
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
	// the lock. (PP / RM / DM)
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

		Task::create(low_task, P_L, "pip/low");
		Task::create(med_task, P_M, "pip/med");
		Task::create(ha_task, P_HA, "pip/ha");
		Task::create(hb_task, P_HB, "pip/hb");

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

	// Spin until `ticks` have elapsed (keeps the CPU, unlike Task::delay).
	inline void busy_for(Task::Tick_t ticks)
	{
		const auto end = Global::os_ticks + ticks;
		while ((int32_t) (Global::os_ticks - end) < 0) MOS_NOP();
	}

	// Wait (sleeping) until `flag` is set or `ticks` elapse.
	inline void wait_flag(volatile Task::Tick_t& flag, Task::Tick_t ticks)
	{
		const auto end = Global::os_ticks + ticks;
		while (flag == 0 && (int32_t) (Global::os_ticks - end) < 0) {
			Task::delay(10_ms);
		}
	}

	// PIP: two waiters boost the owner through the SAME mutex. After the
	// unlock the owner must drop all the way back to its base priority.
	inline void pip_multi_waiter_test()
	{
		constexpr Task::Prior_t P_H2 = 2, P_H1 = 5, P_L = 10;

		static Sync::Mutex_t mtx;
		static volatile Task::Tick_t low_done = 0;
		static volatile Task::Prior_t peak = 0, after = 0;

		auto low_task = [] {
			mtx.hold([] {
				busy_for(120_ms);
				peak = Task::current()->get_pri();
			});
			after    = Task::current()->get_pri();
			low_done = Global::os_ticks;
		};

		auto h1_task = [] { Task::delay(30_ms); mtx.hold([] {}); };
		auto h2_task = [] { Task::delay(60_ms); mtx.hold([] {}); };

		Task::create(low_task, P_L, "pmw/low");
		Task::create(h1_task, P_H1, "pmw/h1");
		Task::create(h2_task, P_H2, "pmw/h2");

		wait_flag(low_done, 1000_ms);
		Task::delay(20_ms); // Let h1/h2 finish

		const bool ok = low_done != 0 && peak == P_H2 && after == P_L;
		kprintf(
		    "[TEST] PIP multi-waiter: peak=%d after=%d -> %s\n",
		    peak, after, ok ? "PASS" : "FAIL"
		);
	}

	// PIP: releasing an uncontended mutex must not drop a boost that another,
	// still-held mutex is responsible for.
	inline void pip_unrelated_unlock_test()
	{
		constexpr Task::Prior_t P_H = 2, P_L = 10;

		static Sync::Mutex_t mtx_a, mtx_b;
		static volatile Task::Tick_t low_done = 0;
		static volatile Task::Prior_t mid = 0;

		auto low_task = [] {
			{
				auto guard_a = mtx_a.lock();
				{
					auto guard_b = mtx_b.lock();
					busy_for(60_ms); // H blocks on A meanwhile
				} // B was never contended
				mid = Task::current()->get_pri();
				busy_for(20_ms);
			}
			low_done = Global::os_ticks;
		};

		auto h_task = [] { Task::delay(30_ms); mtx_a.hold([] {}); };

		Task::create(low_task, P_L, "pul/low");
		Task::create(h_task, P_H, "pul/h");

		wait_flag(low_done, 1000_ms);
		Task::delay(20_ms);

		const bool ok = low_done != 0 && mid == P_H;
		kprintf(
		    "[TEST] PIP unrelated unlock: pri after B=%d -> %s\n",
		    mid, ok ? "PASS" : "FAIL"
		);
	}

	// PIP: H waits on B held by M, M waits on A held by L. H's priority must
	// reach L through the chain.
	inline void pip_transitive_test()
	{
		constexpr Task::Prior_t P_H = 1, P_M = 5, P_L = 10;

		static Sync::Mutex_t mtx_a, mtx_b;
		static volatile Task::Tick_t low_done = 0;
		static volatile Task::Prior_t low_peak = 0;

		auto low_task = [] {
			mtx_a.hold([] {
				busy_for(120_ms);
				low_peak = Task::current()->get_pri();
			});
			low_done = Global::os_ticks;
		};

		auto m_task = [] {
			Task::delay(20_ms);
			mtx_b.hold([] { mtx_a.hold([] {}); });
		};

		auto h_task = [] { Task::delay(50_ms); mtx_b.hold([] {}); };

		Task::create(low_task, P_L, "ptr/low");
		Task::create(m_task, P_M, "ptr/m");
		Task::create(h_task, P_H, "ptr/h");

		wait_flag(low_done, 1000_ms);
		Task::delay(20_ms);

		const bool ok = low_done != 0 && low_peak == P_H;
		kprintf(
		    "[TEST] PIP transitive: low peak=%d -> %s\n",
		    low_peak, ok ? "PASS" : "FAIL"
		);
	}

	// MsgQueue: a sender woken because space became available may find the
	// queue full again (another sender got there first). It must wait again
	// instead of reporting success for a message that was dropped.
	inline void msgq_recheck_test()
	{
		using Q = IPC::MsgQueue_t<int, 1>;
		static Q q;
		static volatile Task::Tick_t sent = 0;
		static volatile bool sent_ok = false;

		auto sender = [] {
			sent_ok = q.send(1, 300_ms) == Q::Ok;
			sent    = Global::os_ticks;
		};

		q.send(0, 0);                       // Fill the queue
		Task::create(sender, 10, "mrc/s1"); // Lower priority than this task
		Task::delay(10_ms);                 // s1 blocks: queue is full

		auto r0 = q.recv(0);  // Frees the slot and wakes s1 ...
		q.send(2, 0);         // ... but this task refills it first
		Task::delay(10_ms);   // s1 runs and must find the queue full

		auto r1 = q.recv(0);    // 2, wakes s1 again
		auto r2 = q.recv(300_ms); // 1, if s1 really delivered it
		wait_flag(sent, 400_ms);

		const bool ok = r0.msg == 0 && r1.msg == 2 &&
		                sent_ok && r2.status == Q::Ok && r2.msg == 1;
		kprintf(
		    "[TEST] MsgQueue recheck: s1=%s r1=%d r2=%s/%d -> %s\n",
		    sent_ok ? "Ok" : "TimeOut", r1.msg,
		    r2.status == Q::Ok ? "Ok" : "TimeOut", r2.msg,
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

		if constexpr (DEFAULT_POLICY == Policy::PP ||
		              DEFAULT_POLICY == Policy::RM ||
		              DEFAULT_POLICY == Policy::DM) {
			preempt_test();
			nested_pip_test();
			pip_multi_waiter_test();
			pip_unrelated_unlock_test();
			pip_transitive_test();
		}

		msgq_recheck_test();

		if constexpr (DEFAULT_POLICY == Policy::COOP) {
			coop_test();
		}

		if constexpr (DEFAULT_POLICY == Policy::RM ||
		              DEFAULT_POLICY == Policy::DM ||
		              DEFAULT_POLICY == Policy::EDF) {
			deadline_order_test<DEFAULT_POLICY>();
		}

		if constexpr (DEFAULT_POLICY == Policy::PP ||
		              DEFAULT_POLICY == Policy::RM ||
		              DEFAULT_POLICY == Policy::DM ||
		              DEFAULT_POLICY == Policy::EDF) {
			periodic_timing_test();
		}

		kprintf("=== Scheduling policy tests done ===\n");
	}
}

#endif
