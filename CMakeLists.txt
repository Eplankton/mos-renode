cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ==============================================================================
# Board Selection
#
# Every directory examples/boards/<board>/ with a board.cmake is a board. board.cmake
# only sets variables (toolchain, MCU flags, sources, linker script) and is
# read before project(), because the board decides the toolchain. See
# examples/README.md for the variables it must provide.
# ==============================================================================
file(GLOB _mos_board_files RELATIVE "${CMAKE_SOURCE_DIR}/examples/boards"
    "${CMAKE_SOURCE_DIR}/examples/boards/*/board.cmake")
set(MOS_BOARDS "")
foreach(_f IN LISTS _mos_board_files)
    get_filename_component(_b "${_f}" DIRECTORY)
    list(APPEND MOS_BOARDS "${_b}")
endforeach()

set(MOS_BOARD "stm32f4-disco" CACHE STRING "Target board")
set_property(CACHE MOS_BOARD PROPERTY STRINGS ${MOS_BOARDS})
if(NOT MOS_BOARD IN_LIST MOS_BOARDS)
    message(FATAL_ERROR "Unknown MOS_BOARD='${MOS_BOARD}'. Boards: ${MOS_BOARDS}")
endif()

set(CORE_DIR  "${CMAKE_SOURCE_DIR}/core")
set(STM32_DIR "${CMAKE_SOURCE_DIR}/vendor/stm32")
set(BOARD_DIR "${CMAKE_SOURCE_DIR}/examples/boards/${MOS_BOARD}")

# Defaults a board may override
set(MOS_DEVICE_HEADER "")
set(MOS_EXTRA_DEFS "")
set(MOS_BOARD_DEFS "")
set(MCU_LINK_FLAGS "")
set(BOARD_INCLUDES "")
set(MOS_BOARD_BENCHMARKS OFF)

include("${BOARD_DIR}/board.cmake")

# ==============================================================================
# Toolchain
# ==============================================================================
if(MOS_TOOLCHAIN STREQUAL "riscv-none-elf")
    # xPack GNU RISC-V Embedded GCC (ships newlib-nano and libstdc++).
    # Searched in MOS_RISCV_TOOLCHAIN_DIR, ~/.local/xpacks and PATH.
    set(MOS_RISCV_TOOLCHAIN_DIR "" CACHE PATH "Directory holding riscv-none-elf-gcc")
    file(GLOB _mos_xpack_bins "$ENV{HOME}/.local/xpacks/xpack-riscv-none-elf-gcc-*/bin")
    find_program(MOS_RISCV_GCC riscv-none-elf-gcc
        HINTS ${MOS_RISCV_TOOLCHAIN_DIR} ${_mos_xpack_bins})
    if(NOT MOS_RISCV_GCC)
        message(FATAL_ERROR
            "riscv-none-elf-gcc not found. Install the xPack RISC-V toolchain "
            "(https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack) under "
            "~/.local/xpacks/ or pass -DMOS_RISCV_TOOLCHAIN_DIR=<dir>/bin")
    endif()
    get_filename_component(_mos_tc_dir "${MOS_RISCV_GCC}" DIRECTORY)
    set(TC_PREFIX "${_mos_tc_dir}/riscv-none-elf-")
    set(CMAKE_SYSTEM_PROCESSOR riscv)
elseif(MOS_TOOLCHAIN STREQUAL "arm-none-eabi")
    set(TC_PREFIX arm-none-eabi-)
    set(CMAKE_SYSTEM_PROCESSOR arm)
else()
    message(FATAL_ERROR "examples/boards/${MOS_BOARD}/board.cmake: unknown MOS_TOOLCHAIN='${MOS_TOOLCHAIN}'")
endif()

set(CMAKE_C_COMPILER   ${TC_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TC_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TC_PREFIX}gcc)
set(CMAKE_OBJCOPY      ${TC_PREFIX}objcopy)
set(CMAKE_SIZE         ${TC_PREFIX}size)

