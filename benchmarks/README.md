# Benchmarks

Two open RTOS benchmark suites, ported to MOS, plus a FreeRTOS build of
both for comparison. Everything runs headless in Renode.

| Suite | Measures | MOS port |
|---|---|---|
| [Thread-Metric](thread_metric/README.md) | throughput: operations per second over a fixed interval | `thread_metric/` |
| [RTOS-Benchmark](rtos_benchmark/README.md) | latency of single kernel operations, in ns | `rtos_benchmark/` |

```text
benchmarks/
├── compare.sh          # MOS vs FreeRTOS, both suites
├── common/             # allocator for the allocation tests
├── thread_metric/      # MOS port, run.sh
├── rtos_benchmark/     # MOS port, run.sh, shared timing code
├── freertos/           # FreeRTOS config and ports
└── external/           # submodules, upstream names
    ├── thread-metric-benchmark/
    ├── rtos-benchmark/
    └── FreeRTOS-Kernel/
```

## Running

The sources under `external/` are submodules marked `update = none` in
`.gitmodules`, so a recursive clone or `git submodule update --recursive`
skips them. CMake fetches the ones a configuration needs (with
`--checkout`) the first time a benchmark is built; to fetch or update one
by hand:

```bash
git submodule update --init --checkout benchmarks/external/FreeRTOS-Kernel
```

```bash
./benchmarks/thread_metric/run.sh stm32f4-disco     # MOS only
./benchmarks/rtos_benchmark/run.sh stm32f4-disco    # MOS only
./benchmarks/compare.sh                             # MOS and FreeRTOS, ~2 min
```

The suites support the Cortex-M boards (`stm32f4-disco`, `stm32h7-disco`);
the FreeRTOS reference build exists for `stm32f4-disco`. `compare.sh`
writes `build/stm32f4-disco-compare/compare.md` and keeps every UART log
under `build/stm32f4-disco-compare/results/`. Options: `--duration`,
`--iterations`, `--assert`, `--jobs`, `--no-build`.

## MOS vs FreeRTOS

### Setup

Both kernels are built from one CMake configuration and differ only in the
kernel and its porting layer:

| | Same for both |
|---|---|
| Machine | Renode 1.17 `stm32f4_discovery-kit`: Cortex-M4F at 100 MIPS (Renode default), 16 MHz SysTick clock, 1 kHz tick |
| Build | arm-none-eabi-gcc, `-Os`, same board startup and linker script; LTO for Thread-Metric, no LTO for RTOS-Benchmark (as in the MOS build) |
| Tests | the upstream test sources, unmodified |
| Timing | `rtos_benchmark/bench_timer_cortex_m.hpp` (TIM2 at 10 MHz in Renode, SysTick hooks for the interrupt test) |
| Allocation tests | the same 128-byte freelist (`common/block_pool.hpp`) |
| Assertions | off in both (`MOS_CONF_ASSERT`, `configASSERT`); `--assert` keeps them |

| | MOS | FreeRTOS V11.3.3 |
|---|---|---|
| Scheduling | `PP` (preemptive priority), 50 ms time slice | preemptive, time slicing every tick, port-optimised task selection |
| Thread-Metric port | `thread_metric/tm_port.hpp` | upstream's `ports/freertos/tm_port.c`, unmodified; `freertos/tm_main.c` adds the STM32 glue |
| RTOS-Benchmark port | `rtos_benchmark/bench_porting_layer_mos.hpp` | `freertos/rb_port.cpp`, after upstream's K64 port |

### Results

`./benchmarks/compare.sh`, Thread-Metric 5 s, RTOS-Benchmark 1000
iterations. Speedup is MOS over FreeRTOS, so above 1 means MOS is faster.
The last column is the same with assertions on (`--assert`).

**Thread-Metric** (operations per second, higher is better)

| Test | MOS | FreeRTOS | Speedup | with asserts |
|---|---:|---:|---:|---:|
| Basic processing (no kernel calls) | 12,188 | 12,192 | 1.00 | 1.00 |
| Cooperative scheduling | 1,255,357 | 1,513,615 | 0.83 | 0.87 |
| Preemptive scheduling | 527,038 | 380,932 | 1.38 | 1.49 |
| Interrupt processing | 1,784,401 | 1,218,988 | 1.46 | 1.91 |
| Interrupt preemption | 406,205 | 301,985 | 1.35 | 1.47 |
| Message processing | 876,547 | 707,410 | 1.24 | 1.44 |
| Synchronization | 2,039,315 | 1,218,996 | 1.67 | 2.10 |
| Memory allocation | 5,551,469 | 16,659,449 | n/a | n/a |

**RTOS-Benchmark** (average latency in ns, lower is better; TIM2 resolution
is 100 ns)

