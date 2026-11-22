# Renode Emulation

This directory contains the generic emulation scripts. Everything specific
to a board (platform additions, machine setup, debug and multi-machine
scripts) lives in `examples/boards/<board>/renode/`, see [examples/README.md](../examples/README.md).

## Layout

```text
emulation/
├── run.sh            # interactive build + emulation launcher
├── selftest.sh       # headless kernel self-test (every board x policy)
├── renode.sh         # shared helpers (board discovery, headless Renode runs)
├── riscv-gdb.sh      # riscv-none-elf-gdb from PATH or ~/.local/xpacks
└── uart_console.py   # telnet UART client / multi-machine multiplexer

examples/boards/<board>/renode/
├── board.resc        # machine setup, included by everything below
├── debug.resc        # GDB + UART socket (single machine)
├── multi_mach.resc   # M1/M2 UART hub
└── *.repl            # board-specific platform additions
```

The benchmark runners (`benchmarks/<suite>/run.sh`, `benchmarks/compare.sh`)
use `renode.sh` as well; see [benchmarks/README.md](../benchmarks/README.md).

## Requirements

- Renode in `PATH`
- `python3`
- `arm-none-eabi-gdb` (STM32) or `riscv-none-elf-gdb` (HiFive1, from the
  xPack toolchain; found under `~/.local/xpacks/` if not in `PATH`) for
  `demo` / `multi` modes (the `debug` modes do not start GDB automatically)

## Quick start

Run from the repository root:

```bash
./emulation/run.sh
```

The script asks for a board and a mode, builds the board example with
`./build.sh <board>`, and starts Renode. Firmware artifacts live under
`build/<board>/<board>.elf`.

Non-interactive form:

```bash
./emulation/run.sh stm32f4-disco demo
./emulation/run.sh stm32h7-disco debug
./emulation/run.sh stm32f4-disco multi
./emulation/run.sh stm32h7-disco multi-debug
./emulation/run.sh stm32f4-disco demo --no-build
./emulation/run.sh hifive1 demo                 # RISC-V
```

| Mode | Behavior |
|---|---|
| `demo` | Build, start Renode, load firmware through GDB, attach the UART console |
| `debug` | Build, start Renode + GDB server (`1234`), print connection instructions |
| `multi` | Build, start M1/M2, start two GDB sessions, multiplex both UART consoles |
| `multi-debug` | Build, start M1/M2 with GDB servers (`1234` / `1235`) |

## Self-test

`emulation/selftest.sh` builds the board example once per scheduling policy,
runs it headless in Renode and collects the `[TEST] ... PASS/FAIL` lines from
`examples/common/kernel_tests.hpp`. It exits non-zero on any failure or timeout, so it
can gate CI:

```bash
./emulation/selftest.sh                           # every board, all six policies
./emulation/selftest.sh stm32h7-disco --policies PreemptPri,Cooperative
./emulation/selftest.sh --no-build                # reuse build/selftest/...
```

Builds and UART logs go to `build/selftest/<board>/<policy>/`.

## Ports

| Board | Single UART | Single GDB | Multi UART | Multi GDB |
|---|---|---|---|---|
| STM32F4 | `3333` | `1234` | `3333` (M1), `3334` (M2) | `1234`, `1235` |
| STM32H747 | `3333` | `1234` | `3333` (M1), `3334` (M2) | `1234`, `1235` |
| HiFive1 | `3333` | `1234` | `3333` (M1), `3334` (M2) | `1234`, `1235` |

## HiFive1 model notes

Renode has no HiFive1 board file, so `examples/boards/hifive1/renode/` builds one on top
of `platforms/cpus/sifive-fe310.repl`:

- `clint.frequency` is set to 32768 Hz, the RTC clock that drives `mtime`
  on the real board (the stock model uses 62 MHz). The firmware assumes it.
- The PRCI clock block is not modeled; `board.resc` tags its registers so
  the firmware's "oscillator ready" wait completes.
- `cpu PerformanceInMips 16` matches the 16 MHz core clock of the BSP.
- Renode's `SiFive_GPIO` only latches an external level on a pin whose
  `input_en` and `output_en` bits are both set. `multi_mach.resc` sets them
  for the id strap (GPIO9) before pressing it on M1, and the firmware samples
  the strap once at boot.

The UART sockets speak telnet. `uart_console.py` handles the negotiation and
can prefix output from multiple machines with `[M1]` / `[M2]`.

## VS Code + GDB

The debug scripts are intentionally independent of the launcher:

```bash
renode --console examples/boards/stm32f4-disco/renode/debug.resc
renode --console examples/boards/stm32h7-disco/renode/debug.resc
```

Then press `F5` and choose `Cortex Debug (F4)`, `Cortex Debug (H7)` or
`RISC-V Debug (HiFive1)` (it uses `emulation/riscv-gdb.sh`). The matching
VS Code tasks are `Run Renode (F4)` / `(H7)` / `(HiFive1)`.

For multi-machine debugging, use the `Run Renode (<board> multi-machine)`
task and attach GDB to `1234` / `1235`.

Every `.resc` ends with:

```text
echo "MOS_RENODE_READY"
```

The launcher and VS Code background-task matcher wait for this marker, so GDB
does not connect while the Renode script is still creating machines.

## Adding a board

See [examples/README.md](../examples/README.md#add-a-board): a new
`examples/boards/<board>/` directory with `board.cmake`, `board.env` and `renode/` is
picked up by every script automatically. Keep `echo "MOS_RENODE_READY"` at
the end of `debug.resc` and `multi_mach.resc`.
