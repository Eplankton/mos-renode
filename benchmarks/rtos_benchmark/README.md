# RTOS-Benchmark for MOS

This directory contains a MOS port of
[`zephyrproject-rtos/rtos-benchmark`](https://github.com/zephyrproject-rtos/rtos-benchmark)
(Apache-2.0). The suite measures the latency of individual RTOS kernel
services rather than throughput, which complements the Thread-Metric suite in
[`../thread_metric`](../thread_metric).

The upstream sources live in the
[`../external/rtos-benchmark`](../external/rtos-benchmark) git submodule; the
common test files are compiled unmodified. Upstream also carries ports for
Zephyr, FreeRTOS, NuttX, RTEMS and VxWorks; this port follows the same
`bench_api.h` model.

| File | Role |
|---|---|
| [`bench_porting_layer_mos.hpp`](bench_porting_layer_mos.hpp) | kernel half: threads, semaphores, mutexes, queues on MOS |
| [`bench_timer_cortex_m.hpp`](bench_timer_cortex_m.hpp) | timing half: DWT/TIM2 counter, SysTick hooks; shared with the FreeRTOS build |
| [`bench_porting_layer_mos.h`](bench_porting_layer_mos.h) | feature flags, force-included into the test sources |
| [`main.cpp`](main.cpp) | entry point |

## Tests

| Test | What it measures |
|---|---|
| `bench_thread_test.c` | Thread create/start/suspend/resume/terminate, with and without a context switch |
| `bench_thread_switch_yield_test.c` | Yield with and without a context switch |
| `bench_mutex_lock_unlock_test.c` | Mutex lock/unlock, recursive lock, unpend and priority inheritance |
| `bench_sem_context_switch_test.c` | Semaphore give/take across a context switch |
| `bench_sem_signal_release_test.c` | Semaphore give/take without a context switch |
| `bench_malloc_free_test.c` | 128-byte allocation/free latency |
| `bench_message_queue_test.c` | Queue create/send/receive, with and without a context switch |
| `bench_interrupt_latency_test.c` | Timer interrupt entry latency |

The results are printed as `avg, min, max` in nanoseconds. The upstream suite
also supports selecting a single test; this port runs `bench_all()` in one
firmware, which is more convenient for Renode regression runs.

## Quick start

```bash
./benchmarks/rtos_benchmark/run.sh stm32f4-disco
./benchmarks/rtos_benchmark/run.sh stm32h7-disco --iterations 1000
```

The submodule is fetched on first use (see
[`../README.md`](../README.md#running)).

The runner builds `build/<board>/rtos_benchmark.elf`, runs it in headless
Renode, waits for the `RTOS-BENCHMARK DONE` marker, and prints the UART log.
Logs are written under `build/<board>/rtos_benchmark/`.

Useful options:

```bash
./benchmarks/rtos_benchmark/run.sh stm32f4-disco --iterations 10000
./benchmarks/rtos_benchmark/run.sh stm32f4-disco --no-build
./benchmarks/rtos_benchmark/run.sh stm32f4-disco --timer-hz 0
```

`--iterations` controls the number of samples per test. The upstream default
is 10000; the MOS runner defaults to 1000 for a quicker turn-around. Use 10000
for publishable numbers. `--timer-hz` overrides the clock used for the ns
conversion (see the clock-model note below); the runner defaults it to the
Renode model's rate per board.

## Manual build

```bash
cmake -S . -B build/stm32f4-disco \
    -DMOS_BOARD=stm32f4-disco \
    -DMOS_RTOS_BENCHMARK=ON \
    -DMOS_RB_ITERATIONS=1000
cmake --build build/stm32f4-disco --target rtos_benchmark.elf -j
```

The normal board firmware (`stm32f4-disco.elf`) is still built as usual.

`build.sh` accepts the same options through the environment:

```bash
MOS_RTOS_BENCHMARK=ON MOS_RB_ITERATIONS=10000 ./build.sh stm32f4-disco
```

## MOS porting notes

- **Priorities**: RTOS-Benchmark and MOS both use smaller numbers for higher
  priority. `BENCH_LAST_PRIORITY` is 20, so all benchmark threads stay well
  above the idle task (`PRI_MIN`).
- **Thread model**: `bench_thread_create()` creates a MOS task whose entry
  trampoline waits on a private start semaphore. `bench_thread_start()` posts
  it. `bench_thread_suspend()`/`bench_thread_resume()` use a second per-thread
  semaphore plus a suspend flag, so a lower-priority thread can be suspended
  before it first runs. `bench_thread_abort()` maps to `Task::terminate()`;
  `bench_thread_exit()` clears the slot and calls `Task::exit()`.
- **No deferred cleanup**: MOS can terminate a task immediately, so
  `bench_collect_resources()` is the weak no-op from `bench_utils.c`.
- **Timing**: the port prefers the Cortex-M DWT `CYCCNT` counter and falls
  back to a free-running 32-bit TIM2. Renode models a DWT on STM32H7 (at
  250 MHz) but not on STM32F4, where the fallback TIM2 is modeled at 10 MHz.
  Because both rates differ from `SystemCoreClock`, `run.sh` passes the
  emulator's rate through `--timer-hz`, i.e. `MOS_RB_TIMER_HZ`. Leave
  `MOS_RB_TIMER_HZ=0` on real hardware so the port uses `SystemCoreClock`.
- **Two clocks**: the timing counter (DWT/TIM2, `MOS_RB_TIMER_HZ`) and
  SysTick (`SystemCoreClock`; the Renode scripts set `sysbus.nvic Frequency`
  to the same value). The interrupt-latency test measures in SysTick cycles,
  so `bench_timer_cycles_diff()` converts them into timing-counter cycles
  (rounded to nearest) before the common code turns them into ns. Earlier
  revisions used the timing clock for SysTick as well, which mis-scaled the
  latency (H7 reported 8 ns for what is 2 SysTick cycles, about 31 ns) and
  left the OS tick at the wrong rate after the test.
- **Interrupt latency**: the test chains the SysTick vector at index 15.
  `bench_timer_isr_set()` copies the vector table into a 1 KB RAM table,
  replaces SysTick, and points `VTOR` at it. The benchmark ISR gives a
  semaphore and then calls the previous SysTick handler, so MOS scheduling
  continues during the test. `bench_timer_isr_restore()` restores `VTOR` and
  the 1 kHz reload. Renode enters the exception immediately, so its reported
  latency is a lower bound; use hardware for meaningful absolute numbers.
- **Semaphores/mutexes**: semaphores use `Kernel::Sync::Sema_t`; mutexes use
  `Kernel::Sync::MutexImpl_t` so recursive locking and priority inheritance
  are exercised.
- **Message queue**: `Kernel::IPC::MsgQueue_t<char, 1>` with
  `IPC::FOREVER` timeouts provides the blocking 1-byte queue expected by the
  message-queue test.
- **Allocator**: MOS has no general-purpose heap, so the malloc/free test uses
  a static O(1) 128-byte block freelist
  ([`../common/block_pool.hpp`](../common/block_pool.hpp), shared with the
  Thread-Metric port). It measures this allocator, not the kernel page
  allocator.

## Sample Renode results

Renode 1.17.0, `-Os`, 1000 iterations, default `PreemptPri` policy. Values are
average latency in nanoseconds, converted with the clock the Renode model
actually drives (`--timer-hz`: 10 MHz TIM2 on STM32F4, 250 MHz DWT on
STM32H7).

| Operation | STM32F4 (ns) | STM32H747 (ns) |
|---|---:|---:|
| Thread create (no context switch) | 2,400 | 2,400 |
| Thread start (context switch) | 1,500 | 1,500 |
| Thread suspend (context switch) | 1,100 | 1,108 |
| Thread resume (context switch) | 1,500 | 1,508 |
| Thread terminate (context switch) | 1,700 | 1,720 |
| Yield (context switch) | 800 | 848 |
| Mutex lock (no owner) | 400 | 400 |
| Mutex unlock with unpend (context switch) | 3,000 | 3,060 |
| Mutex pend with priority inheritance | 2,000 | 2,012 |
| Semaphore take (context switch) | 1,000 | 1,000 |
| Semaphore give (context switch) | 1,400 | 1,412 |
| Message queue send (context switch) | 2,500 | 2,448 |
| Message queue receive (context switch) | 2,400 | 2,372 |
| Malloc (128 B) | 200 | 276 |
| Interrupt latency | 100 | 32 |

### What these numbers do and do not mean

The two boards land within a few percent of each other on every row. That is
expected: Renode's `CPU.CortexM` model advances a fixed number of cycles per
instruction and our firmware is board-independent, so the emulator — not the
silicon — determines the timing. Treat these values as a relative regression
baseline for MOS on Renode, not as hardware latency.

- **Interrupt latency is a lower bound.** Renode takes the exception almost
  in the same time step as the timer expiry: 1 SysTick cycle on F4 (62.5 ns,
  shown as 100 ns because the 10 MHz TIM2 cannot resolve less) and 2 cycles
  on H7 (31 ns). Real Cortex-M4/M7 entry latency is on the order of 12–20
  cycles plus flash wait states; measure on hardware.
- **Resolution floor.** The fallback TIM2 is modeled at 10 MHz on STM32F4
  (100 ns per tick) and the DWT at 250 MHz on STM32H7 (3.9 ns per tick). The
  shortest operations (mutex lock, recursive unlock, malloc/free) sit within
  one or two ticks of zero and should be read as "≤ 2 ticks", not as precise
  values.
- **No jitter data.** The upstream report only carries avg/min/max. On Renode
  min == avg == max for most rows because the model is deterministic; that is
  a property of the emulator, not evidence of low jitter. Real hardware shows
  a long tail, especially for interrupt latency.
- **Malloc/free measures the benchmark's own freelist**, not the MOS page
  allocator (MOS has no general-purpose heap). Do not compare it with
  FreeRTOS/Zephyr heap numbers.
- **Wrong-clock trap.** `bench_timing_cycles_to_ns()` must divide by the clock
  that actually drives the timer. Renode's STM32F4 TIM2 runs at 10 MHz and the
  STM32H7 DWT at 250 MHz even though the firmware's `SystemCoreClock` is
  16/64 MHz. Dividing by `SystemCoreClock` inflates STM32H7 numbers by 3.9x
  and shrinks STM32F4 numbers by 1.6x, which makes the H7 look ~6.5x slower
  than the F4 for identical code. The runner passes the correct rate through
  `--timer-hz` / `MOS_RB_TIMER_HZ`; keep `MOS_RB_TIMER_HZ=0` (the default) on
  real hardware.

The full output includes min/max values and all mutex/semaphore variants; see
`build/<board>/rtos_benchmark/rtos_benchmark.uart.log`.

## Comparison with FreeRTOS

[`../README.md`](../README.md#mos-vs-freertos) compares these numbers with
FreeRTOS built the same way (`./benchmarks/compare.sh`).

## License

The upstream suite is Apache-2.0; see
[`../external/rtos-benchmark/LICENSE`](../external/rtos-benchmark/LICENSE). The MOS
port follows the repository's Apache-2.0 license.
