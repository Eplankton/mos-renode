#ifndef STM32H7xx_HAL_CONF_H
#define STM32H7xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

// Minimal module set used by the LL-only H7 BSP. HAL source files are not
// compiled for this target; the header is still required by stm32h7xx.h.
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED

#if !defined(HSE_VALUE)
#define HSE_VALUE 25000000UL
#endif

#if !defined(HSI_VALUE)
#define HSI_VALUE 64000000UL
#endif

#if !defined(CSI_VALUE)
#define CSI_VALUE 4000000UL
#endif

#if !defined(LSI_VALUE)
#define LSI_VALUE 32000UL
#endif

#if !defined(LSE_VALUE)
#define LSE_VALUE 32768UL
#endif

#define TICK_INT_PRIORITY 0x0FUL
#define USE_RTOS          0

#define PREFETCH_ENABLE          1
#define INSTRUCTION_CACHE_ENABLE 1
#define DATA_CACHE_ENABLE        1

#include "stm32h7xx_hal_def.h"

#ifdef __cplusplus
}
#endif

#endif
