# Renode Emulation

This directory contains the Renode launcher, UART console helper and
board-specific platform/script files.

## Layout

```text
emulation/
├── run.sh                  # interactive build + emulation launcher
├── uart_console.py         # telnet UART client / multi-machine multiplexer
├── stm32f4-disco/
│   ├── add_on.repl         # virtual LED / button
│   ├── debug.resc          # GDB + UART socket (single machine)
│   └── multi_mach.resc     # M1/M2 UART hub (USART3)
└── stm32h7-disco/
    ├── add_on.repl         # virtual LED / button
    ├── debug.resc          # GDB + UART socket (single machine)
    └── multi_mach.resc     # M1/M2 UART hub (USART6)
```

## Requirements

- Renode in `PATH`
- `python3`
- `arm-none-eabi-gdb` for `demo` / `multi` modes (the `debug` modes do not
  start GDB automatically)

## Quick start

Run from the repository root:

```bash
./emulation/run.sh
```

The script asks for a board and a mode, builds the board example with
`./build.sh stm32f4-disco` / `./build.sh stm32h7-disco`, and starts Renode.
Firmware artifacts live under:

```text
build/stm32f4-disco/stm32f4-disco.elf
build/stm32h7-disco/stm32h7-disco.elf
```

Non-interactive form:

```bash
./emulation/run.sh stm32f4-disco demo
./emulation/run.sh stm32h7-disco debug
./emulation/run.sh stm32f4-disco multi
./emulation/run.sh stm32h7-disco multi-debug
./emulation/run.sh stm32f4-disco demo --no-build
```

| Mode | Behavior |
|---|---|
| `demo` | Build, start Renode, load firmware through GDB, attach the UART console |
| `debug` | Build, start Renode + GDB server (`1234`), print connection instructions |
| `multi` | Build, start M1/M2, start two GDB sessions, multiplex both UART consoles |
| `multi-debug` | Build, start M1/M2 with GDB servers (`1234` / `1235`) |

## Ports

| Board | Single UART | Single GDB | Multi UART | Multi GDB |
|---|---|---|---|---|
| STM32F4 | `3333` | `1234` | `3333` (M1), `3334` (M2) | `1234`, `1235` |
| STM32H747 | `3333` | `1234` | `3333` (M1), `3334` (M2) | `1234`, `1235` |

The UART sockets speak telnet. `uart_console.py` handles the negotiation and
can prefix output from multiple machines with `[M1]` / `[M2]`.

## VS Code + GDB

The debug scripts are intentionally independent of the launcher:

```bash
renode --console emulation/stm32f4-disco/debug.resc
renode --console emulation/stm32h7-disco/debug.resc
```

Then press `F5` and choose `Cortex Debug (F4)` or `Cortex Debug (H7)`.
The matching VS Code tasks are `Run Renode (F4)` / `Run Renode (H7)`.

For multi-machine debugging, use `Run Renode (F4 multi-machine)` or
`Run Renode (H7 multi-machine)` and attach GDB to `1234` / `1235`.

Every `.resc` ends with:

```text
echo "MOS_RENODE_READY"
```

The launcher and VS Code background-task matcher wait for this marker, so GDB
does not connect while the Renode script is still creating machines.

## Adding a board

1. Add the board to CMake (`MOS_BOARD`) and `examples/boards/<board>/`.
2. Create `examples/boards/<board>/main.cpp` (one standalone program per board).
3. Create `emulation/<board>/add_on.repl`, `debug.resc` and `multi_mach.resc`.
4. Add the board mapping in `emulation/run.sh` (`BUILD_TARGET`, `ELF`,
   `RESC_DIR`) and in `build.sh`.
5. Add VS Code launch/task entries if the board should support `F5`.
6. Keep `echo "MOS_RENODE_READY"` at the end of every `.resc`.