project(mos-renode C CXX ASM)

# ==============================================================================
# Options
# ==============================================================================
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

# Thread-Metric benchmark firmware (one ELF per test, see
# benchmarks/thread_metric/README.md).
set(MOS_THREAD_METRIC OFF CACHE BOOL "Build the Thread-Metric benchmark firmware")
set(MOS_TM_DURATION 5 CACHE STRING "Thread-Metric reporting interval in seconds")
set(MOS_TM_CYCLES 1 CACHE STRING "Thread-Metric reporting cycles (0 = forever)")

# RTOS-Benchmark firmware (see benchmarks/rtos_benchmark/README.md).
set(MOS_RTOS_BENCHMARK OFF CACHE BOOL "Build the RTOS-Benchmark firmware")
set(MOS_RB_ITERATIONS 1000 CACHE STRING "RTOS-Benchmark iterations per test")
# Clock actually driving the benchmark timer. 0 means "use SystemCoreClock",
# which is correct on real hardware. Renode models the STM32F4 TIM2 at 10 MHz
# and the STM32H7 DWT at 250 MHz regardless of SystemCoreClock, so the
# benchmark runner overrides this for emulation runs.
set(MOS_RB_TIMER_HZ 0 CACHE STRING "RTOS-Benchmark timer clock in Hz (0 = SystemCoreClock)")
# Build the same benchmarks on FreeRTOS too (tm_freertos_*.elf,
# rtos_benchmark_freertos.elf), for the MOS-vs-FreeRTOS comparison.
set(MOS_BENCH_FREERTOS OFF CACHE BOOL "Also build the benchmarks on FreeRTOS")
# Kernel assertions in the benchmark builds (MOS_CONF_ASSERT, FreeRTOS
# configASSERT). On by default, as in the MOS defaults; the comparison runs
# both kernels without them.
set(MOS_BENCH_ASSERT ON CACHE BOOL "Keep kernel assertions in the benchmark builds")

# ==============================================================================
# Dependencies (git submodules)
# ==============================================================================
set(ETL_DIR "${CORE_DIR}/external/etl")
set(TM_DIR  "${CMAKE_SOURCE_DIR}/benchmarks/external/thread-metric-benchmark")
set(RB_DIR  "${CMAKE_SOURCE_DIR}/benchmarks/external/rtos-benchmark")
set(FREERTOS_DIR "${CMAKE_SOURCE_DIR}/benchmarks/external/FreeRTOS-Kernel")

# Initialize only the submodules this configuration needs.
set(_mos_needed_submodules "")
if(NOT EXISTS "${ETL_DIR}/CMakeLists.txt")
    list(APPEND _mos_needed_submodules core)
endif()
if(MOS_DEVICE_HEADER AND NOT EXISTS "${STM32_DIR}/cmsis-core/CMakeLists.txt")
    list(APPEND _mos_needed_submodules vendor/stm32)
endif()
if(MOS_THREAD_METRIC AND NOT EXISTS "${TM_DIR}/include/tm_api.h")
    list(APPEND _mos_needed_submodules benchmarks/external/thread-metric-benchmark)
endif()
if(MOS_RTOS_BENCHMARK AND NOT EXISTS "${RB_DIR}/h/bench_api.h")
    list(APPEND _mos_needed_submodules benchmarks/external/rtos-benchmark)
endif()
if(MOS_BENCH_FREERTOS AND NOT EXISTS "${FREERTOS_DIR}/tasks.c")
    list(APPEND _mos_needed_submodules benchmarks/external/FreeRTOS-Kernel)
endif()

if(_mos_needed_submodules)
    find_package(Git QUIET)
    if(GIT_FOUND)
        message(STATUS "Initializing git submodules: ${_mos_needed_submodules}")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --checkout --recursive ${_mos_needed_submodules}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
    endif()
endif()

