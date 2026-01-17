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
		using MsgQ_t = IPC::MsgQueue_t<int, NUM>;

		static auto producer = [](MsgQ_t& msg_q) {
			uint32_t i = Task::current()->get_pri();
			while (true) {
				msg_q.send(i++);
				Task::delay(50_ms);
			}
		};

		static auto consumer = [](MsgQ_t& msg_q) {
			while (true) {
				auto [status, msg] = msg_q.recv(200_ms);
				IrqGuard_t guard;
				kprintf(status ? "" : "MsgQ Timeout!\n");
				// kprintf(status ? "%d, " : "MsgQ Timeout!\n", msg);
			}
		};

		static auto launch = [] {
			static MsgQ_t channel; // Create a static MsgQueue
			constexpr Task::Prior_t base_pri = NUM + 1; // Set base priority
			Task::create(consumer, &channel, base_pri, "recv"); // Create a Consumer

			for (auto p = base_pri; p <= base_pri * 2; p += 1) {
				Task::create(producer, &channel, p, "send"); // Create multiple Producers
			}
		};

		Task::create(launch, nullptr, Macro::PRI_MAX, "msg_q/test");
	}

	void AsyncTest(const size_t scale)
	{
		using Async::Future_t;

		auto sum_worker = [](const size_t n) -> Future_t<int> {
			uint32_t result = 0;
			for (uint32_t i = 0; i < n; i += 1) {
				result += i;
				co_await Async::delay(1000_ms / n);
			}
			co_return result;
		};

		auto sum = [&](const size_t n) -> Future_t<> {
			LOG("sum(%d) => %d", n, co_await sum_worker(n));
		};

		// launch all coroutines
		for (size_t k = scale; k > 0; k -= 1) {
			auto fut = sum(k); // lazy calculation
		}
	}
}

#endif
