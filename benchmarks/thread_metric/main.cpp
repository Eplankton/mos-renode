#include "board.hpp"
#include "kernel.hpp"
#include "tm_api.h"

// The port has to live in the same translation unit as main(): the MOS
// scheduler header defines the exception entry points, so including it from a
// second .cpp would emit duplicate SVC/PendSV/SysTick handlers.
#include "tm_port.hpp"

using namespace MOS;

namespace Board = Boards::MOS_BOARD_NAMESPACE;

extern "C" void tm_main(void);

extern "C" void tm_putchar(int c)
{
	_putchar(static_cast<char>(c));
}

extern "C" void tm_semihosting_exit(int code)
{
	tm_printf("\n*** THREAD-METRIC DONE (code=%d) ***\n", code);

	// Renode/serial output is polled, so everything above is already on the
	// wire. Park the CPU instead of pulling in newlib's exit path.
	for (;;) {
		__WFI();
	}
}

int main()
{
	Board::init();

	tm_report_init();
	tm_printf("Thread-Metric: reporting interval = %d s\n", tm_test_duration);

	tm_main();
	return 0;
}
