# STM32F4 Discovery (STM32F407VG, Cortex-M4F). See examples/README.md.

set(MOS_TOOLCHAIN     arm-none-eabi)
set(MOS_MCU           "STM32F407xx")
set(MOS_DEVICE_HEADER "stm32f4xx.h")
set(MOS_ARCH_DEF      MOS_ARCH_CORTEX_M4)
set(MOS_FAMILY_DEF    MOS_FAMILY_STM32F4)
set(MOS_BOARD_NS      "STM32F4Disco")
set(MOS_BOARD_BENCHMARKS ON)

set(MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

set(_dev "${STM32_DIR}/cmsis-device-f4")
set(_hal "${STM32_DIR}/stm32f4xx-hal-driver")

set(BOARD_INCLUDES
    "${STM32_DIR}/cmsis-core/CMSIS/Core/Include"
    "${_dev}/Include"
    "${_hal}/Inc"
)
set(BOARD_SOURCES
    "${_dev}/Source/Templates/gcc/startup_stm32f407xx.s"
    "${_dev}/Source/Templates/system_stm32f4xx.c"
    "${_hal}/Src/stm32f4xx_hal.c"
    "${_hal}/Src/stm32f4xx_hal_cortex.c"
    "${_hal}/Src/stm32f4xx_hal_rcc.c"
    "${_hal}/Src/stm32f4xx_hal_rcc_ex.c"
    "${_hal}/Src/stm32f4xx_hal_pwr.c"
    "${_hal}/Src/stm32f4xx_hal_pwr_ex.c"
    "${_hal}/Src/stm32f4xx_hal_flash.c"
    "${_hal}/Src/stm32f4xx_hal_flash_ex.c"
    "${_hal}/Src/stm32f4xx_ll_gpio.c"
)
set(LINKER_SCRIPT "${BOARD_DIR}/STM32F407VGTx_FLASH.ld")

# FreeRTOS reference builds of the benchmarks (benchmarks/freertos)
set(MOS_BOARD_FREERTOS      ON)
set(MOS_BOARD_FREERTOS_PORT ARM_CM4F)
