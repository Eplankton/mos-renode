# SiFive register definitions

The HiFive1 board code accesses the FE310 peripherals through a CMSIS-style
device header generated from SiFive's own register description, the way the
STM32 boards use the CMSIS device headers.

```text
vendor/sifive/
├── LICENSE.Apache2, LICENSE.MIT   # freedom-e-sdk licenses
├── svd2header.py                  # SVD -> C header generator (part of MOS)
└── hifive1-revb/
    ├── design.svd                 # verbatim from freedom-e-sdk
    └── hifive1_revb.h             # generated, do not edit
```

## Source

| | |
|---|---|
| Repository | https://github.com/sifive/freedom-e-sdk (archived, read-only) |
| Commit | `4518a22c213d7fec6e23e20d24a2ba646014f6dd` (2024-07-01, last commit) |
| File | `bsp/sifive-hifive1-revb/design.svd` |
| License | Apache-2.0 and MIT (`LICENSE.Apache2`, `LICENSE.MIT`) |

Only the SVD is taken. The rest of the SDK (freedom-metal, its startup code,
trap handling and device-tree generated BSP) is not used: MOS owns the
startup and the trap vector (`core/arch/riscv32`), and the SDK is no longer
maintained.

The SVD does not describe interrupts; the PLIC source numbers used by the
board code (UART0 = 3, UART1 = 4) come from the same BSP's `core.dts`.

## Regenerate

```bash
python3 vendor/sifive/svd2header.py vendor/sifive/hifive1-revb/design.svd \
    > vendor/sifive/hifive1-revb/hifive1_revb.h
```

The header defines one struct per peripheral type (`UART_Type`,
`GPIO_Type`, `PRCI_Type`, `PLIC_Type`, ...), instance pointers (`UART0`,
`UART1`, `GPIO`, `PRCI`, `PLIC`, ...) and `<PERIPH>_<REG>_<FIELD>_Pos/_Msk`
macros. Registers named `name_<i>` become arrays indexed by `<i>`, e.g.
`PLIC->PRIORITY[source]`, `PLIC->ENABLE_0[source / 32]` (context 0). Static
assertions check every member against its SVD offset.
