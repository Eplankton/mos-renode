# STM32 dependencies

These components are managed as git submodules so the RTOS does not carry a
second copy of the STM32 HAL/CMSIS sources.

| Submodule | Purpose |
|---|---|
| `cmsis-core` | CMSIS Cortex-M core headers |
| `cmsis-device-f4` | STM32F4 device headers, startup and system files |
| `cmsis-device-h7` | STM32H7 device headers, startup and system files |
| `stm32f4xx-hal-driver` | STM32F4 HAL and LL drivers |
| `stm32h7xx-hal-driver` | STM32H7 HAL and LL drivers |

## Update

```bash
git submodule update --init --recursive
# or, to move a component to its latest upstream commit:
git submodule update --remote vendor/stm32/<component>
```

Board-specific files (HAL configuration, linker script) stay in `examples/<board>/`
because they are part of this project rather than the upstream vendor trees.