if(NOT EXISTS "${ETL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "ETL submodule is missing. Run: git submodule update --init --recursive")
endif()

# The kernel: mos::core (sources, include root, ETL)
add_subdirectory(${CORE_DIR})

# ==============================================================================
# Firmware Setup
# ==============================================================================
# Board sources, STM32 device setup and toolchain flags. Shared by the MOS
# firmware and the FreeRTOS reference builds of the benchmarks, so that both
# kernels are compiled the same way.
function(mos_setup_target target)
    target_sources(${target} PRIVATE ${BOARD_SOURCES})

    target_compile_features(${target} PRIVATE c_std_17 cxx_std_23)

    # The board's own headers and drivers
    target_include_directories(${target} PRIVATE
        ${BOARD_DIR}
        ${BOARD_INCLUDES}
    )

    # STM32: CMSIS device header and the ST HAL/LL drivers
    if(MOS_DEVICE_HEADER)
        target_compile_definitions(${target} PRIVATE
            ${MOS_MCU}
            ${MOS_FAMILY_DEF}
            USE_FULL_LL_DRIVER
            USE_HAL_DRIVER
            MOS_DEVICE_HEADER="${MOS_DEVICE_HEADER}"
        )
    endif()

    target_compile_options(${target} PRIVATE
        ${MCU_FLAGS}
        -Os -Wall -g
        -ffunction-sections -fdata-sections
        -funsigned-char -fno-exceptions
        --specs=nano.specs
        -flto
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fcoroutines -fno-use-cxa-atexit>
        $<$<COMPILE_LANGUAGE:C>:-Wno-deprecated-volatile>
    )

    target_link_options(${target} PRIVATE
        ${MCU_FLAGS}
        ${MCU_LINK_FLAGS}
        -T${LINKER_SCRIPT}
        -Wl,--gc-sections --specs=nosys.specs --specs=nano.specs
        -Wl,--print-memory-usage
        -flto
        -Wl,-Map=${CMAKE_BINARY_DIR}/${target}.map
    )
endfunction()

# A MOS firmware for the selected board.
function(mos_setup_firmware target)
    mos_setup_target(${target})

    target_compile_definitions(${target} PRIVATE
        ${MOS_ARCH_DEF}
        ${MOS_FAMILY_DEF}
        ${MOS_EXTRA_DEFS}
        ${MOS_BOARD_DEFS}
        MOS_MCU="${MOS_MCU}"
        MOS_BOARD_NAMESPACE=${MOS_BOARD_NS}
    )

    # "common/..." shared by the examples
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/examples)

    if(MOS_SCHED_POLICY)
        target_compile_definitions(${target} PRIVATE
            MOS_CONF_SCHED_POLICY=${MOS_SCHED_POLICY}
        )
    endif()

    if(MOS_SCHED_DEMO)
        target_compile_definitions(${target} PRIVATE MOS_SCHED_DEMO=1)
    endif()

    target_link_libraries(${target} PRIVATE mos::core)
endfunction()

# ==============================================================================
# Board Example
# ==============================================================================
set(TARGET_NAME ${MOS_BOARD})

add_executable(${TARGET_NAME}.elf "${BOARD_DIR}/main.cpp")
mos_setup_firmware(${TARGET_NAME}.elf)

add_custom_command(TARGET ${TARGET_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET_NAME}.elf> ${TARGET_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}.elf> ${TARGET_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}.elf>
    COMMENT "Generating HEX, BIN, and size report..."
)

# ==============================================================================
# Benchmarks
# ==============================================================================
if((MOS_THREAD_METRIC OR MOS_RTOS_BENCHMARK) AND NOT MOS_BOARD_BENCHMARKS)
    message(FATAL_ERROR "The benchmark ports do not support MOS_BOARD=${MOS_BOARD} (Cortex-M only)")
endif()

