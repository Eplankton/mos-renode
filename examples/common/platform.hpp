#ifndef _MOS_COMMON_PLATFORM_
#define _MOS_COMMON_PLATFORM_

// MOS_DEVICE_HEADER is supplied by CMake, e.g. "stm32f4xx.h" or "stm32h747xx.h".
#include MOS_DEVICE_HEADER

#if defined(MOS_FAMILY_STM32F4)
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usart.h"
#elif defined(MOS_FAMILY_STM32H7)
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_usart.h"
#else
#error "Unsupported STM32 family: define MOS_FAMILY_STM32F4 or MOS_FAMILY_STM32H7"
#endif

#endif
