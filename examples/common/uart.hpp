#ifndef _MOS_COMMON_UART_
#define _MOS_COMMON_UART_

#include "platform.hpp"
#include "kernel/data_type.hpp"

namespace MOS::Common
{
	// Small LL-based USART driver shared by all STM32 families.
	// Board code is responsible for clocks, GPIO and baud-rate configuration.
	template <size_t N>
	struct SyncUart
	{
		using Port_t = USART_TypeDef*;
		using Buf_t  = DataType::SyncRxBuf_t<N>;

		Port_t tx, rx;
		Buf_t buf;

		MOS_INLINE SyncUart(Port_t port): tx(port), rx(port) {}
		MOS_INLINE SyncUart(Port_t _tx, Port_t _rx): tx(_tx), rx(_rx) {}

		[[nodiscard]] static constexpr size_t max_size() { return N; }

		void read_line(auto&& oops)
		{
			if (LL_USART_IsActiveFlag_RXNE(rx)) {
				const char data = LL_USART_ReceiveData8(rx);
				if (data == '\r') return;
				Utils::IrqGuard_t guard;

				if (!buf.full()) {
					if (data == '\n') {
						buf.signal_from_isr();
					}
					else {
						buf.push(data);
					}
				}
				else {
					if (data == '\n') {
						buf.signal_from_isr();
					}
					else {
						oops();
					}
				}
			}
		}

		void send_line(const char* msg)
		{
			static auto send_byte = [&](uint8_t byte) {
				while (!LL_USART_IsActiveFlag_TXE(tx));
				LL_USART_TransmitData8(tx, byte);
			};

			if (!msg) return;
			while (*msg) {
				send_byte(*msg++);
			}
			send_byte('\n');
		}
	};
}

#endif
