# Examples

One standalone firmware per board, named after the board:

```text
examples/
├── common/                     # shared UART driver, platform headers, tests
│   ├── uart.hpp
│   ├── platform.hpp
│   └── tests.hpp
└── boards/
    ├── stm32f4-disco/
    │   ├── main.cpp            # STM32F4 Discovery program
    │   ├── board.hpp
    │   ├── stm32f4xx_hal_conf.h
    │   └── STM32F407VGTx_FLASH.ld
    └── stm32h7-disco/
        ├── main.cpp            # STM32H747I-DISCO program
        ├── board.hpp
        └── stm32h7xx_hal_conf.h
```

Each example contains its own `main()` and task setup. The only difference
between the two files is the board alias:

```cpp
namespace Board = MOS::Boards::STM32F4Disco;
// or
namespace Board = MOS::Boards::STM32H747Disco;
```

## Build

```bash
./build.sh                        # interactive board menu
./build.sh stm32f4-disco
./build.sh stm32h7-disco
./build.sh all
```

Or directly with CMake:

```bash
cmake -S . -B build/stm32f4-disco -DMOS_BOARD=stm32f4-disco
cmake --build build/stm32f4-disco -j
```

Artifacts:

```text
build/<board>/<board>.elf
build/<board>/<board>.bin
build/<board>/<board>.hex
```

## Add a board

1. Create `examples/boards/<board>/` with `main.cpp` (the program),
   a header-only `board.hpp` (board init, UART, LED), the HAL configuration
   and the linker script.
2. Add the board to `MOS_BOARD` in `CMakeLists.txt` (startup, linker script,
   HAL sources) and to `build.sh`.
3. Add `emulation/<board>/` scripts and a mapping in `emulation/run.sh`.
