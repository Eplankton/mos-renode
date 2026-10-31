cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Toolchain & Project Setup
# ==============================================================================
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TC_PREFIX arm-none-eabi-)
set(CMAKE_C_COMPILER ${TC_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TC_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TC_PREFIX}gcc)
set(CMAKE_OBJCOPY ${TC_PREFIX}objcopy)
set(CMAKE_SIZE ${TC_PREFIX}size)

project(mos-renode C CXX ASM)

# ==============================================================================
# Board Selection
# ==============================================================================
set(MOS_BOARD "stm32f4-disco" CACHE STRING "Target board")
set_property(CACHE MOS_BOARD PROPERTY STRINGS
    stm32f4-disco
    stm32h7-disco
)

# Overrides MOS_CONF_SCHED_POLICY from config.h when non-empty, e.g.
#   -DMOS_SCHED_POLICY=EarliestDeadlineFirst
set(MOS_SCHED_POLICY "" CACHE STRING "Scheduler policy override")
set_property(CACHE MOS_SCHED_POLICY PROPERTY STRINGS
    RoundRobin
    PreemptPri
    RateMonotonic
    DeadlineMonotonic
    EarliestDeadlineFirst
    Cooperative
)

set(MOS_SCHED_DEMO OFF CACHE BOOL "Build the periodic scheduling demo")

# ==============================================================================
# Dependency Layout
# ==============================================================================
set(CORE_DIR    "${CMAKE_SOURCE_DIR}/core")
set(ETL_DIR     "${CORE_DIR}/external/etl")
set(STM32_DIR   "${CMAKE_SOURCE_DIR}/vendor/stm32")

find_package(Git QUIET)

