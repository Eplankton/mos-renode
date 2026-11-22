# SiFive HiFive1 Rev B (FE310, RV32IMAC, 16 KB RAM). See examples/README.md.

set(MOS_TOOLCHAIN     riscv-none-elf) # xPack GNU RISC-V Embedded GCC
set(MOS_MCU           "FE310")
set(MOS_ARCH_DEF      MOS_ARCH_RISCV32)
set(MOS_FAMILY_DEF    MOS_FAMILY_FE310)
set(MOS_BOARD_NS      "HiFive1")
set(MOS_BOARD_BENCHMARKS OFF)         # Benchmark ports are Cortex-M only

# 16 KB of RAM: a smaller page pool, task table and async executor.
set(MOS_BOARD_DEFS
    MOS_CONF_USE_HARD_FPU=false
    MOS_CONF_TASK_MAX=12
    MOS_CONF_POOL_SIZE=9
    MOS_CONF_ASYNC_TASK_MAX=8
)

set(MCU_FLAGS -march=rv32imac_zicsr_zifencei -mabi=ilp32 -mcmodel=medlow)
# The xPack multilibs have no rv32imac variant; link the rv32imc libraries
# (same ilp32 ABI, they do not need atomics). LTO keeps compiling our code
# for rv32imac, so atomics stay native AMO instructions.
set(MCU_LINK_FLAGS -march=rv32imc_zicsr_zifencei -nostartfiles)

# Register definitions generated from SiFive's design.svd (vendor/sifive/)
set(BOARD_INCLUDES "${CMAKE_SOURCE_DIR}/vendor/sifive/hifive1-revb")

set(BOARD_SOURCES "${BOARD_DIR}/startup.S")
set(LINKER_SCRIPT "${BOARD_DIR}/hifive1.ld")
