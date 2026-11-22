#ifndef _MOS_BOARD_RISCV32_VIRT_
#define _MOS_BOARD_RISCV32_VIRT_

#include <cstdint>

#include "uart.hpp"
#include "ns16550_uart.hpp"

// Renode RISC-V32 Virtual (riscv_virt, RV32IMAC) plus renode/riscv32-virt.repl:
//   UART0 -> shell, UART1 -> machine hub,
//   LED on GPIO0, GPIO1 selects the machine id.
namespace MOS::Boards::RiscV32Virt
{
	using Stdio = Drivers::SyncUart<Drivers::Ns16550Uart, MOS_CONF_SHELL_BUF_SIZE>;
	using Hub   = Drivers::SyncUart<Drivers::Ns16550Uart, 32>;

	namespace Detail
	{
		constexpr uintptr_t UART0_BASE = 0x1000'0000;
		constexpr uintptr_t UART1_BASE = 0x1000'1000;
		constexpr uintptr_t GPIO_BASE  = 0x1001'2000; // SiFive GPIO model
		constexpr uintptr_t PLIC_BASE  = 0x0C00'0000;

		// The divisor is ignored by Renode; 1.8432 MHz is the classic
		// 16550 reference clock.
		constexpr uint32_t UART_CLOCK_HZ = 1'843'200;
		constexpr uint32_t BAUD          = 115200;

		constexpr uint32_t IRQ_UART0 = 3; // PLIC sources
		constexpr uint32_t IRQ_UART1 = 4;

		constexpr uint32_t PIN_LED = 0;
		constexpr uint32_t PIN_ID  = 1;

		inline volatile uint32_t& reg(uintptr_t addr)
		{
			return *reinterpret_cast<volatile uint32_t*>(addr);
		}

		// SiFive GPIO registers
		inline volatile uint32_t& gpio_input_val  = reg(GPIO_BASE + 0x00);
		inline volatile uint32_t& gpio_input_en   = reg(GPIO_BASE + 0x04);
		inline volatile uint32_t& gpio_output_en  = reg(GPIO_BASE + 0x08);
		inline volatile uint32_t& gpio_output_val = reg(GPIO_BASE + 0x0C);

		inline char machine_id = '2';

		inline void gpio_init()
		{
			// Sample the id strap first and keep it: it is a power-on setting.
			gpio_input_en |= 1U << PIN_ID;
			machine_id = (gpio_input_val & (1U << PIN_ID)) ? '1' : '2';

			gpio_output_val &= ~(1U << PIN_LED); // LED off
			gpio_output_en |= 1U << PIN_LED;
		}

		inline void plic_enable(uint32_t source)
		{
			reg(PLIC_BASE + 4 * source) = 1;                                   // Priority
			reg(PLIC_BASE + 0x2000 + 4 * (source / 32)) |= 1U << (source % 32); // Context 0 enable
		}

		inline void irq_init()
		{
			reg(PLIC_BASE + 0x20'0000) = 0; // Context 0 threshold: accept every priority
			plic_enable(IRQ_UART0);
			plic_enable(IRQ_UART1);
		}

		inline Stdio stdio_instance {Drivers::Ns16550Uart {UART0_BASE}};
		inline Hub hub_instance {Drivers::Ns16550Uart {UART1_BASE}};
	}

	inline Stdio& stdio() { return Detail::stdio_instance; }
	inline Hub& hub() { return Detail::hub_instance; }

	// The system tick (CLINT) is started by the scheduler.
	inline void init()
	{
		Detail::gpio_init();
		stdio().port.init(Detail::UART_CLOCK_HZ, Detail::BAUD);
		hub().port.init(Detail::UART_CLOCK_HZ, Detail::BAUD);
		Detail::irq_init();
	}

	inline void led_toggle() { Detail::gpio_output_val ^= 1U << Detail::PIN_LED; }

	inline char machine_id() { return Detail::machine_id; }
}

extern "C" MOS_USED inline void MOS_PUTCHAR(char ch)
{
	MOS::Boards::RiscV32Virt::stdio().port.write(ch);
}

extern "C" MOS_USED inline void mos_external_irq(uint32_t source)
{
	using namespace MOS::Boards::RiscV32Virt;

	if (source == Detail::IRQ_UART0) {
		stdio().read_line([] { LOG("Oops! Command too long!"); });
	}
	else if (source == Detail::IRQ_UART1) {
		hub().read_line([] { LOG("Oops! Buffer Overflow!"); });
	}
}

#endif
