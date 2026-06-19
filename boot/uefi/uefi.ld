OUTPUT_FORMAT("elf64-x86-64")
ENTRY(efi_main)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x401000;

    . = ALIGN(0x1000);
    .text :
    {
        *(.text*)
    } :text

    . = ALIGN(0x1000);
    .rodata :
    {
        *(.rodata*)
    } :rodata

    . = ALIGN(0x1000);
    .data :
    {
        *(.data*)
    } :data

    .bss :
    {
        *(COMMON)
        *(.bss*)
    } :data

    /DISCARD/ :
    {
        *(.comment*)
        *(.eh_frame*)
        *(.note*)
    }
}
