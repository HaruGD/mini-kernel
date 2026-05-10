ENTRY(_start64)
SECTIONS
{
    . = 0x100000;

    .text :
    {
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data*)
    }

    .init_array :
    {
        __init_array_start = .;
        KEEP(*(.init_array*))
        __init_array_end = .;
    }

    .bss :
    {
        *(COMMON)
        *(.bss*)
    }
}
