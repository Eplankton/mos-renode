// SPDX-License-Identifier: Apache-2.0
//
// Timing half of the RTOS-Benchmark porting layer for Cortex-M (STM32),
// shared by the MOS and the FreeRTOS ports so that both kernels are measured
// with the same code:
//  - bench_timing_*: the timing counter (DWT CYCCNT, or TIM2 where DWT is not
//    modeled) used for every start/stop measurement and the ns conversion;
//  - bench_timer_*: SysTick, which the interrupt-latency test reprograms and
//    chains through a RAM copy of the vector table.
//
// The including file defines BENCH_TICK_HZ (the OS tick rate) and
// MOS_RB_TIMER_HZ (0 = the timing counter runs at SystemCoreClock).

#ifndef BENCH_TIMER_CORTEX_M_HPP
#define BENCH_TIMER_CORTEX_M_HPP

#include "stm32_ll.hpp" // From the board directory

#include <cstdint>
#include <cstring>

#ifndef BENCH_TICK_HZ
#error "BENCH_TICK_HZ must be the OS tick rate"
#endif

namespace BenchTimer
{
	constexpr uint32_t NSEC_PER_SEC = 1'000'000'000;

	// On silicon both clocks run at SystemCoreClock. Under Renode the timing
	// counter is modeled at a fixed rate, passed in as MOS_RB_TIMER_HZ, while
	// SysTick follows `sysbus.nvic Frequency`, which the scripts set to
	// SystemCoreClock.
	inline uint32_t timing_hz()
	{
		return MOS_RB_TIMER_HZ != 0
		           ? static_cast<uint32_t>(MOS_RB_TIMER_HZ)
		           : SystemCoreClock;
	}

	inline uint32_t systick_hz() { return SystemCoreClock; }

	// Vector table used by the interrupt-latency test. 256 entries cover
	// both STM32F4 and STM32H7; the table must be aligned to its size.
	constexpr uint32_t VECTOR_COUNT = 256;
	alignas(1024) inline bench_isr_handler_t vector_table[VECTOR_COUNT];
	inline uint32_t original_vtor = 0;
	inline bool vector_active     = false;

	// Prefer the DWT cycle counter on real hardware. Renode does not model
	// CYCCNT on every core, so fall back to a free-running TIM2.
	inline bool use_dwt = false;

	inline void init_dwt()
	{
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
		DWT->CYCCNT = 0;
		DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

		for (int i = 0; i < 16; ++i) {
			__NOP();
		}
		const auto first = DWT->CYCCNT;
		for (int i = 0; i < 16; ++i) {
			__NOP();
		}
		const auto second = DWT->CYCCNT;

		use_dwt = (second != first);
	}

	inline void init_tim2()
	{
		// PSC=0 and the board's default APB1 prescaler make the timer clock
		// equal SystemCoreClock.
		LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);

		TIM2->CR1 = 0;
		TIM2->PSC = 0;
		TIM2->ARR = 0xFFFF'FFFFU;
		TIM2->CNT = 0;
		TIM2->EGR = TIM_EGR_UG;
		TIM2->SR  = 0;
		TIM2->CR1 = TIM_CR1_CEN;
	}

	inline uint32_t active_vtor()
	{
		const auto vtor = SCB->VTOR;
		return vtor != 0 ? vtor : 0x0800'0000U;
	}
}

