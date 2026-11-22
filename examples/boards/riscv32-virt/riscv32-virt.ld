/*
 * Renode RISC-V32 Virtual: the image is loaded into RAM at 0x80000000
 * (riscv_virt models 2 GB; 256 KB is used).
 *
 * RAM: code | .data | .bss | heap -> ... <- boot/interrupt stack (__stack_top)
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    ram (rwx) : ORIGIN = 0x80000000, LENGTH = 256K
}

/* Separate segments keep code R+X and data R+W. */
PHDRS
{
    text PT_LOAD FLAGS(5); /* R X */
    data PT_LOAD FLAGS(6); /* R W */
    bss  PT_LOAD FLAGS(6); /* R W */
}

/* Boot stack, reused as the interrupt stack once the scheduler runs. */
__stack_size = 2K;

SECTIONS
{
    .init : { KEEP(*(.init)) } >ram :text

    .text :
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } >ram :text

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >ram :text

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >ram :text

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array .dtors))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >ram :text

    .eh_frame : { KEEP(*(.eh_frame)) } >ram :text

    .data : ALIGN(8)
    {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(8);
        /* gp-relative small data lives around __global_pointer$ (+-2 KB) */
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.srodata .srodata.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } >ram :data

    .bss (NOLOAD) : ALIGN(8)
    {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } >ram :bss

    /* Heap start for _sbrk() */
    PROVIDE(_end = .);
    PROVIDE(end = .);

    __stack_top = ORIGIN(ram) + LENGTH(ram);
    __heap_end  = __stack_top - __stack_size; /* _sbrk() refuses to grow past it */

    ASSERT(_end <= __stack_top - __stack_size, "RAM overflow: code + .data + .bss collide with the stack")
}
