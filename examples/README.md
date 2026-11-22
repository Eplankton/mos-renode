# Examples

```text
examples/
├── common/           # shared by every board's example
│   ├── kernel_tests.hpp   # kernel test battery (run by emulation/selftest.sh)
│   └── sched_demo.hpp     # periodic scheduling demo (MOS_SCHED_DEMO=ON)
└── boards/<board>/   # one directory per board, see below
```

Every directory under `examples/boards/` is one board. It holds everything
that is specific to that board, drivers included, and the build and emulation
scripts discover boards by listing these directories, so adding a board does
not require editing any script.

```text
examples/boards/<board>/
├── main.cpp          # the board's example program (shell, LED, M2M, tests)
├── board.hpp         # header-only board support: init, UART, LED, _putchar, IRQs
├── uart.hpp          # line-buffered UART (Drivers::SyncUart) on a hardware backend
├── <backend>.hpp     # stm32_usart.hpp + stm32_ll.hpp, or ns16550_uart.hpp
├── <startup/HAL/linker files>
├── board.cmake       # build description, read by CMakeLists.txt
├── board.env         # facts for the shell scripts
└── renode/
    ├── board.resc    # machine setup, shared by every Renode script
    ├── debug.resc    # single machine: GDB on 1234, shell UART on TCP 3333
    ├── multi_mach.resc  # M1/M2 linked through a UART hub
    └── *.repl        # board-specific platform additions
```

Current boards:

| Board | CPU | Notes |
|---|---|---|
| `stm32f4-disco` | Cortex-M4F | STM32 HAL/LL from `vendor/stm32` |
| `stm32h7-disco` | Cortex-M7 | LL only; USART clock fixed to Renode's 200 MHz model (`-DMOS_H7_UART_CLOCK` on hardware) |
| `riscv32-virt` | RV32IMAC | Renode's RISC-V32 Virtual (`riscv_virt`) plus a second UART and a GPIO, see `renode/riscv32-virt.repl` |

Each board is self-contained: the UART driver is copied into every board
directory instead of being shared, so a board can be changed (or taken
elsewhere) without touching the others.

## Build

```bash
./build.sh                        # interactive board menu
./build.sh <board>
./build.sh all
```

Artifacts: `build/<board>/<board>.{elf,bin,hex}`.

## board.cmake

Included by the top-level `CMakeLists.txt` **before** `project()` (the board picks the
toolchain), so it only sets variables. `BOARD_DIR`, `STM32_DIR` and
`CMAKE_SOURCE_DIR` are available.

| Variable | Required | Meaning |
|---|---|---|
| `MOS_TOOLCHAIN` | yes | `arm-none-eabi` or `riscv-none-elf` (xPack) |
| `MOS_MCU` | yes | MCU name, shown by the shell (`uname`) |
| `MOS_ARCH_DEF` | yes | `MOS_ARCH_CORTEX_M4`, `MOS_ARCH_CORTEX_M7` or `MOS_ARCH_RISCV32` |
| `MOS_FAMILY_DEF` | yes | family macro for the board code |
| `MOS_BOARD_NS` | yes | namespace in `MOS::Boards` (`MOS_BOARD_NAMESPACE` in code) |
| `MCU_FLAGS` | yes | compile and link CPU flags |
| `BOARD_SOURCES` | yes | startup/system/HAL sources |
| `LINKER_SCRIPT` | yes | linker script |
| `MOS_DEVICE_HEADER` | STM32 | CMSIS device header; enables the ST HAL/LL setup |
| `BOARD_INCLUDES` | no | extra include directories (CMSIS, HAL) |
| `MOS_EXTRA_DEFS` | no | extra preprocessor definitions |
| `MOS_BOARD_DEFS` | no | kernel configuration overrides, e.g. `MOS_CONF_POOL_SIZE=9` |
| `MCU_LINK_FLAGS` | no | link-only flags |
| `MOS_BOARD_BENCHMARKS` | no | `ON` if the benchmark ports support the board |
| `MOS_BOARD_FREERTOS` | no | `ON` if the FreeRTOS reference build supports the board |
| `MOS_BOARD_FREERTOS_PORT` | with the above | FreeRTOS port directory, e.g. `ARM_CM4F` |

## board.env

Sourced by `emulation/renode.sh` for `build.sh`, `emulation/run.sh`,
`emulation/selftest.sh` and the benchmark runners:

| Variable | Meaning |
|---|---|
| `BOARD_TITLE` | human-readable name for menus |
| `BOARD_GDB` | GDB executable (looked up in `PATH`, then `~/.local/xpacks`) |
| `BOARD_SHELL_UART` | Renode name of the shell UART, e.g. `sysbus.usart2` |
| `BOARD_RB_TIMER_HZ` | RTOS-Benchmark timer clock in Renode; empty if unsupported |
| `BOARD_FREERTOS` | non-empty if `benchmarks/compare.sh` supports the board |

## renode/board.resc

Creates nothing by itself: it loads the platform into the current machine,
applies board-specific settings (clocks, tags) and loads `$bin_path`. Paths
inside are relative to `$ORIGIN`, so it can be included from anywhere:

```text
$bin_path=@build/<board>/<board>.elf
mach create
include @examples/boards/<board>/renode/board.resc
```

`debug.resc` and `multi_mach.resc` add GDB servers, UART sockets and the UART
hub, and end with `echo "MOS_RENODE_READY"` (the launcher and the VS Code
tasks wait for it).

## Add a board

1. Create `examples/boards/<board>/` with the files above. For the UART, copy
   `uart.hpp` and write a backend with `try_read(char&)` and `write(char)`.
2. A new CPU architecture also needs a port in `core/arch/<arch>/` (contract
   in `core/arch/cpu.hpp`).
3. Optionally add VS Code launch/task entries.

`./build.sh`, `./emulation/run.sh` and `./emulation/selftest.sh` pick it up
automatically.
