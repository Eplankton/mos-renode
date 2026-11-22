# STM32H747I-DISCO (Cortex-M7 core of the dual-core H747). See examples/README.md.

set(MOS_TOOLCHAIN     arm-none-eabi)
set(MOS_MCU           "STM32H747xx")
set(MOS_DEVICE_HEADER "stm32h747xx.h")
set(MOS_ARCH_DEF      MOS_ARCH_CORTEX_M7)
set(MOS_FAMILY_DEF    MOS_FAMILY_STM32H7)
set(MOS_BOARD_NS      "STM32H747Disco")
set(MOS_EXTRA_DEFS    CORE_CM7 USER_VECT_TAB_ADDRESS)
set(MOS_BOARD_BENCHMARKS ON)

set(MCU_FLAGS -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)

set(_dev "${STM32_DIR}/cmsis-device-h7")
set(_hal "${STM32_DIR}/stm32h7xx-hal-driver")

set(BOARD_INCLUDES
    "${STM32_DIR}/cmsis-core/CMSIS/Core/Include"
    "${_dev}/Include"
    "${_hal}/Inc"
)
# The H7 board code uses LL only; no HAL sources are needed.
set(BOARD_SOURCES
    "${_dev}/Source/Templates/gcc/startup_stm32h747xx.s"
    "${_dev}/Source/Templates/system_stm32h7xx.c"
    "${_hal}/Src/stm32h7xx_ll_gpio.c"
)
set(LINKER_SCRIPT "${_dev}/Source/Templates/gcc/linker/stm32h747xx_flash_CM7.ld")
