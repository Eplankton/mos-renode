#include "board.hpp"
#include "kernel.hpp"
#include "shell.hpp"
#include "common/tests.hpp"
#if defined(MOS_SCHED_DEMO)
#include "common/sched_test.hpp"
#endif

using namespace MOS;
using namespace Kernel;
using namespace Examples;

namespace Board = Boards::STM32H747Disco;

static void led_task()
{
	while (true) {
		Board::led_toggle();
		Task::delay(500_ms);
	}
}

static void m2m_task()
{
	char msg[]           = "Hej! I'm Mx";
	msg[sizeof(msg) - 2] = Board::machine_id();

	[&] -> Async::Future_t<> {
		while (true) {
			LOG("tx -> \"%s\"", msg);
			Board::hub().send_line(msg);
			co_await Async::delay(500_ms);
		}
	}();

	while (true) {
		LOG("rx -> \"%s\"", Board::hub().buf.recv().as_str());
	}
}

int main()
{
	Board::init();

	Task::create(Shell::launch<Board::Stdio::max_size()>, &Board::stdio().buf, 0, "shell");
	Task::create(led_task, nullptr, 1, "led");
	Task::create(m2m_task, nullptr, 2, "m2m");

	// The scheduling demo replaces the two stress tests: it needs the pages
	// they would otherwise occupy and its output would drown in the async log.
#if !defined(MOS_SCHED_DEMO)
	Task::create(Test::MsgQueueTest<3>, nullptr, 2, "msgq/test");
	Task::create(Test::AsyncTest<300>, nullptr, 2, "async/test");

	// Policy test battery (self-terminating, one verdict line per test)
	Task::create(Test::SchedPolicyTest, nullptr, 2, "sched/test");
#endif

#if defined(MOS_SCHED_DEMO)
	Task::create(Test::SchedTest, nullptr, 0, "sched/demo");
#endif

	Scheduler::launch();
}
