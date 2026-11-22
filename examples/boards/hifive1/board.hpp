#ifndef _MOS_BOARD_HIFIVE1_
#define _MOS_BOARD_HIFIVE1_

// Register definitions generated from SiFive's design.svd, see vendor/sifive/
#include "hifive1_revb.h"

#include "uart.hpp"
#include "sifive_uart.hpp"

// SiFive HiFive1 Rev B (FE310): RV32IMAC, 16 KB RAM.
//   UART0 (GPIO16/17) -> shell, UART1 (GPIO18/23) -> machine hub,
//   green LED on GPIO19 (active low), GPIO9 selects the machine id.
namespace MOS::Boards::HiFive1
{
	using Stdio = Drivers::SyncUart<Drivers::SiFiveUart, MOS_CONF_SHELL_BUF_SIZE>;
	using Hub   = Drivers::SyncUart<Drivers::SiFiveUart, 32>;

	namespace Detail
	{
		constexpr uint32_t CORE_HZ = 16'000'000; // HFXOSC, PLL bypassed
		constexpr uint32_t BAUD    = 115200;

		// PLIC sources, from the freedom-e-sdk BSP device tree (core.dts);
		// the SVD does not carry interrupts.
		constexpr uint32_t IRQ_UART0 = 3;
		constexpr uint32_t IRQ_UART1 = 4;

		constexpr uint32_t PIN_LED = 19;
		constexpr uint32_t PIN_ID  = 9;

		// hfclk = HFXOSC (16 MHz): enable the crystal, wait until it is
		// ready, then select it as PLL reference with the PLL bypassed.
		inline void clock_init()
		{
			PRCI->HFXOSCCFG = PRCI_HFXOSCCFG_HFXOSCEN_Msk;
			while ((PRCI->HFXOSCCFG & PRCI_HFXOSCCFG_HFXOSCRDY_Msk) == 0);
			PRCI->PLLCFG = PRCI_PLLCFG_PLLSEL_Msk | PRCI_PLLCFG_PLLREFSEL_Msk | PRCI_PLLCFG_PLLBYPASS_Msk;
		}

		inline char machine_id = '2';

		inline void gpio_init()
		{
			// Sample the id strap first and keep it: it is a power-on setting,
			// and touching the other GPIO registers cannot disturb it then.
			GPIO->INPUT_EN |= 1U << PIN_ID;
			machine_id = (GPIO->INPUT_VAL & (1U << PIN_ID)) ? '1' : '2';

			// UART0: 16 RX / 17 TX, UART1: 23 RX / 18 TX, all on IOF0
			constexpr uint32_t uart_pins = (1U << 16) | (1U << 17) | (1U << 18) | (1U << 23);
			GPIO->IOF_SEL &= ~uart_pins;
			GPIO->IOF_EN |= uart_pins;

			GPIO->OUTPUT_VAL |= 1U << PIN_LED; // LED off (active low)
			GPIO->OUTPUT_EN |= 1U << PIN_LED;
		}

		inline void plic_enable(uint32_t source)
		{
			PLIC->PRIORITY[source] = 1;
			PLIC->ENABLE_0[source / 32] |= 1U << (source % 32); // Context 0: hart 0, M-mode
		}

		inline void irq_init()
		{
			PLIC->THRESHOLD_0 = 0; // Accept every priority
			plic_enable(IRQ_UART0);
			plic_enable(IRQ_UART1);
		}

		inline Stdio stdio_instance {Drivers::SiFiveUart {UART0}};
		inline Hub hub_instance {Drivers::SiFiveUart {UART1}};
	}

	inline Stdio& stdio() { return Detail::stdio_instance; }
	inline Hub& hub() { return Detail::hub_instance; }

	// The system tick (CLINT) is started by the scheduler.
	inline void init()
	{
		Detail::clock_init();
		Detail::gpio_init();
		stdio().port.init(Detail::CORE_HZ, Detail::BAUD);
		hub().port.init(Detail::CORE_HZ, Detail::BAUD);
		Detail::irq_init();
	}

	inline void led_toggle() { GPIO->OUTPUT_VAL ^= 1U << Detail::PIN_LED; }

	inline char machine_id() { return Detail::machine_id; }
}

extern "C" MOS_USED inline void MOS_PUTCHAR(char ch)
{
	MOS::Boards::HiFive1::stdio().port.write(ch);
}

extern "C" MOS_USED inline void mos_external_irq(uint32_t source)
{
	using namespace MOS::Boards::HiFive1;

	if (source == Detail::IRQ_UART0) {
		stdio().read_line([] { LOG("Oops! Command too long!"); });
	}
	else if (source == Detail::IRQ_UART1) {
		hub().read_line([] { LOG("Oops! Buffer Overflow!"); });
	}
}

#endif
