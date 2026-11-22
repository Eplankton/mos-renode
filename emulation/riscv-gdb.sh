#!/usr/bin/env bash
# riscv-none-elf-gdb from PATH or ~/.local/xpacks, for tools that need a
# single executable path (VS Code's "gdbPath").
source "$(dirname "${BASH_SOURCE[0]}")/renode.sh"
gdb="$(mos_find_tool riscv-none-elf-gdb)" || {
    echo "riscv-none-elf-gdb not found (install the xPack RISC-V toolchain)" >&2
    exit 1
}
exec "$gdb" "$@"
