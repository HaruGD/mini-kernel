ENTRY(_start)
OUTPUT_FORMAT(binary)
SECTIONS
{
    . = 0x01000;
    .text :
    {
        *(.text)
    }

    .rodata :
    {
        *(.rodata)
    }

    .data :
    {
        *(.data)
    }

    .ctors :
    {
        __ctors_start = .;
        *(.ctors)
        __ctors_end = .;
    }

    .init_array :
    {
        __init_array_start = .;
        *(.init_array)
        __init_array_end = .;
    }

    .bss :
    {
        *(COMMON)
        *(.bss)
    }
}