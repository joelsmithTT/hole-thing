/* Linker script for X280 bare-metal programs
 *
 * X280 L3 LIM (Local Instruction Memory / scratchpad):
 *   At reset: 128 KiB cache + 1920 KiB LIM (15 ways * 128 KiB)
 *   LIM address range: 0x0800_0000 to 0x09DF_FFFF (~1.875 MiB usable)
 *
 * Host writes binary here via NOC and sets reset vector to 0x0800_0000.
 */

OUTPUT_FORMAT("elf64-littleriscv", "elf64-littleriscv", "elf64-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    /* L3 LIM at reset = 15 ways * 128 KiB = 1920 KiB */
    LIM (rwx) : ORIGIN = 0x08000000, LENGTH = 1920K
}

__stack_size = 8K;

PHDRS
{
    text PT_LOAD;
}

SECTIONS
{
    .text : ALIGN(4) {
        *(.start)
        *(.text)
        *(.text.*)
        . = ALIGN(8);
    } > LIM :text

    .rodata : ALIGN(8) {
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
        . = ALIGN(8);
    } > LIM :text

    .data : ALIGN(8) {
        __data_start = .;
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        . = ALIGN(8);
        __data_end = .;
    } > LIM :text

    .bss : ALIGN(8) {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > LIM :text

    /* Stack at end of region (grows down) */
    .stack (NOLOAD) : ALIGN(16) {
        . = . + __stack_size;
        __stack_top = .;
    } > LIM

    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.gnu_debuglink)
        *(.gnu.lto_*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}

__global_pointer$ = ADDR(.data) + 0x800;

