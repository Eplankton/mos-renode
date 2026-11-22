#ifndef _MOS_DRIVERS_NS16550_UART_
#define _MOS_DRIVERS_NS16550_UART_

#include <cstdint>

namespace MOS::Drivers
{
	// NS16550-compatible UART backend for `SyncUart`, byte-spaced registers.
	struct Ns16550Uart
	{
		uintptr_t base;

		enum Reg : uint32_t
		{
			RBR_THR = 0, // Receive buffer / transmit holding (DLL with DLAB)
			IER     = 1, // Interrupt enable (DLM with DLAB)
			FCR     = 2, // FIFO control
			LCR     = 3, // Line control
			LSR     = 5, // Line status
		};

		static constexpr uint8_t IER_RDA  = 0x01; // Received data available
		static constexpr uint8_t FCR_FIFO = 0x07; // Enable and clear both FIFOs
		static constexpr uint8_t LCR_8N1  = 0x03;
		static constexpr uint8_t LCR_DLAB = 0x80;
		static constexpr uint8_t LSR_DR   = 0x01; // Data ready
		static constexpr uint8_t LSR_THRE = 0x20; // Transmit holding empty

		volatile uint8_t& reg(Reg r) const
		{
			return *reinterpret_cast<volatile uint8_t*>(base + r);
		}

		// 8N1, interrupt when a byte is received.
		void init(uint32_t clock_hz, uint32_t baud) const
		{
			const uint32_t div = clock_hz / (16 * baud);
			reg(LCR)     = LCR_DLAB;
			reg(RBR_THR) = (uint8_t) div;
			reg(IER)     = (uint8_t) (div >> 8);
			reg(LCR)     = LCR_8N1;
			reg(FCR)     = FCR_FIFO;
			reg(IER)     = IER_RDA;
		}

		bool try_read(char& data) const
		{
			if ((reg(LSR) & LSR_DR) == 0) return false;
			data = (char) reg(RBR_THR);
			return true;
		}

		void write(char data) const
		{
			while ((reg(LSR) & LSR_THRE) == 0);
			reg(RBR_THR) = (uint8_t) data;
		}
	};
}

#endif