# ETL is a nested submodule of core; STM32 components are top-level submodules.
if(NOT EXISTS "${ETL_DIR}/CMakeLists.txt"
   OR NOT EXISTS "${STM32_DIR}/cmsis-core/CMakeLists.txt")
    if(GIT_FOUND)
        message(STATUS "Initializing git submodules...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
    endif()
endif()

if(NOT EXISTS "${ETL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "ETL submodule is missing. Run: git submodule update --init --recursive")
endif()

# ==============================================================================
# Board / MCU Configuration
# ==============================================================================
if(MOS_BOARD STREQUAL "stm32f4-disco")
    set(MOS_MCU              "STM32F407xx")
    set(MOS_DEVICE_HEADER    "stm32f4xx.h")
    set(MOS_ARCH_DEF         MOS_ARCH_CORTEX_M4)
    set(MOS_FAMILY_DEF       MOS_FAMILY_STM32F4)
    set(MOS_EXTRA_DEFS       "")

    set(MCU_FLAGS
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
    )

    set(DEVICE_DIR  "${STM32_DIR}/cmsis-device-f4")
    set(HAL_DIR     "${STM32_DIR}/stm32f4xx-hal-driver")
    set(STARTUP_SRC "${DEVICE_DIR}/Source/Templates/gcc/startup_stm32f407xx.s")
    set(SYSTEM_SRC  "${DEVICE_DIR}/Source/Templates/system_stm32f4xx.c")
    set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/examples/boards/stm32f4-disco/STM32F407VGTx_FLASH.ld")
    set(BOARD_DIR   "${CMAKE_SOURCE_DIR}/examples/boards/stm32f4-disco")

    set(HAL_SOURCES
        "${HAL_DIR}/Src/stm32f4xx_hal.c"
        "${HAL_DIR}/Src/stm32f4xx_hal_cortex.c"
        "${HAL_DIR}/Src/stm32f4xx_hal_rcc.c"
        "${HAL_DIR}/Src/stm32f4xx_hal_rcc_ex.c"
        "${HAL_DIR}/Src/stm32f4xx_hal_pwr.c"
        "${HAL_DIR}/Src/stm32f4xx_hal_pwr_ex.c"
        "${HAL_DIR}/Src/stm32f4xx_hal_flash.c"
        "${HAL_DIR}/Src/stm32f4xx_hal_flash_ex.c"
        "${HAL_DIR}/Src/stm32f4xx_ll_gpio.c"
    )
elseif(MOS_BOARD STREQUAL "stm32h7-disco")
    set(MOS_MCU              "STM32H747xx")
    set(MOS_DEVICE_HEADER    "stm32h747xx.h")
    set(MOS_ARCH_DEF         MOS_ARCH_CORTEX_M7)
    set(MOS_FAMILY_DEF       MOS_FAMILY_STM32H7)
    # H747 is dual-core; this project targets the Cortex-M7 side.
    set(MOS_EXTRA_DEFS       "CORE_CM7;USER_VECT_TAB_ADDRESS")

    set(MCU_FLAGS
        -mcpu=cortex-m7
        -mthumb
        -mfpu=fpv5-d16
        -mfloat-abi=hard
    )

    set(DEVICE_DIR  "${STM32_DIR}/cmsis-device-h7")
    set(HAL_DIR     "${STM32_DIR}/stm32h7xx-hal-driver")
    set(STARTUP_SRC "${DEVICE_DIR}/Source/Templates/gcc/startup_stm32h747xx.s")
    set(SYSTEM_SRC  "${DEVICE_DIR}/Source/Templates/system_stm32h7xx.c")
    set(LINKER_SCRIPT "${DEVICE_DIR}/Source/Templates/gcc/linker/stm32h747xx_flash_CM7.ld")
    set(BOARD_DIR   "${CMAKE_SOURCE_DIR}/examples/boards/stm32h7-disco")

    # The H7 BSP uses LL only; no HAL source files are required.
    set(HAL_SOURCES
        "${HAL_DIR}/Src/stm32h7xx_ll_gpio.c"
    )
else()
    message(FATAL_ERROR "Unsupported MOS_BOARD='${MOS_BOARD}'")
endif()

# ==============================================================================
# Example Selection
# ==============================================================================
set(EXAMPLE_SRC "${CMAKE_SOURCE_DIR}/examples/boards/${MOS_BOARD}/main.cpp")
if(NOT EXISTS "${EXAMPLE_SRC}")
    message(FATAL_ERROR "Example not found: ${EXAMPLE_SRC}")
endif()

# ==============================================================================
# Target Configuration
# ==============================================================================
set(TARGET_NAME ${MOS_BOARD})

# Use EXCLUDE_FROM_ALL to prevent building ETL tests or examples
add_subdirectory(${ETL_DIR} EXCLUDE_FROM_ALL)

add_executable(${TARGET_NAME}.elf
    core/kernel/printf.c
    core/kernel/global.cpp
    core/kernel/syscall.c
    ${EXAMPLE_SRC}
    ${STARTUP_SRC}
    ${SYSTEM_SRC}
    ${HAL_SOURCES}
)

# ==============================================================================
# Build Options
# ==============================================================================
target_compile_features(${TARGET_NAME}.elf PRIVATE c_std_17 cxx_std_23)
target_compile_definitions(${TARGET_NAME}.elf PRIVATE
    ${MOS_MCU}
    ${MOS_ARCH_DEF}
    ${MOS_FAMILY_DEF}
    ${MOS_EXTRA_DEFS}
    USE_FULL_LL_DRIVER
    USE_HAL_DRIVER
    MOS_DEVICE_HEADER="${MOS_DEVICE_HEADER}"
    MOS_MCU="${MOS_MCU}"
)

target_include_directories(${TARGET_NAME}.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/examples
    ${CMAKE_SOURCE_DIR}/examples/common
    ${BOARD_DIR}

    ${CORE_DIR}
    ${CORE_DIR}/kernel
    ${CORE_DIR}/kernel/data_type
    ${CORE_DIR}/arch

    ${STM32_DIR}/cmsis-core/CMSIS/Core/Include
    ${DEVICE_DIR}/Include
    ${HAL_DIR}/Inc
)

if(MOS_SCHED_POLICY)
    target_compile_definitions(${TARGET_NAME}.elf PRIVATE
        MOS_CONF_SCHED_POLICY=${MOS_SCHED_POLICY}
    )
endif()

if(MOS_SCHED_DEMO)
    target_compile_definitions(${TARGET_NAME}.elf PRIVATE MOS_SCHED_DEMO=1)
endif()

target_compile_options(${TARGET_NAME}.elf PRIVATE
    ${MCU_FLAGS}
    -Os -Wall -g
    -ffunction-sections -fdata-sections
    -funsigned-char -fno-exceptions
    --specs=nano.specs
    -flto
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fcoroutines -fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:C>:-Wno-deprecated-volatile>
)

target_link_libraries(${TARGET_NAME}.elf PRIVATE etl::etl)

target_link_options(${TARGET_NAME}.elf PRIVATE
    ${MCU_FLAGS}
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections --specs=nosys.specs --specs=nano.specs
    -Wl,--print-memory-usage
    -flto
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map
)

# ==============================================================================
# Post-Build Steps
# ==============================================================================
add_custom_command(TARGET ${TARGET_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET_NAME}.elf> ${TARGET_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}.elf> ${TARGET_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}.elf>
    COMMENT "Generating HEX, BIN, and size report..."
)
