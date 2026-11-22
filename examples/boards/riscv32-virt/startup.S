/*
 * Minimal machine-mode startup for Renode's RISC-V32 Virtual.
 *
 * The image is loaded straight into RAM, so .data needs no copy. Sets up
 * gp/sp, clears .bss, runs the C++ constructors and calls main(). Traps
 * before the scheduler starts land in `early_trap` (a stop);
 * Arch::sched_init() installs the kernel trap vector.
 */

    .section .init, "ax", @progbits
    .globl _start
    .type  _start, @function
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, __stack_top

    csrw    mie, zero
    csrci   mstatus, 8
    la      t0, early_trap
    csrw    mtvec, t0

    /* Clear .bss */
    la      a0, __bss_start
    la      a1, __bss_end
3:  bgeu    a0, a1, 4f
    sw      zero, 0(a0)
    addi    a0, a0, 4
    j       3b

4:  call    __libc_init_array

    /* Like PRIMASK=0 after a Cortex-M reset: globally enabled, but no source
     * is enabled in `mie` until Arch::sched_init(), so nothing is taken yet. */
    csrsi   mstatus, 8
    call    main
5:  wfi
    j       5b
    .size  _start, . - _start

    .balign 4
early_trap:
    wfi
    j       early_trap

    /* Called by __libc_init_array / exit; the init_array does the work. */
    .text
    .globl _init
    .globl _fini
_init:
_fini:
    ret
