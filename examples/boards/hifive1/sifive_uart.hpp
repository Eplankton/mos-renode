#ifndef _MOS_DRIVERS_SIFIVE_UART_
#define _MOS_DRIVERS_SIFIVE_UART_

#include "hifive1_revb.h"

namespace MOS::Drivers
{
	// SiFive UART backend for `SyncUart` (FE310 and friends).
	struct SiFiveUart
	{
		UART_Type* reg;

		// 8N1, interrupt when at least one byte is received.
		void init(uint32_t clock_hz, uint32_t baud) const
		{
			reg->DIV    = clock_hz / baud - 1;
			reg->TXCTRL = UART_TXCTRL_TXEN_Msk;
			reg->RXCTRL = UART_RXCTRL_RXEN_Msk; // rxcnt 0: rxwm while non-empty
			reg->IE     = UART_IE_RXWM_Msk;
		}

		// Reading RXDATA pops the FIFO, so test and read in one access.
		bool try_read(char& data) const
		{
			const uint32_t rx = reg->RXDATA;
			if (rx & UART_RXDATA_EMPTY_Msk) return false;
			data = (char) (rx & UART_RXDATA_DATA_Msk);
			return true;
		}

		void write(char data) const
		{
			while (reg->TXDATA & UART_TXDATA_FULL_Msk);
			reg->TXDATA = (uint8_t) data;
		}
	};
}

#endif
