#include "board.hpp"
#include "kernel.hpp"
#include "shell.hpp"
#include "common/kernel_tests.hpp"
#if defined(MOS_SCHED_DEMO)
#include "common/sched_demo.hpp"
#endif

using namespace MOS;
using namespace Kernel;

namespace Board = Boards::STM32H747Disco;

static void led_task()
{
	while (true) {
		Board::led_toggle();
		Task::delay(500_ms);
	}
}

// A named coroutine copies its parameters into the coroutine frame. A
// capturing lambda coroutine would instead keep a reference to the closure,
// which is a temporary destroyed right after the call.
static Async::Future_t<> m2m_tx(const char* msg)
{
	while (true) {
		LOG("tx -> \"%s\"", msg);
		Board::hub().send_line(msg);
		co_await Async::delay(500_ms);
	}
}

static void m2m_task()
{
	char msg[]           = "Hej! I'm Mx";
	msg[sizeof(msg) - 2] = Board::machine_id();

	m2m_tx(msg); // Starts now; `msg` lives as long as this task

	while (true) {
		LOG("rx -> \"%s\"", Board::hub().buf.recv().as_str());
	}
}

int main()
{
	Board::init();

	Task::create(Shell::launch<Board::Stdio::max_size()>, &Board::stdio().buf, 0, "shell");
	Task::create(led_task, 1, "led");
	Task::create(m2m_task, 2, "m2m");

	// The scheduling demo replaces the two stress tests: it needs the pages
	// they would otherwise occupy and its output would drown in the async log.
#if !defined(MOS_SCHED_DEMO)
	Task::create(Test::MsgQueueTest<3>, 2, "msgq/test");
	Task::create(Test::AsyncTest<300>, 2, "async/test");

	// Policy test battery (self-terminating, one verdict line per test)
	Task::create(Test::SchedPolicyTest, 2, "sched/test");
#endif

#if defined(MOS_SCHED_DEMO)
	Task::create(Test::SchedDemo, 0, "sched/demo");
#endif

	Scheduler::launch();
}
