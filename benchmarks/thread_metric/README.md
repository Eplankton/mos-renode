# Thread-Metric for MOS

This directory contains a MOS port of the
[Thread-Metric](https://github.com/sysprog21/thread-metric-benchmark) RTOS
benchmark suite. The benchmark sources and the RTOS-neutral API live in the
[`../external/thread-metric-benchmark`](../external/thread-metric-benchmark)
submodule (MIT license); only the porting layer in [`tm_port.hpp`](tm_port.hpp)
and the entry point in [`main.cpp`](main.cpp) are MOS-specific.

Thread-Metric is the same test suite used by Zephyr and other RTOS projects:
each test runs a small workload for a fixed interval and reports the number of
completed operations, which is then normalized to operations per second.

For per-operation latency (context switch, mutex, semaphore, queue, interrupt),
see the companion [`../rtos_benchmark`](../rtos_benchmark) suite.

## Tests

| Test | Source | What it measures |
|---|---|---|
| Basic Processing | `basic_processing.c` | Single-thread processing baseline |
| Cooperative Scheduling | `cooperative_scheduling.c` | Five equal-priority threads doing round-robin yield |
| Preemptive Scheduling | `preemptive_scheduling.c` | Priority-ordered resume/suspend chain |
| Interrupt Processing | `interrupt_processing.c` | In-line ISR body + semaphore handshake |
| Interrupt Preemption | `interrupt_preemption_processing.c` | Real IRQ entry/exit + higher-priority wake-up |
| Message Processing | `message_processing.c` | Send/receive of 4-word queue messages |
| Synchronization | `synchronization_processing.c` | Semaphore get/put cycle |
| Memory Allocation | `memory_allocation.c` | 128-byte block allocate/free cycle |

## Quick start

```bash
./benchmarks/thread_metric/run.sh stm32f4-disco
```

The submodule is fetched on first use (see
[`../README.md`](../README.md#running)).

The runner configures CMake with `MOS_THREAD_METRIC=ON`, builds one ELF per
test, runs every test in headless Renode, and prints a summary:

```text
Test                                     Operations     Ops/second   Status
--------------------------------------------------------------------------
basic_processing                              60942          12188     PASS
cooperative_scheduling                      5281363        1056273     PASS
...
```

Results and per-test logs are written to
`build/<board>/thread_metric/`:

```text
build/stm32f4-disco/thread_metric/
├── results.csv
├── basic_processing.uart.log
├── basic_processing.renode.log
└── ...
```

Board selection:

```bash
./benchmarks/thread_metric/run.sh stm32h7-disco
```

Useful runner options:

```bash
./benchmarks/thread_metric/run.sh stm32f4-disco --duration 30
./benchmarks/thread_metric/run.sh stm32f4-disco --tests basic_processing,message_processing
./benchmarks/thread_metric/run.sh stm32f4-disco --duration 30 --cycles 3
./benchmarks/thread_metric/run.sh stm32f4-disco --no-build
```

`--duration` is the reporting interval in RTOS seconds. The upstream default
is 30 seconds; the runner uses 5 seconds for a quicker turn-around. Use 30
seconds when comparing results across builds or boards.

## Manual build

```bash
cmake -S . -B build/stm32f4-disco \
    -DMOS_BOARD=stm32f4-disco \
    -DMOS_THREAD_METRIC=ON \
    -DMOS_TM_DURATION=5 \
    -DMOS_TM_CYCLES=1
cmake --build build/stm32f4-disco -j
```

This produces `build/stm32f4-disco/tm_<test>.elf`. Each ELF contains exactly
one Thread-Metric test. The normal board firmware (`stm32f4-disco.elf`) is
still built as usual.

`build.sh` accepts the same options through the environment:

```bash
MOS_THREAD_METRIC=ON MOS_TM_DURATION=30 ./build.sh stm32f4-disco
```

## MOS porting notes

- **Priorities**: Thread-Metric priority `1` is the highest. MOS uses smaller
  numeric priorities for higher priorities, so the port maps
  `TM priority n` to `n - 1`. The idle task remains at `PRI_MIN`.
- **Thread creation**: `tm_thread_create()` creates a MOS task in the ready
  queue. Each task waits on a private counting semaphore before running its
  entry function; `tm_thread_resume()` posts that semaphore and
  `tm_thread_suspend()` waits on it. This reproduces the
  create-suspended/resume semantics of the reference ports.
- **Scheduling**: Run the benchmarks with the default `PP` (preemptive
  priority) policy.
  A cooperative or non-preemptive policy does not satisfy the assumptions of
  the preemptive and interrupt-preemption tests.
- **Interrupts**: `tm_cause_interrupt()` pends `EXTI0_IRQn` in the NVIC and
  lets the normal Cortex-M exception path save/restore context.
  `tm_cause_interrupt_sync()` calls the ISR body in-line with interrupts
  masked, matching the Thread-Metric definition of "interrupt processing"
  without a scheduler round-trip. The port does not trigger PendSV itself:
  `Sema_t::up_from_isr()` requests the switch when it wakes a higher-priority
  task.
- **Queues**: `tm_queue_*` wraps `Kernel::IPC::MsgQueue_t` with a 10-entry
  queue of 4 `unsigned long` words.
- **Semaphores**: `tm_semaphore_get()` is the non-blocking
  `Sema_t::try_down()`, as in the reference ports. `tm_semaphore_put()` uses the ISR-safe path when called
  from the benchmark ISR.
- **Memory pool**: the memory-allocation test uses a static O(1) 128-byte
  block freelist ([`../common/block_pool.hpp`](../common/block_pool.hpp)),
  so it measures the allocator loop rather than the MOS page allocator. Its
  `allocate()`/`release()` are deliberately not inlined: with LTO the
  compiler would otherwise cancel each allocate/free pair and the test would
  time an empty loop (earlier results of ~16.6 M ops/s were exactly that). This matches the reference ports' intent of comparing RTOS
  primitive overhead, not heap implementations.
- **Output**: `tm_putchar()` goes through the board's `_putchar` hook
  (`USART2` on STM32F4, `USART1` on STM32H747). After one report,
  `tm_semihosting_exit()` prints a `THREAD-METRIC DONE` marker and parks the
  CPU; the host runner uses the marker to stop Renode.

## Sample Renode results

Renode 1.17.0, `-Os`, 5-second interval, one report, default `PP`
policy. These numbers are useful for relative comparisons on the same setup;
they are not calibrated hardware measurements.

| Test | STM32F4 (ops/s) | STM32H747 (ops/s) |
|---|---:|---:|
| Basic Processing | 12,189 | 12,189 |
| Cooperative Scheduling | 1,255,357 | 1,255,417 |
| Preemptive Scheduling | 520,450 | 520,475 |
| Interrupt Processing | 1,784,401 | 1,885,495 |
| Interrupt Preemption | 402,930 | 402,949 |
| Message Processing | 876,554 | 925,289 |
| Synchronization | 1,959,343 | 2,081,900 |
| Memory Allocation | 5,551,469 | 5,551,733 |

Compared with the kernel before the v0.5 fixes (STM32F4): cooperative
+27%, preemptive +47%, interrupt processing +45%, interrupt preemption +31%,
message processing +66%, synchronization +96%. The gains come from the
unpacked TCB, the list without barriers/length counting, `any_higher()` no
longer forcing a rotation among equal-priority tasks, the semaphore token
hand-off, and a shorter `next_tcb()` (direct priority compares, no repeated
volatile loads). Memory allocation is not comparable: the old 16.6 M ops/s
was an empty loop (see the porting notes).

The interval is driven by the RTOS tick. The runner sets
`sysbus.nvic Frequency` to the firmware's `SystemCoreClock` (16 MHz on
STM32F4, 64 MHz on STM32H747), so one tick is one millisecond of emulated
time. Renode is not cycle-accurate, though: it executes a fixed number of
instructions per virtual second, which is why both boards report nearly
identical rates. Compare ratios and regressions, not absolute values.

## Comparison with FreeRTOS

[`../README.md`](../README.md#mos-vs-freertos) runs the same tests on
FreeRTOS, built with upstream's FreeRTOS port (`./benchmarks/compare.sh`).

## License

The benchmark suite is MIT-licensed; see
[`../external/thread-metric-benchmark/LICENSE`](../external/thread-metric-benchmark/LICENSE). The MOS
port follows the repository's Apache-2.0 license.
