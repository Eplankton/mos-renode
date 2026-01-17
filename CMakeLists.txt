cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# 0. Critical settings: Skip compiler link checks to resolve bare-metal
# compilation errors like "undefined reference to _exit/_sbrk"
# ==============================================================================
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ==============================================================================
# 1. Cross-compilation toolchain setup
# ==============================================================================
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Specify compiler paths (based on your previous logs, located at /usr/bin)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

# Define project name and enable languages
project(mos-renode C CXX ASM)

# ==============================================================================
# 2. Automatically check and update Git Submodule (core directory)
# ==============================================================================
find_package(Git QUIET)

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    # Check if key files exist in core directory to determine if empty
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/core/kernel/printf.c")
        message(STATUS "Git submodule 'core' seems missing. Initializing...")

        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive core
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE git_submod_result
        )

        if(NOT git_submod_result EQUAL "0")
            message(FATAL_ERROR "git submodule update failed. Please checkout submodules manually.")
        else()
            message(STATUS "Git submodule 'core' updated successfully.")
        endif()
    endif()
endif()

# ==============================================================================
# 3. MCU hardware definitions and compilation options
# ==============================================================================
# 【Important】No quotes, ensure treated as list to fix "unrecognized -mcpu target" error
set(MCU_FLAGS -mcpu=cortex-m4 -mfloat-abi=soft -mthumb)

# C standard (Makefile: -std=c17)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# C++ standard (Makefile: -std=c++23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Global common compilation options
add_compile_options(
    ${MCU_FLAGS}
    -Os
    -Wall
    -g
    -ffunction-sections
    -fdata-sections
    -funsigned-char
    -fno-exceptions
    --specs=nosys.specs
    --specs=nano.specs
)

# C++ specific options (resolve syntax errors)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>)

# C specific options
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-deprecated-volatile>)

# Macro definitions
add_compile_definitions(
    STM32F407xx
    USE_FULL_LL_DRIVER
    USE_HAL_DRIVER
)

# ==============================================================================
# 4. Include paths (Include Paths)
# ==============================================================================
include_directories(
    vendor
    vendor/Drivers/STM32F4xx_HAL_Driver/Src
    vendor/Drivers/STM32F4xx_HAL_Driver/Src/Legacy
    vendor/Drivers/STM32F4xx_HAL_Driver/Inc
    vendor/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    vendor/Drivers/CMSIS/Include
    vendor/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    vendor/Core/Src
    vendor/Core/Inc
    core
    core/kernel/data_type
    core/kernel
    core/arch
    app
)

# ==============================================================================
# 5. Source file lists
# ==============================================================================

# Application and kernel
set(SOURCES_APP
    core/kernel/printf.c
    core/kernel/syscall.c
    app/main.cpp
)

# Startup file
set(SOURCES_ASM
    vendor/startup_stm32f407xx.s
)

# Driver files (use recursive wildcard search to avoid listing hundreds of files)
file(GLOB_RECURSE SOURCES_DRIVERS
    "vendor/Core/Src/*.c"
    "vendor/Drivers/STM32F4xx_HAL_Driver/Src/*.c"
)

# Aggregate all sources
set(ALL_SOURCES ${SOURCES_APP} ${SOURCES_ASM} ${SOURCES_DRIVERS})

# ==============================================================================
# 6. Generate executable file (ELF)
# ==============================================================================
set(TARGET_NAME mos-renode)
add_executable(${TARGET_NAME}.elf ${ALL_SOURCES})

# Linker script
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/vendor/STM32F407VGTx_FLASH.ld")

# Link options
target_link_options(${TARGET_NAME}.elf PRIVATE
    ${MCU_FLAGS}
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map
    --specs=nosys.specs
    --specs=nano.specs
    -lm
    -lsupc++
)

# ==============================================================================
# 7. Post-build steps (Generate HEX / BIN)
# ==============================================================================
add_custom_command(TARGET ${TARGET_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${TARGET_NAME}.elf ${TARGET_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${TARGET_NAME}.elf ${TARGET_NAME}.bin
    COMMAND ${CMAKE_SIZE} ${TARGET_NAME}.elf
    COMMENT "Building ${TARGET_NAME}.hex and ${TARGET_NAME}.bin..."
)