if(MOS_BENCH_FREERTOS)
    if(NOT MOS_BOARD_FREERTOS)
        message(FATAL_ERROR "The FreeRTOS reference builds support only boards with MOS_BOARD_FREERTOS (stm32f4-disco)")
    endif()
    if(NOT EXISTS "${FREERTOS_DIR}/tasks.c")
        message(FATAL_ERROR
            "FreeRTOS-Kernel submodule is missing. Run: git submodule update --init --checkout benchmarks/external/FreeRTOS-Kernel")
    endif()
endif()

# A MOS benchmark firmware.
function(mos_setup_bench target)
    mos_setup_firmware(${target})
    if(NOT MOS_BENCH_ASSERT)
        target_compile_definitions(${target} PRIVATE MOS_CONF_ASSERT=false)
    endif()
endfunction()

# A FreeRTOS firmware with the same board sources and flags as MOS.
function(mos_setup_freertos target)
    mos_setup_target(${target})
    if(NOT MOS_BENCH_ASSERT)
        target_compile_definitions(${target} PRIVATE MOS_BENCH_NO_ASSERT=1)
    endif()

    target_sources(${target} PRIVATE
        ${FREERTOS_DIR}/tasks.c
        ${FREERTOS_DIR}/queue.c
        ${FREERTOS_DIR}/list.c
        ${FREERTOS_DIR}/portable/GCC/${MOS_BOARD_FREERTOS_PORT}/port.c
        ${FREERTOS_DIR}/portable/MemMang/heap_4.c
        ${CORE_DIR}/libc/syscall.c # newlib stubs, as in the MOS build
    )
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/benchmarks/freertos
        ${FREERTOS_DIR}/include
        ${FREERTOS_DIR}/portable/GCC/${MOS_BOARD_FREERTOS_PORT}
    )
endfunction()

if(MOS_THREAD_METRIC)
    if(NOT EXISTS "${TM_DIR}/include/tm_api.h")
        message(FATAL_ERROR
            "Thread-Metric submodule is missing. Run: git submodule update --init --checkout benchmarks/external/thread-metric-benchmark")
    endif()

    set(TM_TESTS
        basic_processing
        cooperative_scheduling
        preemptive_scheduling
        interrupt_processing
        interrupt_preemption_processing
        message_processing
        synchronization_processing
        memory_allocation
    )

    foreach(tm_test IN LISTS TM_TESTS)
        set(tm_target "tm_${tm_test}.elf")

        add_executable(${tm_target}
            benchmarks/thread_metric/main.cpp
            ${TM_DIR}/src/tm_report.c
            ${TM_DIR}/src/${tm_test}.c
        )

        mos_setup_bench(${tm_target})

        target_include_directories(${tm_target} PRIVATE ${TM_DIR}/include)
        target_compile_definitions(${tm_target} PRIVATE
            TM_SEMIHOSTING=1
            TM_TEST_DURATION=${MOS_TM_DURATION}
            TM_TEST_CYCLES=${MOS_TM_CYCLES}
        )

        add_custom_command(TARGET ${tm_target} POST_BUILD
            COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${tm_target}>
            COMMENT "Thread-Metric size report: ${tm_test}"
        )

        if(MOS_BENCH_FREERTOS)
            # Upstream's FreeRTOS port, unmodified, plus the STM32 glue
            set(tm_fr_target "tm_freertos_${tm_test}.elf")
            add_executable(${tm_fr_target}
                benchmarks/freertos/tm_main.c
                ${TM_DIR}/ports/freertos/tm_port.c
                ${TM_DIR}/src/tm_report.c
                ${TM_DIR}/src/${tm_test}.c
            )
            mos_setup_freertos(${tm_fr_target})
            target_include_directories(${tm_fr_target} PRIVATE ${TM_DIR}/include)
            target_compile_definitions(${tm_fr_target} PRIVATE
                TM_SEMIHOSTING=1
                TM_TEST_DURATION=${MOS_TM_DURATION}
                TM_TEST_CYCLES=${MOS_TM_CYCLES}
            )
        endif()
    endforeach()
