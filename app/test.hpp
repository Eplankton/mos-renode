#ifndef _MOS_USER_TEST_
#define _MOS_USER_TEST_

#include "../mos/kernel/task.hpp"
#include "../mos/kernel/sync.hpp"
#include "../mos/kernel/ipc.hpp"

namespace MOS::User::Test
{
	using namespace Kernel;
	using namespace Utils;

	void MutexTest()
	{
		static Sync::Mutex_t mutex;

		static auto mtx_test = [](uint32_t ticks) {
			auto name = Task::current()->get_name();
			while (true) {
				mutex.exec([&] {
					for (auto _: Range(0, 5)) {
						kprintf("%s is working...\n", name);
						Task::delay(100_ms);
					}
				});
				Task::delay(ticks);
			}
		};

		static auto launch = [] {
			Task::create(mtx_test, 10_ms, 3, "Mtx3");
			Task::delay(5_ms);
			Task::create(mtx_test, 20_ms, 2, "Mtx2");
			Task::delay(5_ms);
			Task::create(mtx_test, 30_ms, 1, "Mtx1");
		};

		Task::create(launch, nullptr, Macro::PRI_MAX, "MtxTest");
	}

	void MsgQueueTest()
	{
		using MsgQ_t = IPC::MsgQueue_t<int, 3>;

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
			static MsgQ_t msg_q; // Create a static MsgQueue
			Task::Prior_t pri_seq[] = {5, 6, 7, 8, 9};

			Task::create(consumer, &msg_q, 4, "recv"); // Create a Consumer

			for (auto p: pri_seq) { // Create several Producers
				Task::create(producer, &msg_q, p, "send");
			}
		};

		Task::create(launch, nullptr, Macro::PRI_MAX, "msg_q/test");
	}
}

#endif