| Operation | MOS | FreeRTOS | Speedup | with asserts |
|---|---:|---:|---:|---:|
| Semaphore take (context switch) | 900 | 4,100 | 4.56 | 4.80 |
| Semaphore give (context switch) | 1,400 | 2,300 | 1.64 | 1.93 |
| Semaphore take (no context switch) | 300 | 500 | 1.67 | 2.33 |
| Semaphore give (no context switch) | 500 | 700 | 1.40 | 2.25 |
| Mutex lock (no owner) | 300 | 800 | 2.67 | 2.50 |
| Mutex unlock (no waiters) | 900 | 1,000 | 1.11 | 1.44 |
| Mutex recursive lock | 300 | 300 | 1.00 | 1.00 |
| Mutex recursive unlock | 400 | 300 | 0.75 | 0.60 |
| Mutex pend (no priority inheritance) | 1,500 | 4,700 | 3.13 | 3.44 |
| Mutex pend (priority inheritance) | 1,900 | 5,100 | 2.68 | 2.95 |
| Mutex unlock with unpend (no context switch) | 1,900 | 1,000 | 0.53 | 0.74 |
| Mutex unlock with unpend (context switch) | 3,000 | 3,200 | 1.07 | 1.20 |
| Message queue create | 700 | 1,200 | 1.71 | 1.86 |
| Message queue send (no context switch) | 700 | 1,000 | 1.43 | 1.71 |
| Message queue receive (no context switch) | 900 | 900 | 1.00 | 1.22 |
| Message queue send (context switch) | 2,500 | 2,900 | 1.16 | 1.32 |
| Message queue receive (context switch) | 2,400 | 2,900 | 1.21 | 1.38 |
| Yield (no context switch) | 700 | 700 | 1.00 | 1.00 |
| Yield (context switch) | 800 | 700 | 0.88 | 1.00 |
| Thread spawn (no context switch) ¹ | 2,900 | 3,200 | 1.10 | 1.14 |
| Thread spawn (context switch) ¹ | 5,100 | 3,700 | 0.73 | 0.76 |
| Thread create (no context switch) ¹ | 2,400 | 5,100 | 2.12 | 2.25 |
| Thread start (no context switch) ¹ | 500 | 1,000 | 2.00 | 2.00 |
| Thread start (context switch) ¹ | 1,400 | 1,600 | 1.14 | 1.13 |
| Thread suspend (no context switch) ² | 200 | 1,200 | n/a | n/a |
| Thread suspend (context switch) ¹ | 1,100 | 1,900 | 1.73 | 1.91 |
| Thread resume (no context switch) ¹ | 500 | 1,000 | 2.00 | 2.00 |
| Thread resume (context switch) ¹ | 1,500 | 1,600 | 1.07 | 1.13 |
| Thread terminate (context switch) ³ | 1,700 | 4,400 | n/a | n/a |
| Malloc / free (128 B) | 200 / 200 | 200 / 200 | n/a | n/a |
| Interrupt latency | 100 | 100 | n/a | n/a |

¹ MOS has no API to create a task without starting it, or to suspend and
resume another task, so its port builds them from a per-thread semaphore:
start and resume post it, suspend waits on it. FreeRTOS uses
`xTaskCreateStatic` + `vTaskSuspend`, `vTaskResume` and `vTaskSuspend`
directly. Both make the same thread runnable or not, through different
code paths.
² Suspending another, lower-priority thread only sets a flag in the MOS
port; the thread blocks when it next runs, outside the measurement.
³ The FreeRTOS port, like upstream's, does not delete an exiting thread
(its static TCB could not be reused before the idle task reaps it): the
thread blocks on a semaphore and is deleted later. MOS releases the task's
pool page in `Task::exit()` itself.

### Reading the numbers

- **MOS is faster at blocking and waking**: semaphores, mutexes and
  message queues with a context switch, and the Thread-Metric tests built
  on them (1.2x to 1.7x). A MOS semaphore or queue blocks by inserting
  the task into a sorted wait list; FreeRTOS routes semaphores, mutexes and
  queues through its generic queue and event-list code.
- **FreeRTOS is still faster at plain switching**: a yield with a switch
  (0.88x), cooperative scheduling (0.83x) and creating a higher-priority
  thread that runs at once. Traced instruction by instruction, a yield
  with a switch is 86 instructions on MOS and 77 on FreeRTOS. FreeRTOS
  picks the next task with one `clz` over a ready-priority bitmap and a
  per-priority list; MOS keeps one sorted ready list for every policy and
  decides through the policy traits, records the running tid for
  debuggers (`MOS_CONF_DEBUG_INFO`) and checks the sleeping list on every
  switch. (Before the fast-path changes of v0.5 it was 101 instructions,
  0.70x.)
- **Handing a mutex to a waiter without a switch** costs MOS 1.9x: the
  per-mutex priority inheritance recomputes the old and the new owner's
  priority on every hand-off, while FreeRTOS only restores a priority it
  actually raised.
- **Assertions** barely change MOS (a few checks on the IRQ state) but
  slow FreeRTOS by 5–30%, which is why the default comparison turns them
  off in both kernels.

### Not comparable

- **Thread-Metric memory allocation**: upstream's FreeRTOS port keeps its
  freelist in `tm_port.c`; with LTO the compiler inlines it and cancels
  each allocate/release pair, so 16.6 M ops/s is an empty loop. The MOS
  side measures `common/block_pool.hpp`, whose functions are kept out of
  line for this reason.
- **Malloc/free** measure the same freelist in both builds, not the kernel.
- **Interrupt latency** is a floor: Renode takes the exception in the same
  time step as the timer expiry, so both report one TIM2 tick.
- **FreeRTOS cooperative scheduling** prints Thread-Metric's "counters
  should not be more than 1 different" warning: with a time slice of one
  tick, the tick can switch a thread out before it increments its
  counter. The total is unaffected.

### Limits

Renode is not cycle-accurate: it executes a fixed number of instructions
per virtual second and does not model flash wait states, caches or bus
contention. The ratios rank the two kernels' code paths by length on the
same model; absolute values, and in particular the interrupt and context
switch paths, need hardware to be meaningful.
