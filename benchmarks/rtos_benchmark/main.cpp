#include "board.hpp"
#include "kernel.hpp"

#include "bench_porting_layer_mos.h"

extern "C" {
#include "bench_api.h"
}

#include "bench_porting_layer_mos.hpp"

using namespace MOS;

namespace Board = Boards::MOS_BOARD_NAMESPACE;

extern "C" void bench_all(void* arg);

// Taking the address of the board's inline _putchar hook forces it into the
// link for printf_(), which lives in a different translation unit.
extern "C" void (*const bench_putchar_link)(char) = _putchar;

int main()
{
	Board::init();
	bench_test_init(bench_all);
	return 0;
}