endif()

if(MOS_RTOS_BENCHMARK)
    if(NOT EXISTS "${RB_DIR}/h/bench_api.h")
        message(FATAL_ERROR
            "RTOS-Benchmark submodule is missing. Run: git submodule update --init --checkout benchmarks/external/rtos-benchmark")
    endif()

    set(MOS_RB_PORT_HEADER
        "${CMAKE_SOURCE_DIR}/benchmarks/rtos_benchmark/bench_porting_layer_mos.h")

    set(RB_SOURCES
        ${RB_DIR}/src/common/bench_utils.c
        ${RB_DIR}/src/common/bench_all.c
        ${RB_DIR}/src/common/bench_thread_test.c
        ${RB_DIR}/src/common/bench_thread_switch_yield_test.c
        ${RB_DIR}/src/common/bench_mutex_lock_unlock_test.c
        ${RB_DIR}/src/common/bench_sem_context_switch_test.c
        ${RB_DIR}/src/common/bench_sem_signal_release_test.c
        ${RB_DIR}/src/common/bench_malloc_free_test.c
        ${RB_DIR}/src/common/bench_message_queue_test.c
        ${RB_DIR}/src/common/bench_interrupt_latency_test.c
    )

    add_executable(rtos_benchmark.elf
        benchmarks/rtos_benchmark/main.cpp
        ${RB_SOURCES}
    )

    mos_setup_bench(rtos_benchmark.elf)

    target_include_directories(rtos_benchmark.elf PRIVATE
        ${RB_DIR}/h
        ${CMAKE_SOURCE_DIR}/benchmarks/rtos_benchmark
    )
    target_compile_definitions(rtos_benchmark.elf PRIVATE
        ITERATIONS=${MOS_RB_ITERATIONS}
        MOS_RB_TIMER_HZ=${MOS_RB_TIMER_HZ}
    )
    target_compile_options(rtos_benchmark.elf PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-include;${MOS_RB_PORT_HEADER}>"
        "$<$<COMPILE_LANGUAGE:CXX>:-include;${MOS_RB_PORT_HEADER}>"
    )

    # The upstream tests pass timestamps through non-volatile file-scope
    # variables written by another task. LTO can hoist those loads across the
    # context-switching call and produce ~2^32 ns deltas, so keep this target
    # without LTO.
    target_compile_options(rtos_benchmark.elf PRIVATE -fno-lto)
    target_link_options(rtos_benchmark.elf PRIVATE -fno-lto)

    add_custom_command(TARGET rtos_benchmark.elf POST_BUILD
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:rtos_benchmark.elf>
        COMMENT "RTOS-Benchmark size report"
    )

    if(MOS_BENCH_FREERTOS)
        set(FR_RB_PORT_HEADER
            "${CMAKE_SOURCE_DIR}/benchmarks/freertos/bench_porting_layer_freertos.h")

        add_executable(rtos_benchmark_freertos.elf
            benchmarks/freertos/rb_port.cpp
            core/libc/printf.c
            ${RB_SOURCES}
        )
        mos_setup_freertos(rtos_benchmark_freertos.elf)
        target_include_directories(rtos_benchmark_freertos.elf PRIVATE ${RB_DIR}/h)
        target_compile_definitions(rtos_benchmark_freertos.elf PRIVATE
            ITERATIONS=${MOS_RB_ITERATIONS}
            MOS_RB_TIMER_HZ=${MOS_RB_TIMER_HZ}
        )
        target_compile_options(rtos_benchmark_freertos.elf PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-include;${FR_RB_PORT_HEADER}>"
            "$<$<COMPILE_LANGUAGE:CXX>:-include;${FR_RB_PORT_HEADER}>"
            -fno-lto
        )
        target_link_options(rtos_benchmark_freertos.elf PRIVATE -fno-lto)
    endif()
endif()
