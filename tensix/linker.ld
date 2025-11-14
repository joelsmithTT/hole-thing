/* Linker script for Tensix test programs */

OUTPUT_FORMAT("elf32-littleriscv", "elf32-littleriscv", "elf32-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    /* Tensix L1 SRAM - we have ~1.5MB available */
    L1 (rwx) : ORIGIN = 0x00000000, LENGTH = 1536K
}

PHDRS
{
    text PT_LOAD;
}

SECTIONS
{
    /* Code section - starts at 0x0 */
    .text 0x0 : {
        *(.start)           /* Entry point first */
        *(.text)
        *(.text.*)
        . = ALIGN(4);
    } > L1 :text

    /* Read-only data */
    .rodata : {
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
    } > L1 :text

    /* Data section */
    .data : {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        . = ALIGN(4);
    } > L1 :text

    /* BSS section */
    .bss : {
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        . = ALIGN(4);
    } > L1 :text

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.gnu_debuglink)
        *(.gnu.lto_*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}

/* Provide symbols for stack (if needed) */
__stack_top = ORIGIN(L1) + LENGTH(L1);

