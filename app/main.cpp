#include "main.h"

#include "../core/kernel.hpp"
#include "../core/shell.hpp"
#include "test.hpp"

#define LED_1_GPIO_Port GPIOA
#define LED_1_Pin       LL_GPIO_PIN_1
#define LED_Toggle()    LL_GPIO_TogglePin(LED_1_GPIO_Port, LED_1_Pin)

namespace MOS::User::Global
{
	using namespace DataType;

	template <size_t N>
	struct SyncUartDev_t
	{
		using Port_t = USART_TypeDef*;
		using Buf_t  = SyncRxBuf_t<N>;

		Port_t port;
		Buf_t buf;

		inline static constexpr auto max_size() { return N; }

		void read_line(auto&& oops)
		{
			// read data register not exmpty
			if (LL_USART_IsActiveFlag_RXNE(port)) {
				Utils::IrqGuard_t guard;
				char8_t data = LL_USART_ReceiveData8(port);
				if (!buf.full()) {
					if (data == '\n') // read a line
						buf.signal_from_isr();
					else
						buf.push(data);
				}
				else {
					buf.clear();
					oops();
				}
			}
		}
	};

	// Serial I/O UART with buffer
	auto stdio = SyncUartDev_t<SHELL_BUF_SIZE> {USART2};
}

namespace MOS::BSP
{
	void SystemClock_Config(void)
	{
		RCC_OscInitTypeDef RCC_OscInitStruct = {0};
		RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

		/** Configure the main internal regulator output voltage
	     */
		__HAL_RCC_PWR_CLK_ENABLE();
		__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
		/** Initializes the RCC Oscillators according to the specified parameters
	     * in the RCC_OscInitTypeDef structure.
	     */
		RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
		RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
		RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
		RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
		RCC_OscInitStruct.PLL.PLLM       = 4;
		RCC_OscInitStruct.PLL.PLLN       = 168;
		RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
		RCC_OscInitStruct.PLL.PLLQ       = 4;
		HAL_RCC_OscConfig(&RCC_OscInitStruct);
		/** Initializes the CPU, AHB and APB buses clocks
	     */
		RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
		RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
		RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
		RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
		RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

		HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
		/** Enables the Clock Security System
	     */
		HAL_RCC_EnableCSS();
	}

	void UART_Config(void)
	{
		/* Peripheral clock enable */
		LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);
		LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);

		LL_USART_InitTypeDef USART_InitStruct = {0};
		LL_GPIO_InitTypeDef GPIO_InitStruct   = {0};

		/** USART2 GPIO Configuration
             PD5   ------> USART2_TX
             PD6   ------> USART2_RX
        */
		GPIO_InitStruct.Pin        = LL_GPIO_PIN_5 | LL_GPIO_PIN_6;
		GPIO_InitStruct.Mode       = LL_GPIO_MODE_ALTERNATE;
		GPIO_InitStruct.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH;
		GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
		GPIO_InitStruct.Pull       = LL_GPIO_PULL_NO;
		GPIO_InitStruct.Alternate  = LL_GPIO_AF_7;
		LL_GPIO_Init(GPIOD, &GPIO_InitStruct);

		NVIC_SetPriority(USART2_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
		NVIC_EnableIRQ(USART2_IRQn);

		USART_InitStruct.BaudRate            = 115200;
		USART_InitStruct.DataWidth           = LL_USART_DATAWIDTH_8B;
		USART_InitStruct.StopBits            = LL_USART_STOPBITS_1;
		USART_InitStruct.Parity              = LL_USART_PARITY_NONE;
		USART_InitStruct.TransferDirection   = LL_USART_DIRECTION_TX_RX;
		USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
		USART_InitStruct.OverSampling        = LL_USART_OVERSAMPLING_16;

		LL_USART_Init(USART2, &USART_InitStruct);
		LL_USART_ConfigAsyncMode(USART2);
		LL_USART_Enable(USART2);

		// enable interrupts
		LL_USART_EnableIT_RXNE(USART2);
		// LL_USART_EnableIT_IDLE(USART2);
		// LL_USART_EnableIT_ERROR(USART2);
	}

	/**
     * @brief LED Initialization Function
     * @param None
     * @retval None
    */
	void LED_Config(void)
	{
		LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

		/* GPIO Ports Clock Enable */
		LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);

		/**/
		LL_GPIO_SetOutputPin(LED_1_GPIO_Port, LED_1_Pin);

		/**/
		GPIO_InitStruct.Pin        = LED_1_Pin;
		GPIO_InitStruct.Mode       = LL_GPIO_MODE_OUTPUT;
		GPIO_InitStruct.Speed      = LL_GPIO_SPEED_FREQ_LOW;
		GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
		GPIO_InitStruct.Pull       = LL_GPIO_PULL_UP;

		LL_GPIO_Init(LED_1_GPIO_Port, &GPIO_InitStruct);
	}

	extern "C" void MOS_PUTCHAR(char ch)
	{
		auto uart = User::Global::stdio.port;

		/* Wait until TXE flag is set to send data */
		while (!LL_USART_IsActiveFlag_TXE(uart));

		/* Send byte through the USART2 peripheral */
		LL_USART_TransmitData8(uart, ch);
	}

	extern "C" void USART2_IRQHandler()
	{
		User::Global::stdio.read_line(
		    [] { LOG("Oops! Command too long!"); }
		);
	}

	void config()
	{
		/* Reset of all peripherals */
		HAL_Init();

		/* Set SysTick = 1ms */
		SysTick_Config(SystemCoreClock / MOS_CONF_SYSTICK);

		/* Configure peripherals */
		LED_Config();
		UART_Config();
	}
}

namespace MOS::User::App
{
	using namespace Kernel;

	auto led_test = [] {
		while (true) {
			LED_Toggle();
			Task::delay(500_ms);
		}
	};
}

int main()
{
	using namespace MOS;
	using namespace Kernel;
	using namespace User;
	using namespace User::Global;

	BSP::config();

	/* User Tasks */
	Task::create(Shell::launch<stdio.max_size()>, &stdio.buf, 0, "shell");
	Task::create(App::led_test, nullptr, 1, "led/test");

	/* Test examples */
	Task::create(Test::MsgQueueTest<3>, nullptr, 2, "msgq/test");
	Task::create(Test::AsyncTest, 500, 2, "async/test");
	// Task::create(Test::MutexTest, 3, 2, "mutex/test");

	// Start scheduling, never return
	Scheduler::launch();
}