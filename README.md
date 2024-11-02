<p align="center">
<img src="pic/word_logo.svg" width="25%">
</p>

### Introduction 🚀
-  **[English](https://github.com/Eplankton/mos-renode/blob/master/README.md) | [中文](https://gitee.com/Eplankton/mos-renode/blob/master/README.md)**
```
 A_A       _    MOS Real-Time Operating System
o'' )_____//    Simple RTOS on Cortex-M
 `_/  MOS  )    Developed with C/C++, Tested on Renode
 (_(_/--(_/     [Apache License Version 2.0]
```

### Installation 📦

- Run `git submodule init && git submodule update` to pull the submodules.
- Install `arm-none-eabi-gcc` toolchain.
- Install **[EIDE](https://em-ide.com)** plugin, open `*.code-workspace` with `VSCode`, and run `Build` script to compile.
- Install **[Renode](https://github.com/renode/renode?tab=readme-ov-file#installation)** emulation platform, and add `renode` to `/usr/bin` path.
- Run `./run.sh emulation/*.resc` to start the emulation, type `s` to start, and `q` to quit.
- Open a `TCP` connection to `localhost:3333/3334` and observe the serial output.

### Manual 📚

- **[用户手册(中文)](manual_zh.pdf) | [Manual(English)](translation in progress)**


### Repository 🌏
- **[GitHub(English)](https://github.com/Eplankton/mos-renode) | [Gitee(中文)](https://gitee.com/Eplankton/mos-renode/)**


### Architecture 🔍
<img src="pic/mos-arch.svg">

```C++
.
├── 📁 emulation          // Renode emulation script
├── 📁 vendor              // Vendor HAL (SPL/HAL/LL/...)
├── 📁 core
│   ├── 📁 arch              // Architecture-Specific Code
│   │   └── cpu.hpp          // Initialization/Context Switching
│   │
│   ├── 📁 kernel            // Kernel Layer (Architecture-Independent)
│   │   ├── macro.hpp        // Kernel Constants Macro
│   │   ├── type.hpp         // Basic Types
│   │   ├── concepts.hpp     // Type Constraints (Optional)
│   │   ├── data_type.hpp    // Basic Data Structures
│   │   ├── alloc.hpp        // Memory Management
│   │   ├── global.hpp       // Kernel Layer Global Variables
│   │   ├── printf.h/.c      // Thread-Safe printf (Reference Open Source Implementation)
│   │   ├── task.hpp         // Task Control
│   │   ├── sync.hpp         // Synchronization Primitives
│   │   ├── scheduler.hpp    // Scheduler
│   │   ├── ipc.hpp          // Inter-Process Communication
│   │   └── utils.hpp        // Other Utilities
│   │
│   ├── config.h             // System Configuration
│   ├── kernel.hpp           // Kernel Modules
│   └── shell.hpp            // Shell Command Line
│
└── 📁 app                   // User Code
    ├── main.cpp             // Entry Function
    └── test.hpp             // Test Code
```

### Example 🍎
`Shell Test`
![shell_demo](pic/shell.gif)

`Mutex Test(Priority Ceiling Protocol)`
![mutex_test](pic/mutex.gif)

`LCD Driver & GUI Demo`<br>
<p align="center">
<img src="pic/cat.gif" width="21%"> <img src="pic/mac.gif" width="20.35%"> <img src="pic/face.gif" width="20.35%">
<img src="pic/board.gif" width="39.1%"> <img src="pic/guilite.gif" width="34.5%">
</p>

`Concurrent Task Period & Time Sequence`<br>
<p align="center">
<img src="pic/stmviewer.png" width="80%">
<img src="pic/T0-T1.png" width="80%">
<img src="pic/tids.png" width="65%">
</p>

```C++
// MOS Kernel & Shell
#include "mos/kernel.hpp"
#include "mos/shell.hpp"

// HAL and Device 
#include "drivers/stm32f4xx/hal.hpp"
#include "drivers/device/led.hpp"
```
```C++
namespace MOS::User::Global
{
    using namespace HAL::STM32F4xx;
    using namespace Driver::Device;
    using namespace DataType;

    // Shell I/O UART and Buffer
    auto& stdio = STM32F4xx::convert(USARTx);
    DataType::SyncRxBuf_t<16> io_buf;

    // LED red, green, blue
    Device::LED_t leds[] = {...};
}
```
```C++
namespace MOS::User::BSP
{
    using namespace Driver;
    using namespace Global;

    void LED_Config()
    {
        for (auto& led: leds) {
            led.init();
        }
    }

    void USART_Config()
    {
        stdio.init(9600-8-1-N)
             .rx_config(PXa)  // RX -> PXa
             .tx_config(PYb)  // TX -> PYb
             .it_enable(RXNE) // Enable RXNE interrupt
             .enable();       // Enable UART
    }
    ...
}
```
```C++
namespace MOS::User::App
{
    Sync::Barrier_t bar {2}; // Set Barrier to sync tasks

    void led1(Device::LED_t leds[])
    {
        bar.wait();
        for (auto _: Range(0, 20)) {
           leds[1].toggle(); // green
           Task::delay(250_ms);
        }
        kprintf(
            "%s exits...\n",
            Task::current()->get_name()
        );
    }

    void led0(Device::LED_t leds[])
    {
        Task::create(
            led1, 
            leds, 
            Task::current()->get_pri(),
            "led1"
        );
        bar.wait();
        while (true) {
            leds[0].toggle(); // red
            Task::delay(500_ms);
        }
    }
    ...
}
```
```C++
int main()
{
    using namespace MOS;
    using namespace Kernel;
    using namespace User;
    using namespace User::Global;

    BSP::config(); // Init hardware and clocks

    Task::create( // Create Calendar with RTC
        App::time_init, nullptr, 0, "time/init"
    );

    Task::create( // Create Shell with buffer
        Shell::launch, &stdio.buf, 1, "shell"
    );

    /* User Tasks */
    Task::create(App::led_init, &leds, 2, "led/init");
    ...

    /* Test examples */
    Test::MutexTest();
    Test::MsgQueueTest();
    ...
    
    // Start scheduling, never return
    Scheduler::launch();
}
```

### Boot Up ⚡
```
 A_A       _   Version @ x.x.x(...)
o'' )_____//   Build   @ TIME, DATE
 `_/  MOS  )   Chip    @ MCU, ARCH
 (_(_/--(_/    2023-2024 Copyright by Eplankton

 Tid   Name   Priority   Status    Mem%
----------------------------------------
 #0    idle      15      READY      10%
 #1    shell      1      BLOCKED    21%
 #2    led0       2      RUNNING     9%
----------------------------------------
```

### Version 🧾


📦 `v0.1`

> ✅ Done:
> 
> - Basic data structures, scheduler, and task control, memory management
>
> 📌 Planned: 
> 
> - Timers, round-robin scheduling
> - Inter-Process Communication (IPC), pipes, message queues
> - Process synchronization (Sync), semaphores, mutexes
> - Porting a simple Shell
> - Variable page sizes, memory allocator
> - SPI driver, porting GuiLite/LVGL graphics libraries
> - Porting to other boards/arch, e.g., ESP32-C3 (RISC-V)



📦 `v0.2`

> ✅ Done:
> 
> - Synchronization primitives `Sync::{Sema_t, Lock_t, Mutex_t<T>, CondVar_t, Barrier_t}`
> - `Scheduler::Policy::PreemptPri` with round-robin `RoundRobin` scheduling for same priority levels
> - `Task::terminate` implicitly called upon task exit to reclaim resources
> - Simple command-line interaction `Shell::{Command, CmdCall, launch}`
> - `HAL::STM32F4xx::SPI_t` and `Driver::Device::ST7735S_t`, porting the `GuiLite` graphics library
> - Blocking delay with `Kernel::Global::os_ticks` and `Task::delay`
> - Refactored project organization into `{kernel, arch, drivers}`
> - Support for `GCC` compilation, compatible with `STM32Cube HAL`
> - Real-time calendar `HAL::STM32F4xx::RTC_t`, `CmdCall::date_cmd`, `App::Calendar`
> - `idle` uses `Kernel::Global::zombie_list` to reclaim inactive pages
> - Three basic page allocation policies `Page_t::Policy::{POOL, DYNAMIC, STATIC}`



📦 `v0.3`

> ✅ Done:
>
> - Mapping `Tids` to `BitMap_t`
> - Message queue `IPC::MsgQueue_t`
> - `Task::create` allows generic function signatures as `void fn(auto argv)` with type checker
> - Added `ESP32-C3` as a `WiFi` component
> - Added `Driver::Device::SD_t`, `SD` card driver, porting `FatFs` file system
> - Added `Shell::usr_cmds` for user-registered commands
> - **[Experimental]** Atomic types `<stdatomic.h>`
> - **[Experimental]** `Utils::IrqGuard_t`, nested interrupt critical sections
> - **[Experimental]** Simple formal verification of `Scheduler + Mutex`
>
> 
>
> 📌 Planned: 
>
> - Inter-Process Communication: pipes/channels
> - `FPU` hardware float support
> - Performance benchmarking
> - Error handling with `Result<T, E>`, `Option<T>`
> - `DMA_t` DMA Driver
> - Software/hardware timers `Timer`
> - **[Experimental]** Adding `POSIX` support
> - **[Experimental]** Asynchronous stackless coroutines `Async::{Future_t, async/await}`
> - **[Experimental]** More real-time scheduling algorithms



📦 `v0.4`

> ✅ Done：
>
> - Shift to run `Renode` emulation platform, stable support for `Cortex-M` series
> - **[Experimental]** Add scheduler lock `Scheduler::suspend()`


### References 🛸
- [How to build a Real-Time Operating System(RTOS)](https://medium.com/@dheeptuck/building-a-real-time-operating-system-rtos-ground-up-a70640c64e93)
- [PeriodicScheduler_Semaphore](https://github.com/Dungyichao/PeriodicScheduler_Semaphore)
- [STM32F4-LCD_ST7735s](https://github.com/Dungyichao/STM32F4-LCD_ST7735s)
- [A printf/sprintf Implementation for Embedded Systems](https://github.com/mpaland/printf)
- [GuiLite](https://github.com/idea4good/GuiLite)
- [STMViewer](https://github.com/klonyyy/STMViewer)
- [FatFs](http://elm-chan.org/fsw/ff)
- [The Zephyr Project](https://www.zephyrproject.org/)
- [Eclipse ThreadX](https://github.com/eclipse-threadx/threadx)
- [Embassy](https://embassy.dev/)
- [Renode](https://renode.io/)

```
There's a movie on TV.
Four boys are walking on railroad tracks...
I better go, too.
```

<p align="center">
<img src="pic/cbp-green.svg">
</p>
