# Renode RISC-V32 Virtual (riscv_virt, RV32IMAC). See examples/README.md.

set(MOS_TOOLCHAIN     riscv-none-elf) # xPack GNU RISC-V Embedded GCC
set(MOS_MCU           "RISCV32-VIRT")
set(MOS_ARCH_DEF      MOS_ARCH_RISCV32)
set(MOS_FAMILY_DEF    MOS_FAMILY_RISCV_VIRT)
set(MOS_BOARD_NS      "RiscV32Virt")
set(MOS_BOARD_BENCHMARKS OFF)         # Benchmark ports are Cortex-M only

set(MOS_BOARD_DEFS
    MOS_CONF_USE_HARD_FPU=false      # RV32IMAC: no FPU
    MOS_RISCV_MTIME_HZ=10000000      # CLINT mtime rate of riscv_virt
)

set(MCU_FLAGS -march=rv32imac_zicsr_zifencei -mabi=ilp32 -mcmodel=medany)
# The xPack multilibs have no rv32imac variant; link the rv32imc libraries
# (same ilp32 ABI, they do not need atomics). LTO keeps compiling our code
# for rv32imac, so atomics stay native AMO instructions.
set(MCU_LINK_FLAGS -march=rv32imc_zicsr_zifencei -nostartfiles)

set(BOARD_SOURCES "${BOARD_DIR}/startup.S")
set(LINKER_SCRIPT "${BOARD_DIR}/riscv32-virt.ld")
