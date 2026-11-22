#ifndef _MOS_DRIVERS_UART_
#define _MOS_DRIVERS_UART_

#include "kernel/ipc.hpp"

namespace MOS::Drivers
{
	// Line-oriented UART shared by all boards. The RX interrupt collects a
	// line into `buf` and signals it on '\n'; `send_line()` writes polled.
	//
	// `Port` is the hardware backend and only needs:
	//   bool try_read(char&)  read one byte if available
	//   void write(char)      blocking write of one byte
	template <typename Port, size_t N>
	struct SyncUart
	{
		using Buf_t = Kernel::IPC::SyncRxBuf_t<N>;

		Port port;
		Buf_t buf;

		MOS_INLINE SyncUart(Port _port): port(_port) {}

		[[nodiscard]] static constexpr size_t max_size() { return N; }

		// Call from the RX interrupt
		void read_line(auto&& oops)
		{
			char data;
			while (port.try_read(data)) {
				if (data == '\r') continue;
				Utils::IrqGuard_t guard;

				if (data == '\n') {
					buf.signal_from_isr();
				}
				else if (buf.len < (int32_t) N - 1) { // Keep room for '\0'
					buf.push(data);
				}
				else {
					oops();
				}
			}
		}

		void send_line(const char* msg)
		{
			if (!msg) return;
			while (*msg) {
				port.write(*msg++);
			}
			port.write('\n');
		}
	};
}

#endif
