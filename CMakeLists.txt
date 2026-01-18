cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Toolchain & Project Setup
# ==============================================================================
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Standard Cross-compiler tools
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

project(mos-renode C CXX ASM)

# ==============================================================================
# Simplified Submodule Automation (Nested Support)
# ==============================================================================
find_package(Git QUIET)

# Define key paths
set(CORE_DIR "${CMAKE_SOURCE_DIR}/core")
set(ETL_DIR "${CORE_DIR}/external/etl")

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    # check if 'core' is populated OR if 'etl' inside core is populated
    if(NOT EXISTS "${CORE_DIR}/kernel/printf.c" OR NOT EXISTS "${ETL_DIR}/CMakeLists.txt")
        message(STATUS "Submodules missing. Initializing recursively...")
        
        # This one command handles initializing 'core' AND any submodules defined inside 'core'
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE git_res
        )
        
        # Fallback: If 'etl' was never added to 'core' as a submodule, add it now
        if(EXISTS "${CORE_DIR}" AND NOT EXISTS "${ETL_DIR}/CMakeLists.txt")
            message(STATUS "Adding ETL as nested submodule in core...")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} submodule add https://github.com/ETLCPP/etl.git external/etl
                WORKING_DIRECTORY ${CORE_DIR}
            )
            execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        endif()
    endif()
endif()

# ==============================================================================
# Target Configuration
# ==============================================================================
set(TARGET_NAME mos-renode)
set(MCU_FLAGS -mcpu=cortex-m4 -mfloat-abi=soft -mthumb)

# ETL is added first to ensure etl::etl target is available
add_subdirectory(${ETL_DIR})

# Sources grouping
file(GLOB_RECURSE DRIVER_SOURCES 
    "vendor/Core/Src/*.c" 
    "vendor/Drivers/STM32F4xx_HAL_Driver/Src/*.c"
)

add_executable(${TARGET_NAME}.elf 
    core/kernel/printf.c
    core/kernel/syscall.c
    app/main.cpp
    vendor/startup_stm32f407xx.s
    ${DRIVER_SOURCES}
)

# Compile options & Standards
target_compile_features(${TARGET_NAME}.elf PRIVATE c_std_17 cxx_std_23)
target_compile_options(${TARGET_NAME}.elf PRIVATE
    ${MCU_FLAGS}
    -Os -Wall -g -ffunction-sections -fdata-sections
    -funsigned-char -fno-exceptions
    --specs=nosys.specs --specs=nano.specs
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -flto -fcoroutines>
    $<$<COMPILE_LANGUAGE:C>:-Wno-deprecated-volatile>
)

# Definitions
target_compile_definitions(${TARGET_NAME}.elf PRIVATE 
    STM32F407xx USE_FULL_LL_DRIVER USE_HAL_DRIVER
)

# Include paths (Modern approach using target_include_directories)
target_include_directories(${TARGET_NAME}.elf PRIVATE
    vendor vendor/Core/Inc vendor/Core/Src
    vendor/Drivers/STM32F4xx_HAL_Driver/Inc
    vendor/Drivers/CMSIS/Include
    vendor/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    core core/kernel core/kernel/data_type core/arch
    app
)

# Linking
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/vendor/STM32F407VGTx_FLASH.ld")
target_link_libraries(${TARGET_NAME}.elf PRIVATE etl::etl)
target_link_options(${TARGET_NAME}.elf PRIVATE
    ${MCU_FLAGS}
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections --specs=nosys.specs --specs=nano.specs
    -Wl,--print-memory-usage -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map
    -lm -lsupc++
)

# ==============================================================================
# Post-Build Artifacts
# ==============================================================================
add_custom_command(TARGET ${TARGET_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET_NAME}.elf> ${TARGET_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}.elf> ${TARGET_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}.elf>
    COMMENT "Generating HEX, BIN, and size report..."
)