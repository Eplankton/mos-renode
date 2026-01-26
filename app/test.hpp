#ifndef _MOS_USER_TEST_
#define _MOS_USER_TEST_

#include "../core/kernel.hpp"

namespace MOS::User::Test
{
	using namespace Kernel;
	using namespace Utils;

	void MutexTest(size_t scale)
	{
		static Sync::Mutex_t mutex;

		static auto mtx_test = [](uint32_t ticks) {
			auto cur = Task::current();
			while (true) {
				mutex.hold([&] {
					for (auto i: Range(0, 5)) {
						kprintf(
						    "Mutex(%d => %d), [%d] locked\n",
						    cur->sub_pri, cur->pri, i + 1
						);
						Task::delay(100_ms);
					}
				});
				Task::delay(ticks);
			}
		};

		static auto launch = [](size_t scale) {
			for (auto i = scale; i > 0; i -= 1) {
				Task::create(mtx_test, i * 10_ms, i, "mtx");
				Task::delay(5_ms);
			}
		};

		Task::create(launch, scale, Macro::PRI_MAX, "mutex/test");
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

	void AsyncTest(const size_t scale)
	{
		using Async::Future_t;

		static auto sum_worker = [](const size_t n) -> Future_t<f32> {
			uint32_t result = 0;
			uint32_t rng    = n * 63641362238ULL + 1; // RNG Seed

			for (uint32_t i = 0; i < n; i++) {
				result += i;
				rng = rng * 63641365ULL + os_ticks; // RNG Generator
				if ((rng % n) == 0) {
					co_await Async::delay(1000_ms + 10 * (rng % 13));
				}
			}
			co_return result;
		};

		static size_t cnt = scale;

		auto sum = [](const size_t n) -> Future_t<> {
			auto val = co_await sum_worker(n) * 1.234f;
			LOG("sum(%d) => %.3f", n, val);
			if (--cnt <= 0) {
				LOG("All Async Jobs Are Done!");
			}
		};

		// Launch all coroutines
		for (size_t k = scale; k > 0; k -= 1) {
			auto fut = sum(k); // lazy calculation
		}
	}
}

#endif
