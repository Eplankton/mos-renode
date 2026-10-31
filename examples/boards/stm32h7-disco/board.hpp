#ifndef _MOS_BOARD_STM32H747_DISCO_
#define _MOS_BOARD_STM32H747_DISCO_

#include "../common/uart.hpp"

namespace MOS::Boards::STM32H747Disco
{
	using Stdio = Common::SyncUart<MOS_CONF_SHELL_BUF_SIZE>;
	using Hub   = Common::SyncUart<32>;

	namespace Detail
	{
		constexpr uint32_t SHELL_BAUD = 115200;
		constexpr uint32_t HUB_BAUD   = 115200;

		// Renode's STM32H747 model fixes USART1/USART6 to 200 MHz. On real
		// hardware override this with the actual APB2 kernel clock.
#ifndef MOS_H7_UART_CLOCK
#define MOS_H7_UART_CLOCK 200000000U
#endif

		inline void usart_init(USART_TypeDef* usart, uint32_t periph_clk, uint32_t baud)
		{
			LL_USART_Disable(usart);
			LL_USART_SetDataWidth(usart, LL_USART_DATAWIDTH_8B);
			LL_USART_SetStopBitsLength(usart, LL_USART_STOPBITS_1);
			LL_USART_SetParity(usart, LL_USART_PARITY_NONE);
			LL_USART_SetTransferDirection(usart, LL_USART_DIRECTION_TX_RX);
			LL_USART_SetHWFlowCtrl(usart, LL_USART_HWCONTROL_NONE);
			LL_USART_SetOverSampling(usart, LL_USART_OVERSAMPLING_16);
			LL_USART_SetBaudRate(
			    usart, periph_clk,
			    LL_USART_PRESCALER_DIV1,
			    LL_USART_OVERSAMPLING_16,
			    baud
			);
			LL_USART_ConfigAsyncMode(usart);
			LL_USART_Enable(usart);
			LL_USART_EnableIT_RXNE(usart);
		}

		inline void gpio_af_init(GPIO_TypeDef* port, uint32_t pins, uint32_t alternate)
		{
			LL_GPIO_InitTypeDef gpio = {0};
			gpio.Pin        = pins;
			gpio.Mode       = LL_GPIO_MODE_ALTERNATE;
			gpio.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH;
			gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
			gpio.Pull       = LL_GPIO_PULL_NO;
			gpio.Alternate  = alternate;
			LL_GPIO_Init(port, &gpio);
		}

		inline void led_init()
		{
			LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOA);

			LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_1);

			LL_GPIO_InitTypeDef gpio = {0};
			gpio.Pin        = LL_GPIO_PIN_1;
			gpio.Mode       = LL_GPIO_MODE_OUTPUT;
			gpio.Speed      = LL_GPIO_SPEED_FREQ_LOW;
			gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
			gpio.Pull       = LL_GPIO_PULL_UP;
			LL_GPIO_Init(GPIOA, &gpio);
		}

		inline void id_init()
		{
			LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOA);

			LL_GPIO_InitTypeDef gpio = {0};
			gpio.Pin  = LL_GPIO_PIN_0;
			gpio.Mode = LL_GPIO_MODE_INPUT;
			gpio.Pull = LL_GPIO_PULL_DOWN;
			LL_GPIO_Init(GPIOA, &gpio);
		}

		inline void uart_init()
		{
			LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOA | LL_AHB4_GRP1_PERIPH_GPIOC);
			LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1 | LL_APB2_GRP1_PERIPH_USART6);

			// USART1: PA9(TX) / PA10(RX)
			gpio_af_init(GPIOA, LL_GPIO_PIN_9 | LL_GPIO_PIN_10, LL_GPIO_AF_7);
			NVIC_SetPriority(USART1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
			NVIC_EnableIRQ(USART1_IRQn);

			// USART6: PC6(TX) / PC7(RX)
			gpio_af_init(GPIOC, LL_GPIO_PIN_6 | LL_GPIO_PIN_7, LL_GPIO_AF_7);
			NVIC_SetPriority(USART6_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 1, 0));
			NVIC_EnableIRQ(USART6_IRQn);

			const auto pclk2 = MOS_H7_UART_CLOCK;
			usart_init(USART1, pclk2, SHELL_BAUD);
			usart_init(USART6, pclk2, HUB_BAUD);
		}

		inline Stdio stdio_instance {USART1};
		inline Hub hub_instance {USART6};
	}

	inline void init()
	{
		NVIC_SetPriorityGrouping(0x03U); // 4 bits pre-emption, 0 bits sub-priority
		SysTick_Config(SystemCoreClock / MOS_CONF_SYSTICK);

		Detail::led_init();
		Detail::id_init();
		Detail::uart_init();
	}

	inline Stdio& stdio() { return Detail::stdio_instance; }
	inline Hub& hub() { return Detail::hub_instance; }

	inline void led_toggle()
	{
		LL_GPIO_TogglePin(GPIOA, LL_GPIO_PIN_1);
	}

	inline char machine_id()
	{
		return LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_0) ? '1' : '2';
	}
}

extern "C" MOS_USED inline void MOS_PUTCHAR(char ch)
{
	auto tx = MOS::Boards::STM32H747Disco::stdio().tx;
	while (!LL_USART_IsActiveFlag_TXE(tx));
	LL_USART_TransmitData8(tx, ch);
}

extern "C" MOS_USED inline void USART1_IRQHandler()
{
	MOS::Boards::STM32H747Disco::stdio().read_line(
	    [] { LOG("Oops! Command too long!"); }
	);
}

extern "C" MOS_USED inline void USART6_IRQHandler()
{
	MOS::Boards::STM32H747Disco::hub().read_line(
	    [] { LOG("Oops! Buffer Overflow!"); }
	);
}

#endif
