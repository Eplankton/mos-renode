#ifndef _MOS_DRIVERS_STM32_USART_
#define _MOS_DRIVERS_STM32_USART_

#include "stm32_ll.hpp"

namespace MOS::Drivers
{
	// STM32 USART backend for `SyncUart` (LL driver). Clocks, GPIO and the
	// baud rate are set up by the board.
	struct Stm32Usart
	{
		USART_TypeDef* reg;

		bool try_read(char& data) const
		{
			if (!LL_USART_IsActiveFlag_RXNE(reg)) return false;
			data = (char) LL_USART_ReceiveData8(reg);
			return true;
		}

		void write(char data) const
		{
			while (!LL_USART_IsActiveFlag_TXE(reg));
			LL_USART_TransmitData8(reg, (uint8_t) data);
		}
	};
}

#endif