extern "C" {

void bench_timing_init(void)
{
	BenchTimer::init_dwt();

	if (BenchTimer::use_dwt) {
		TIM2->CR1 &= ~TIM_CR1_CEN;
	}
	else {
		BenchTimer::init_tim2();
	}
}

void bench_timing_start(void)
{
	if (BenchTimer::use_dwt) {
		DWT->CYCCNT = 0;
		DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	}
	else {
		TIM2->CNT = 0;
		TIM2->CR1 |= TIM_CR1_CEN;
	}
}

void bench_timing_stop(void)
{
	DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
	TIM2->CR1 &= ~TIM_CR1_CEN;
}

bench_time_t bench_timing_counter_get(void)
{
	if (BenchTimer::use_dwt) {
		return static_cast<bench_time_t>(DWT->CYCCNT);
	}

	return static_cast<bench_time_t>(TIM2->CNT);
}

bench_time_t bench_timing_cycles_get(
    bench_time_t* time_start, bench_time_t* time_end
)
{
	return static_cast<bench_time_t>(
	    static_cast<uint32_t>(*time_end) - static_cast<uint32_t>(*time_start)
	);
}

bench_time_t bench_timing_cycles_to_ns(bench_time_t cycles)
{
	const auto hz = BenchTimer::timing_hz();

	return cycles * BenchTimer::NSEC_PER_SEC / (hz != 0 ? hz : 1);
}

bench_isr_handler_t bench_timer_isr_get(void)
{
	auto table = reinterpret_cast<bench_isr_handler_t*>(BenchTimer::active_vtor());
	return table[15]; // SysTick exception vector
}

// The "timer" of the ISR tests is SysTick.
uint32_t bench_timer_cycles_per_second(void) { return BenchTimer::systick_hz(); }

uint32_t bench_timer_cycles_per_tick(void)
{
	return bench_timer_cycles_per_second() / BENCH_TICK_HZ;
}

void bench_timer_isr_set(bench_isr_handler_t isr)
{
	using namespace BenchTimer;

	const auto primask = __get_PRIMASK();
	__disable_irq();

	auto source = reinterpret_cast<bench_isr_handler_t*>(active_vtor());

	if (!vector_active) {
		original_vtor = SCB->VTOR;
		vector_active = true;
	}

	if (source != vector_table) {
		std::memcpy(vector_table, source, sizeof(vector_table));
	}
	vector_table[15] = isr;

	SCB->VTOR = reinterpret_cast<uint32_t>(vector_table);
	__DSB();
	__ISB();

	if (primask == 0) __enable_irq();
}

bench_time_t bench_timer_cycles_diff(
    bench_time_t trigger_point, bench_time_t sample_point
)
{
	// SysTick counts down. The result is reported through
	// bench_timing_cycles_to_ns(), so convert SysTick cycles into
	// timing-counter cycles (rounded to nearest; identity on silicon).
	const uint64_t cycles = static_cast<uint32_t>(trigger_point) -
	                        static_cast<uint32_t>(sample_point) + 1;
	const uint64_t sys    = BenchTimer::systick_hz();

	return (cycles * BenchTimer::timing_hz() + sys / 2) / sys;
}

bench_time_t bench_timer_cycles_get(void)
{
	return static_cast<bench_time_t>(SysTick->VAL);
}

bench_time_t bench_timer_isr_expiry_set(uint32_t usec)
{
	const auto cycles_per_usec =
	    (bench_timer_cycles_per_second() + 999'999U) / 1'000'000U;
	const auto cycles = cycles_per_usec * usec;

	SysTick->LOAD = cycles;
	SysTick->VAL  = cycles - 1;
	SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk |
	                 SysTick_CTRL_TICKINT_Msk |
	                 SysTick_CTRL_CLKSOURCE_Msk;

	return static_cast<bench_time_t>(cycles);
}

void bench_timer_isr_restore(bench_isr_handler_t handler)
{
	using namespace BenchTimer;
	(void) handler;

	const auto primask = __get_PRIMASK();
	__disable_irq();

	const auto cycles = bench_timer_cycles_per_tick() - 1;
	SysTick->LOAD     = cycles;
	SysTick->VAL      = 0;
	SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk |
	                 SysTick_CTRL_TICKINT_Msk |
	                 SysTick_CTRL_CLKSOURCE_Msk;

	if (vector_active) {
		SCB->VTOR     = original_vtor;
		vector_active = false;
	}

	__DSB();
	__ISB();

	if (primask == 0) __enable_irq();
}
}

#endif
